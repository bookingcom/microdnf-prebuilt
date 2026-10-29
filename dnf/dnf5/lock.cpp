#include "lock.hpp"

#include "arguments.hpp"

#include "lock_layers.hpp"

#include <glib.h>
#include <json-c/json.h>
#include <libdnf5-cli/argument_parser.hpp>
#include <libdnf5-cli/exception.hpp>
#include <libdnf5/base/goal.hpp>
#include <libdnf5/base/transaction.hpp>
#include <libdnf5/base/transaction_package.hpp>
#include <libdnf5/rpm/nevra.hpp>
#include <libdnf5/rpm/package_query.hpp>
#include <libdnf5/rpm/package_set.hpp>
#include <libdnf5/utils/bgettext/bgettext-lib.h>
#include <libdnf5/utils/bgettext/bgettext-mark-domain.h>

#include <algorithm>
#include <climits>
#include <cstdio>
#include <deque>
#include <map>
#include <set>

namespace dnf5 {

using libdnf5::cli::ArgumentParser;
using libdnf5::rpm::Package;
using libdnf5::rpm::PackageQuery;
using libdnf5::rpm::PackageSet;

namespace {

using NameSet = std::set<std::string>;

std::string name_arch(const Package & pkg) {
    return pkg.get_name() + "." + pkg.get_arch();
}

// Negative when `a` is the better build of one NEVRA: higher repo priority (a lower number), then lower cost.
int repo_prio_cost_cmp(const Package & a, const Package & b) {
    const auto * ra = a.get_repo().get();
    const auto * rb = b.get_repo().get();
    int result = (rb ? rb->get_priority() : 0) - (ra ? ra->get_priority() : 0);
    if (result == 0) {
        result = (ra ? ra->get_cost() : INT_MAX) - (rb ? rb->get_cost() : INT_MAX);
    }
    return result;
}

// Repos of equal priority each contribute their newest package, so a name.arch can appear once per repo, and installing
// every one of them fails with "cannot install both". Keep the highest EVR and, for equal EVRs, the repo that wins on
// priority and cost.
std::vector<Package> pick_one_per_name_arch(const std::vector<Package> & pkgs) {
    std::map<std::string, const Package *> best;
    for (const auto & pkg : pkgs) {
        auto [it, inserted] = best.emplace(name_arch(pkg), &pkg);
        if (inserted) {
            continue;
        }
        int cmp = libdnf5::rpm::evrcmp(pkg, *it->second);
        if (cmp > 0 || (cmp == 0 && repo_prio_cost_cmp(pkg, *it->second) < 0)) {
            it->second = &pkg;
        }
    }
    std::vector<Package> out;
    for (const auto & pkg : pkgs) {
        if (best.at(name_arch(pkg)) == &pkg) {
            out.push_back(pkg);
        }
    }
    return out;
}

// One package per NEVRA, in the iteration order of a GLib hash table keyed by NEVRA. That is the order of the
// lockfile's "packages", and the order dnf_lock produces.
std::vector<Package> one_per_nevra(const std::vector<Package> & pkgs) {
    std::deque<std::string> nevras;
    GHashTable * by_nevra = g_hash_table_new(g_str_hash, g_str_equal);
    for (const auto & pkg : pkgs) {
        nevras.push_back(pkg.get_nevra());
        auto * key = const_cast<char *>(nevras.back().c_str());
        auto * list = static_cast<GSList *>(g_hash_table_lookup(by_nevra, key));
        g_hash_table_insert(by_nevra, key, g_slist_prepend(list, const_cast<Package *>(&pkg)));
    }
    std::vector<Package> out;
    GHashTableIter iter;
    gpointer value;
    g_hash_table_iter_init(&iter, by_nevra);
    while (g_hash_table_iter_next(&iter, nullptr, &value)) {
        auto * sorted = g_slist_sort_with_data(
            static_cast<GSList *>(value),
            [](gconstpointer a, gconstpointer b, gpointer) {
                return repo_prio_cost_cmp(*static_cast<const Package *>(a), *static_cast<const Package *>(b));
            },
            nullptr);
        out.push_back(*static_cast<const Package *>(sorted->data));
        g_slist_free(sorted);
    }
    g_hash_table_destroy(by_nevra);
    return out;
}

// `base` and `rel` with exactly one slash between them.
std::string join_url(const std::string & base, const std::string & rel) {
    const bool base_slash = base.ends_with('/');
    const bool rel_slash = rel.starts_with('/');
    if (base_slash && rel_slash) {
        return base + rel.substr(1);
    }
    return base_slash || rel_slash ? base + rel : base + "/" + rel;
}

// Where the package is downloaded from: its location when that is a URL already, else the location under the package's
// base URL (xml:base), which is itself relative to the repo's first URL unless it is absolute.
std::string package_url(const Package & pkg) {
    auto location = pkg.get_location();
    if (location.empty() || location.find("://") != std::string::npos) {
        return location;
    }
    auto pkg_base = pkg.get_baseurl();
    std::string repo_base;
    if (auto * repo = pkg.get_repo().get()) {
        auto urls = repo->get_config().get_baseurl_option().get_value();
        if (!urls.empty()) {
            repo_base = urls.front();
        } else if (auto mirrors = repo->get_mirrors(); !mirrors.empty()) {
            repo_base = mirrors.front();
        }
    }
    const bool pkg_base_absolute = pkg_base.find("://") != std::string::npos;
    std::string base;
    if (pkg_base_absolute || repo_base.empty()) {
        base = pkg_base;
    } else {
        base = pkg_base.empty() ? repo_base : join_url(repo_base, pkg_base);
    }
    return base.empty() ? location : join_url(base, location);
}

struct Providers {
    NameSet names;
    NameSet nevras;
};

// The packages that provide any of `deps`, other than `self`, when they are part of the lock (`locked`, by id) or of a
// layer below (`installed`, by name).
Providers providers_of(
    libdnf5::Base & base,
    const libdnf5::rpm::ReldepList & deps,
    const std::string & self,
    const std::set<libdnf5::rpm::PackageId> & locked,
    const std::map<std::string, std::string> & installed) {
    Providers result;
    PackageQuery query(base);
    query.filter_provides(deps);
    for (const auto & prov : query) {
        auto name = prov.get_name();
        if ((!locked.count(prov.get_id()) && !installed.count(name)) || name == self) {
            continue;
        }
        result.names.insert(name);
        result.nevras.insert(prov.get_nevra());
    }
    return result;
}

std::string problems_message(libdnf5::base::Transaction & transaction) {
    std::string message = "Error resolving package dependencies:";
    for (const auto & problem : transaction.get_resolve_logs_as_strings()) {
        message += "\n" + problem;
    }
    return message;
}

json_object * json_strings(const NameSet & items) {
    json_object * array = json_object_new_array();
    for (const auto & item : items) {
        json_object_array_add(array, json_object_new_string(item.c_str()));
    }
    return array;
}

// The dependency strings of `deps`, as pool_dep2str writes them; nothing when there are none.
void add_dep_strings(json_object * pkg_obj, const char * key, const libdnf5::rpm::ReldepList & deps) {
    if (deps.size() == 0) {
        return;
    }
    json_object * array = json_object_new_array();
    for (int i = 0; i < deps.size(); ++i) {
        json_object_array_add(array, json_object_new_string(deps.get(i).to_string().c_str()));
    }
    json_object_object_add(pkg_obj, key, array);
}

}  // namespace

void LockCommand::set_parent_command() {
    auto * arg_parser_parent_cmd = get_session().get_argument_parser().get_root_command();
    auto * arg_parser_this_cmd = get_argument_parser_command();
    arg_parser_parent_cmd->register_command(arg_parser_this_cmd);
}

void LockCommand::set_argument_parser() {
    auto & parser = get_context().get_argument_parser();
    auto & cmd = *get_argument_parser_command();
    cmd.set_description(_("Resolve packages and their dependencies into a JSON lockfile"));

    auto specs_arg = parser.add_new_positional_arg("package-spec", ArgumentParser::PositionalArg::UNLIMITED, nullptr, nullptr);
    specs_arg->set_description(_("Packages to lock"));
    specs_arg->set_parse_hook_func([this]([[maybe_unused]] ArgumentParser::PositionalArg * arg, int argc, const char * const argv[]) {
        for (int i = 0; i < argc; ++i) {
            specs.emplace_back(argv[i]);
        }
        return true;
    });
    cmd.register_positional_arg(specs_arg);

    add_value_option(parser, cmd, "output", 'o', "FILE", _("Write the lockfile to FILE instead of standard output"), [this](const char * v) { output = v; });
    add_value_option(parser, cmd, "archlist", 0, "ARCH,...", _("Architectures a locked package may have"), [this](const char * v) { append_list(archlist, v); });
    add_value_option(parser, cmd, "prune", 0, "PACKAGE,...", _("Leave PACKAGE and what only it pulls in weakly out of the lockfile"), [this](const char * v) { append_list(prune, v); });
    add_value_option(parser, cmd, "module-enable", 0, "NAME:STREAM,...", _("Enable these module streams before resolving"), [this](const char * v) { append_list(module_enable, v); });
    add_value_option(parser, cmd, "lockfile", 'l', "FILE,...", _("Lockfiles of the layers below this one, in order"), [this](const char * v) { append_list(lockfiles, v); });
}

void LockCommand::configure() {
    // The installed set comes from the lockfiles of the layers below, not from an rpmdb.
    get_context().set_load_system_repo(false);
    get_context().set_load_available_repos(Context::LoadAvailableRepos::ENABLED);
}

void LockCommand::load_additional_packages() {}

void LockCommand::run() {
    if (specs.empty()) {
        throw libdnf5::cli::CommandExitError(1, M_("No packages specified for lockfile generation"));
    }
    auto & base = get_context().get_base();
    const bool best = base.get_config().get_best_option().get_value();
    const bool weak_deps = base.get_config().get_install_weak_deps_option().get_value();

    // Enabling a stream takes a resolve of its own: it filters the packages of the other streams out of the queries below.
    if (!module_enable.empty()) {
        libdnf5::Goal module_goal(base);
        for (const auto & spec : module_enable) {
            module_goal.add_module_enable(spec, libdnf5::GoalJobSettings());
        }
        auto module_transaction = module_goal.resolve();
        if (module_transaction.get_problems() != libdnf5::GoalProblem::NO_PROBLEM) {
            throw libdnf5::cli::CommandExitError(1, M_("{}"), problems_message(module_transaction));
        }
        base.get_repo_sack()->reapply_module_filtering();
    }

    // What the layers below hold.
    const auto installonly = base.get_config().get_installonlypkgs_option().get_value();
    Layers layers;
    for (const auto & lockfile : lockfiles) {
        load_lockfile(lockfile, installonly, layers);
    }
    // The architectures of the first layer that names some, unless --archlist says otherwise.
    std::string arch_text;
    for (const auto & arch : archlist) {
        arch_text += (arch_text.empty() ? "" : ",") + arch;
    }
    if (archlist.empty() && layers.arch && !layers.arch->empty()) {
        arch_text = *layers.arch;
        std::replace(arch_text.begin(), arch_text.end(), ' ', ',');
        append_list(archlist, arch_text.c_str());
        arch_text = *layers.arch;
    }
    if (!layers.order.empty()) {
        load_system_repo(base, layers, archlist);
    }
    std::map<std::string, std::string> installed;
    for (const auto & [name, pkg] : layers.by_name) {
        installed[name] = pkg->nevra.value_or("");
    }

    libdnf5::ResolveSpecSettings spec_settings;
    spec_settings.set_with_nevra(true);
    spec_settings.set_with_provides(true);
    spec_settings.set_with_filenames(true);
    spec_settings.set_with_binaries(false);
    spec_settings.set_expand_globs(true);
    spec_settings.set_ignore_case(false);

    // The candidates of a spec: the newest per name.arch with best, every version otherwise (for --no-best).
    auto candidates_of = [&](const std::string & spec, bool latest) {
        PackageQuery query(base);
        query.resolve_pkg_spec(spec, spec_settings, false);
        query.filter_available();
        if (latest) {
            query.filter_priority();
            query.filter_latest_evr();
        }
        if (!archlist.empty()) {
            query.filter_arch(archlist);
        }
        return query;
    };

    std::vector<Package> candidates;
    for (const auto & spec : specs) {
        for (const auto & pkg : candidates_of(spec, best)) {
            candidates.push_back(pkg);
        }
    }
    std::sort(candidates.begin(), candidates.end(), [](const Package & a, const Package & b) { return a.get_id() < b.get_id(); });
    candidates.erase(std::unique(candidates.begin(), candidates.end()), candidates.end());
    auto targets = pick_one_per_name_arch(candidates);

    NameSet directly_requested(specs.begin(), specs.end());
    for (const auto & pkg : targets) {
        directly_requested.insert(pkg.get_name());
    }

    const bool all_requested_installed = std::all_of(
        directly_requested.begin(), directly_requested.end(), [&](const auto & name) { return installed.count(name) > 0; });
    if (targets.empty() && !all_requested_installed) {
        throw libdnf5::cli::CommandExitError(1, M_("No packages matched the specified criteria"));
    }

    // A package of a layer below that the repositories do not replace with a newer or another build.
    auto held = [&](const Package & pkg) {
        auto inst = layers.find(pkg, installonly);
        return inst && !integrity_differs(inst->integrity, pkg);
    };

    std::vector<Package> all_pkgs;
    std::vector<Package> obsoleted;
    if (!targets.empty()) {
        libdnf5::Goal goal(base);
        if (best) {
            for (const auto & pkg : targets) {
                // dnf leaves an installed package alone unless the repositories hold a newer build: an install job for
                // an older or identical copy of it would count as a fresh install and pull in that copy's weak
                // dependencies.
                auto inst = layers.find(pkg, installonly);
                if (inst && inst->version) {
                    int cmp = evrcmp_str(*inst->version, pkg);
                    if (cmp > 0 || (cmp == 0 && !integrity_differs(inst->integrity, pkg))) {
                        continue;
                    }
                }
                goal.add_rpm_install(pkg);
            }
        } else {
            // Let the solver pick among all candidates of each request, preferring the newest.
            for (const auto & spec : specs) {
                PackageSet all(base);
                for (const auto & pkg : candidates_of(spec, false)) {
                    all.add(pkg);
                }
                if (!all.empty()) {
                    goal.add_rpm_install(all);
                }
            }
        }
        auto transaction = goal.resolve();
        if (transaction.get_problems() != libdnf5::GoalProblem::NO_PROBLEM) {
            throw libdnf5::cli::CommandExitError(1, M_("{}"), problems_message(transaction));
        }
        using Action = libdnf5::transaction::TransactionItemAction;
        // Only packages of a layer below can be obsoleted: a package the solve replaces with one of another name.
        for (const auto & tp : transaction.get_transaction_packages()) {
            if (tp.get_action() != Action::REPLACED) {
                continue;
            }
            const auto replaced = tp.get_package();
            for (const auto & replacer : tp.get_replaced_by()) {
                if (replacer.get_name() != replaced.get_name()) {
                    obsoleted.push_back(replaced);
                    break;
                }
            }
        }
        std::sort(obsoleted.begin(), obsoleted.end(), [](const Package & a, const Package & b) { return a.get_id() < b.get_id(); });
        for (const auto & pkg : obsoleted) {
            std::fprintf(stderr, "warning: the solve obsoletes %s from a layer below\n", pkg.get_nevra().c_str());
        }

        // Targets and installs only when new to the layers below or a different build. With --nobest the solver may pick
        // another version than the targets: the installs list it.
        if (best) {
            for (const auto & pkg : targets) {
                if (!held(pkg)) {
                    all_pkgs.push_back(pkg);
                }
            }
        }
        for (const auto & tp : transaction.get_transaction_packages()) {
            if (tp.get_action() == Action::INSTALL && !held(tp.get_package())) {
                all_pkgs.push_back(tp.get_package());
            }
        }
        for (const auto & tp : transaction.get_transaction_packages()) {
            if (tp.get_action() == Action::UPGRADE) {
                all_pkgs.push_back(tp.get_package());
            }
        }
    }

    auto resolved = one_per_nevra(all_pkgs);
    std::set<libdnf5::rpm::PackageId> locked;
    NameSet locked_names;
    for (const auto & pkg : resolved) {
        locked.insert(pkg.get_id());
        locked_names.insert(pkg.get_name());
    }

    // Without --prune every package the solver installs is kept, as dnf installs it. With it, what is only reachable
    // through a pruned package is trimmed.
    const bool trim = !prune.empty();
    NameSet pruned;
    for (const auto & name : prune) {
        if (!directly_requested.count(name)) {
            pruned.insert(name);
        }
    }

    std::map<std::string, Providers> direct;
    // Weak edges (Recommends, reverse Supplements), only followed to find what to trim; they stay out of "dependencies"
    // as the package does not require them.
    std::map<std::string, NameSet> weak;
    for (const auto & pkg : resolved) {
        auto name = pkg.get_name();
        direct[name] = providers_of(base, pkg.get_requires(), name, locked, installed);
        if (trim && weak_deps) {
            for (const auto & prov : providers_of(base, pkg.get_recommends(), name, locked, {}).names) {
                weak[name].insert(prov);
            }
            for (const auto & prov : providers_of(base, pkg.get_supplements(), name, locked, {}).names) {
                weak[prov].insert(name);
            }
        }
    }

    NameSet reachable;
    std::deque<std::string> queue;
    auto reach = [&](const std::string & name) {
        if (reachable.insert(name).second) {
            queue.push_back(name);
        }
    };
    bool any_requested = false;
    for (const auto & name : directly_requested) {
        if (locked_names.count(name)) {
            reach(name);
            any_requested = true;
        }
    }
    // Without a pruned package, or with every request already in a layer below, all new packages are kept.
    if (!trim || !any_requested) {
        for (const auto & name : locked_names) {
            if (!pruned.count(name)) {
                reach(name);
            }
        }
    }
    while (!queue.empty()) {
        auto current = queue.front();
        queue.pop_front();
        for (const auto * edges : {&direct[current].names, &weak[current]}) {
            for (const auto & dep : *edges) {
                if (!pruned.count(dep) && locked_names.count(dep)) {
                    reach(dep);
                }
            }
        }
    }

    json_object * root = json_object_new_object();
    json_object_object_add(root, "version", json_object_new_int(1));
    if (!arch_text.empty()) {
        json_object_object_add(root, "arch", json_object_new_string(arch_text.c_str()));
    }
    json_object_object_add(root, "requested", json_strings(directly_requested));
    if (!obsoleted.empty()) {
        json_object * array = json_object_new_array();
        for (const auto & pkg : obsoleted) {
            json_object * obj = json_object_new_object();
            json_object_object_add(obj, "name", json_object_new_string(pkg.get_name().c_str()));
            json_object_object_add(obj, "version", json_object_new_string(pkg.get_evr().c_str()));
            json_object_object_add(obj, "arch", json_object_new_string(pkg.get_arch().c_str()));
            json_object_object_add(obj, "nevra", json_object_new_string(pkg.get_nevra().c_str()));
            json_object_array_add(array, obj);
        }
        json_object_object_add(root, "obsoleted", array);
    }

    json_object * packages = json_object_new_array();
    for (const auto & pkg : resolved) {
        auto name = pkg.get_name();
        if (!reachable.count(name)) {
            continue;
        }
        std::string hex;
        auto integrity = integrity_of(pkg, hex);
        json_object * obj = json_object_new_object();
        json_object_object_add(obj, "name", json_object_new_string(name.c_str()));
        json_object_object_add(obj, "version", json_object_new_string(pkg.get_evr().c_str()));
        json_object_object_add(obj, "arch", json_object_new_string(pkg.get_arch().c_str()));
        json_object_object_add(obj, "nevra", json_object_new_string(pkg.get_nevra().c_str()));
        json_object_object_add(obj, "url", json_object_new_string(package_url(pkg).c_str()));
        json_object_object_add(obj, "integrity", json_object_new_string(integrity.c_str()));
        if (!hex.empty()) {
            json_object_object_add(obj, "checksum", json_object_new_string(hex.c_str()));
        }
        json_object_object_add(obj, "requested", json_object_new_boolean(directly_requested.count(name)));

        auto kept = [&](const std::string & dep) { return reachable.count(dep) || installed.count(dep); };
        NameSet deps;
        for (const auto & dep : direct[name].names) {
            if (kept(dep)) {
                deps.insert(dep);
            }
        }
        json_object_object_add(obj, "dependencies", json_strings(deps));
        json_object_object_add(obj, "dependencies_nevra", json_strings(direct[name].nevras));
        // So that a later lock on top of this one knows what the package left unmet.
        add_dep_strings(obj, "recommends", pkg.get_recommends());
        add_dep_strings(obj, "supplements", pkg.get_supplements());

        NameSet transitive;
        std::deque<std::string> walk{name};
        while (!walk.empty()) {
            auto current = walk.front();
            walk.pop_front();
            for (const auto & dep : direct[current].names) {
                if (dep != name && kept(dep) && transitive.insert(dep).second) {
                    walk.push_back(dep);
                }
            }
        }
        json_object_object_add(obj, "transitive_dependencies", json_strings(transitive));
        json_object_array_add(packages, obj);
    }
    json_object_object_add(root, "packages", packages);

    std::string document = std::string(json_object_to_json_string_ext(root, JSON_C_TO_STRING_PRETTY)) + "\n";
    json_object_put(root);
    if (output.empty() || output == "-") {
        std::fwrite(document.data(), 1, document.size(), stdout);
        return;
    }
    std::FILE * file = std::fopen(output.c_str(), "wb");
    if (!file || std::fwrite(document.data(), 1, document.size(), file) != document.size() || std::fclose(file) != 0) {
        throw libdnf5::cli::CommandExitError(1, M_("Cannot write {}"), output);
    }
}

}  // namespace dnf5
