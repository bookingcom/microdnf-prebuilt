// What a dnf5 command plugin of ours shares: an IPlugin adding one command, and the extern "C" entry points. The
// plugins are linked into the binary, so BUILD.bazel renames those entry points per plugin (see entry_points), as it
// does for the upstream command plugins.

#pragma once

#include <dnf5/iplugin.hpp>

#include <cstring>
#include <exception>
#include <memory>
#include <vector>

namespace dnf5 {

// A plugin whose one command is `Cmd`. `name` and `description` must outlive it: pass string literals.
template <typename Cmd>
class CommandPlugin : public IPlugin {
public:
    static constexpr PluginAPIVersion API_VERSION{.major = 2, .minor = 0};
    static constexpr PluginVersion VERSION{.major = 1, .minor = 0, .micro = 0};

    CommandPlugin(Context & context, const char * name, const char * description)
        : IPlugin(context), name(name), description(description) {}

    PluginAPIVersion get_api_version() const noexcept override { return API_VERSION; }
    const char * get_name() const noexcept override { return name; }
    PluginVersion get_version() const noexcept override { return VERSION; }
    const char * const * get_attributes() const noexcept override { return ATTRS; }

    const char * get_attribute(const char * attribute) const noexcept override {
        return std::strcmp(attribute, "description") == 0 ? description : nullptr;
    }

    std::vector<std::unique_ptr<Command>> create_commands() override {
        std::vector<std::unique_ptr<Command>> commands;
        commands.push_back(std::make_unique<Cmd>(get_context()));
        return commands;
    }

    void finish() noexcept override {}

private:
    static constexpr const char * ATTRS[]{"description", nullptr};

    const char * name;
    const char * description;
};

}  // namespace dnf5

// Defines the entry points of the plugin CommandPlugin<CMD>. Use once per plugin, at namespace scope.
#define DNF5_COMMAND_PLUGIN(CMD, NAME, DESCRIPTION)                                                              \
    namespace {                                                                                                  \
    std::exception_ptr last_exception;                                                                           \
    }                                                                                                            \
    dnf5::PluginAPIVersion dnf5_plugin_get_api_version(void) { return dnf5::CommandPlugin<CMD>::API_VERSION; } \
    const char * dnf5_plugin_get_name(void) { return NAME; }                                                     \
    dnf5::PluginVersion dnf5_plugin_get_version(void) { return dnf5::CommandPlugin<CMD>::VERSION; }            \
    dnf5::IPlugin * dnf5_plugin_new_instance(                                                                    \
        [[maybe_unused]] dnf5::ApplicationVersion application_version, dnf5::Context & context) try {            \
        return new dnf5::CommandPlugin<CMD>(context, NAME, DESCRIPTION);                                         \
    } catch (...) {                                                                                              \
        last_exception = std::current_exception();                                                               \
        return nullptr;                                                                                          \
    }                                                                                                            \
    void dnf5_plugin_delete_instance(dnf5::IPlugin * plugin_object) { delete plugin_object; }                    \
    std::exception_ptr * dnf5_plugin_get_last_exception(void) { return &last_exception; }
