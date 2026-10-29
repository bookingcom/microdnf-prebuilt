#include "lock.hpp"

#include "arguments.hpp"

#include <libdnf5-cli/argument_parser.hpp>
#include <libdnf5-cli/exception.hpp>
#include <libdnf5/utils/bgettext/bgettext-lib.h>
#include <libdnf5/utils/bgettext/bgettext-mark-domain.h>

namespace dnf5 {

using libdnf5::cli::ArgumentParser;

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
}

void LockCommand::load_additional_packages() {}

void LockCommand::run() {
    if (specs.empty()) {
        throw libdnf5::cli::CommandExitError(1, M_("No packages specified for lockfile generation"));
    }
    throw libdnf5::cli::CommandExitError(1, M_("lock is not implemented yet"));
}

}  // namespace dnf5
