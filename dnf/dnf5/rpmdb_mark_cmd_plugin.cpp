// Registers the rpmdb-mark command with dnf5.

#include "command_plugin.hpp"
#include "rpmdb_mark.hpp"

DNF5_COMMAND_PLUGIN(dnf5::RpmdbMarkCommand, "rpmdb-mark", "rpmdb-mark command: records packages in an rpmdb without installing them.")
