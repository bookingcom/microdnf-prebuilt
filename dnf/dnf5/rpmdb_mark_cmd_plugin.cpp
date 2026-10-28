// Registers the rpmdb-mark command with dnf5. Linked into the binary: BUILD.bazel renames the extern "C" entry points to
// rpmdb_mark_dnf5_plugin_*, as it does for the upstream command plugins.

#include "rpmdb_mark.hpp"

#include <dnf5/iplugin.hpp>

#include <cstring>

using namespace dnf5;

namespace {

constexpr const char * PLUGIN_NAME{"rpmdb-mark"};
constexpr PluginVersion PLUGIN_VERSION{.major = 1, .minor = 0, .micro = 0};
constexpr PluginAPIVersion REQUIRED_PLUGIN_API_VERSION{.major = 2, .minor = 0};

constexpr const char * attrs[]{"description", nullptr};

class RpmdbMarkCmdPlugin : public IPlugin {
public:
    using IPlugin::IPlugin;

    PluginAPIVersion get_api_version() const noexcept override { return REQUIRED_PLUGIN_API_VERSION; }
    const char * get_name() const noexcept override { return PLUGIN_NAME; }
    PluginVersion get_version() const noexcept override { return PLUGIN_VERSION; }
    const char * const * get_attributes() const noexcept override { return attrs; }

    const char * get_attribute(const char * attribute) const noexcept override {
        return std::strcmp(attribute, "description") == 0 ? "rpmdb-mark command: records packages in an rpmdb without installing them." : nullptr;
    }

    std::vector<std::unique_ptr<Command>> create_commands() override {
        std::vector<std::unique_ptr<Command>> commands;
        commands.push_back(std::make_unique<RpmdbMarkCommand>(get_context()));
        return commands;
    }

    void finish() noexcept override {}
};

std::exception_ptr last_exception;

}  // namespace

PluginAPIVersion dnf5_plugin_get_api_version(void) { return REQUIRED_PLUGIN_API_VERSION; }
const char * dnf5_plugin_get_name(void) { return PLUGIN_NAME; }
PluginVersion dnf5_plugin_get_version(void) { return PLUGIN_VERSION; }

IPlugin * dnf5_plugin_new_instance([[maybe_unused]] ApplicationVersion application_version, Context & context) try {
    return new RpmdbMarkCmdPlugin(context);
} catch (...) {
    last_exception = std::current_exception();
    return nullptr;
}

void dnf5_plugin_delete_instance(IPlugin * plugin_object) { delete plugin_object; }
std::exception_ptr * dnf5_plugin_get_last_exception(void) { return &last_exception; }
