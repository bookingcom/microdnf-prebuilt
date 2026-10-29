#include "rpmdb_mark.hpp"

#include "arguments.hpp"

#include <libdnf5-cli/argument_parser.hpp>
#include <libdnf5-cli/exception.hpp>
#include <libdnf5/base/goal.hpp>
#include <libdnf5/base/transaction.hpp>
#include <libdnf5/base/transaction_package.hpp>
#include <libdnf5/repo/package_downloader.hpp>
#include <libdnf5/rpm/package_query.hpp>
#include <libdnf5/utils/bgettext/bgettext-lib.h>
#include <libdnf5/utils/bgettext/bgettext-mark-domain.h>
#include <rpm/rpmdb.h>
#include <rpm/rpmio.h>
#include <rpm/rpmlib.h>
#include <rpm/rpmmacro.h>
#include <rpm/rpmts.h>

#include <ctime>
#include <deque>
#include <filesystem>
#include <iostream>
#include <map>
#include <set>

namespace dnf5 {

using libdnf5::cli::ArgumentParser;
using libdnf5::rpm::Package;
using libdnf5::rpm::PackageQuery;

namespace {

// The newest available build per name.arch, limited to `archlist`.
void narrow(PackageQuery & query, const std::vector<std::string> & archlist) {
    query.filter_available();
    if (!archlist.empty()) {
        query.filter_arch(archlist);
    }
    query.filter_priority();
    query.filter_latest_evr();
}

// A spec is NAME=VERSION, NAME=EVR (when it holds a release), or anything dnf accepts: a name, a NEVRA, a provide.
std::vector<Package> match_spec(libdnf5::Base & base, const std::string & spec, const std::vector<std::string> & archlist) {
    std::vector<Package> found;
    if (auto eq = spec.find('='); eq != std::string::npos) {
        auto version = spec.substr(eq + 1);
        PackageQuery query(base);
        query.filter_name(spec.substr(0, eq));
        if (version.find('-') != std::string::npos) {
            query.filter_evr(version);
        } else {
            query.filter_version(version);
        }
        narrow(query, archlist);
        found.assign(query.begin(), query.end());
    }
    if (found.empty()) {
        libdnf5::ResolveSpecSettings settings;
        settings.set_with_nevra(true);
        settings.set_with_provides(true);
        settings.set_with_filenames(true);
        settings.set_with_binaries(false);
        settings.set_expand_globs(true);
        PackageQuery query(base);
        query.resolve_pkg_spec(spec, settings, false);
        narrow(query, archlist);
        found.assign(query.begin(), query.end());
    }
    return found;
}

// `targets` and what the solver installs with them. A minimal repository may lack what the base system supplies
// (libc.so.6), which fails the solve; then each requirement is followed to the first package that provides it.
std::vector<Package> closure(libdnf5::Base & base, const std::vector<Package> & targets, const std::vector<std::string> & archlist) {
    libdnf5::Goal goal(base);
    for (const auto & pkg : targets) {
        goal.add_rpm_install(pkg);
    }
    auto transaction = goal.resolve();
    std::vector<Package> all(targets);
    if (transaction.get_problems() == libdnf5::GoalProblem::NO_PROBLEM) {
        for (const auto & tp : transaction.get_transaction_packages()) {
            if (tp.get_action() == libdnf5::transaction::TransactionItemAction::INSTALL) {
                all.push_back(tp.get_package());
            }
        }
        return all;
    }

    std::set<std::string> visited;
    std::deque<Package> queue;
    for (const auto & pkg : targets) {
        if (visited.insert(pkg.get_name()).second) {
            queue.push_back(pkg);
        }
    }
    while (!queue.empty()) {
        auto current = queue.front();
        queue.pop_front();
        auto requires_ = current.get_requires();
        for (int i = 0; i < requires_.size(); ++i) {
            PackageQuery query(base);
            query.filter_provides(requires_.get(i));
            narrow(query, archlist);
            if (query.empty()) {
                continue;
            }
            auto provider = *query.begin();
            if (visited.insert(provider.get_name()).second) {
                all.push_back(provider);
                queue.push_back(provider);
            }
        }
    }
    return all;
}

// Deletes what rpm hands out, whatever the exit path.
struct RpmTransaction {
    rpmts ts = rpmtsCreate();
    rpmtxn txn = nullptr;
    ~RpmTransaction() {
        if (txn) {
            rpmtxnEnd(txn);
        }
        rpmtsCloseDB(ts);
        rpmtsFree(ts);
    }
};

// Adds the header of the rpm file to the database, as rpm does when it installs a package but with no files.
void import_header(RpmTransaction & rpm, const std::string & path, const std::string & nevra) {
    FD_t fd = Fopen(path.c_str(), "r.ufdio");
    if (!fd) {
        throw libdnf5::cli::CommandExitError(1, M_("Cannot open downloaded RPM '{}'"), path);
    }
    Header header = nullptr;
    rpmRC rc = rpmReadPackageFile(rpm.ts, fd, path.c_str(), &header);
    Fclose(fd);
    // The signature is not ours to judge: dnf has checked the repository's.
    if ((rc != RPMRC_OK && rc != RPMRC_NOKEY && rc != RPMRC_NOTTRUSTED) || !header) {
        throw libdnf5::cli::CommandExitError(1, M_("Cannot read RPM header from '{}'"), path);
    }
    const auto now = static_cast<uint32_t>(std::time(nullptr));
    if (!headerIsEntry(header, RPMTAG_INSTALLTIME)) {
        headerPutUint32(header, RPMTAG_INSTALLTIME, &now, 1);
    }
    if (!headerIsEntry(header, RPMTAG_INSTALLTID)) {
        headerPutUint32(header, RPMTAG_INSTALLTID, &now, 1);
    }
    rc = rpmtsImportHeader(rpm.txn, header, 0);
    headerFree(header);
    if (rc != RPMRC_OK) {
        throw libdnf5::cli::CommandExitError(1, M_("Failed to import header for '{}' into rpmdb"), nevra);
    }
}

}  // namespace

void RpmdbMarkCommand::set_parent_command() {
    get_session().get_argument_parser().get_root_command()->register_command(get_argument_parser_command());
}

void RpmdbMarkCommand::set_argument_parser() {
    auto & parser = get_context().get_argument_parser();
    auto & cmd = *get_argument_parser_command();
    cmd.set_description(_("Record packages and their dependencies in an rpmdb without installing them"));

    auto specs_arg = parser.add_new_positional_arg("package-spec", ArgumentParser::PositionalArg::UNLIMITED, nullptr, nullptr);
    specs_arg->set_description(_("Packages to record, NAME or NAME=VERSION[-RELEASE]"));
    specs_arg->set_parse_hook_func([this]([[maybe_unused]] ArgumentParser::PositionalArg * arg, int argc, const char * const argv[]) {
        for (int i = 0; i < argc; ++i) {
            append_list(specs, argv[i]);
        }
        return true;
    });
    cmd.register_positional_arg(specs_arg);

    add_value_option(parser, cmd, "rpmdb-root", 0, "PATH", _("Directory that gets the rpmdb, under --dbpath"), [this](const char * v) { rpmdb_root = v; });
    add_value_option(parser, cmd, "dbpath", 0, "PATH", _("Path of the rpmdb below --rpmdb-root (default: /var/lib/rpm)"), [this](const char * v) { dbpath = v; });
    add_value_option(parser, cmd, "archlist", 0, "ARCH,...", _("Architectures a recorded package may have"), [this](const char * v) { append_list(archlist, v); });

    auto * no_deps = parser.add_new_named_arg("nodeps");
    no_deps->set_long_name("nodeps");
    no_deps->set_description(_("Record only the named packages, not their dependencies"));
    no_deps->set_parse_hook_func([this](ArgumentParser::NamedArg *, const char *, const char *) { return nodeps = true; });
    cmd.register_named_arg(no_deps);
}

void RpmdbMarkCommand::configure() {
    // Nothing is installed, so there is no rpmdb to read: the repositories are all the solver sees.
    get_context().set_load_system_repo(false);
    get_context().set_load_available_repos(Context::LoadAvailableRepos::ENABLED);
}

void RpmdbMarkCommand::load_additional_packages() {}

void RpmdbMarkCommand::run() {
    if (specs.empty()) {
        throw libdnf5::cli::CommandExitError(1, M_("Packages are not specified"));
    }
    if (rpmdb_root.empty() || rpmdb_root == "/") {
        throw libdnf5::cli::CommandExitError(1, M_("--rpmdb-root must be specified and cannot be '/'"));
    }
    auto & base = get_context().get_base();
    // As for the libdnf tool, the dependencies are the required ones only.
    base.get_config().get_install_weak_deps_option().set(libdnf5::Option::Priority::RUNTIME, false);

    std::vector<Package> targets;
    for (const auto & spec : specs) {
        auto matches = match_spec(base, spec, archlist);
        if (matches.empty()) {
            throw libdnf5::cli::CommandExitError(1, M_("No package matching '{}' found in repositories"), spec);
        }
        targets.push_back(matches.front());
    }
    auto packages = nodeps ? targets : closure(base, targets, archlist);
    std::map<std::string, Package> by_nevra;
    for (const auto & pkg : packages) {
        by_nevra.emplace(pkg.get_nevra(), pkg);
    }

    libdnf5::repo::PackageDownloader downloader(base);
    for (const auto & [nevra, pkg] : by_nevra) {
        downloader.add(pkg);
    }
    downloader.download();

    rpmPushMacro(nullptr, "_dbpath", nullptr, dbpath.c_str(), RMIL_CMDLINE);
    rpmPushMacro(nullptr, "_db_backend", nullptr, "sqlite", RMIL_CMDLINE);
    rpmPushMacro(nullptr, "_rpmlock_path", nullptr, "%{_dbpath}/.rpm.lock", RMIL_CMDLINE);
    // rpm wants an absolute root.
    rpmdb_root = std::filesystem::absolute(rpmdb_root).lexically_normal().string();
    std::filesystem::create_directories(std::filesystem::path(rpmdb_root) / std::filesystem::path(dbpath).relative_path());

    RpmTransaction rpm;
    rpmtsSetVSFlags(rpm.ts, RPMVSF_MASK_NOSIGNATURES);
    if (rpmtsSetRootDir(rpm.ts, rpmdb_root.c_str()) != 0) {
        throw libdnf5::cli::CommandExitError(1, M_("Failed to set RPM root directory to '{}'"), rpmdb_root);
    }
    rpm.txn = rpmtxnBegin(rpm.ts, RPMTXN_WRITE);
    if (!rpm.txn) {
        throw libdnf5::cli::CommandExitError(1, M_("Failed to begin RPM transaction for database write"));
    }
    for (const auto & [nevra, pkg] : by_nevra) {
        import_header(rpm, pkg.get_package_path(), nevra);
        std::cout << "Marked into rpmdb: " << nevra << std::endl;
    }
}

}  // namespace dnf5
