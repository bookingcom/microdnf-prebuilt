// Registers the lock command with dnf5.

#include "command_plugin.hpp"
#include "lock.hpp"

DNF5_COMMAND_PLUGIN(dnf5::LockCommand, "lock", "lock command: resolves packages into a JSON lockfile.")
