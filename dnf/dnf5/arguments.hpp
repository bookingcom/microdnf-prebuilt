// Command line helpers shared by our dnf5 commands.

#pragma once

#include <libdnf5-cli/argument_parser.hpp>

#include <sstream>
#include <string>
#include <vector>

namespace dnf5 {

// A comma separated list, appended to `out`: --archlist=x86_64,noarch --archlist=i686.
inline void append_list(std::vector<std::string> & out, const char * value) {
    std::istringstream stream(value);
    for (std::string item; std::getline(stream, item, ',');) {
        if (!item.empty()) {
            out.push_back(item);
        }
    }
}

// Registers the option --NAME, also -SHORT_NAME unless that is 0, which takes a value; `store` receives it.
template <typename Store>
void add_value_option(
    libdnf5::cli::ArgumentParser & parser,
    libdnf5::cli::ArgumentParser::Command & cmd,
    const char * name,
    char short_name,
    const char * value_help,
    const char * description,
    Store store) {
    auto * arg = parser.add_new_named_arg(name);
    arg->set_long_name(name);
    if (short_name) {
        arg->set_short_name(short_name);
    }
    arg->set_has_value(true);
    arg->set_arg_value_help(value_help);
    arg->set_description(description);
    arg->set_parse_hook_func([store]([[maybe_unused]] libdnf5::cli::ArgumentParser::NamedArg * arg,
                                     [[maybe_unused]] const char * option,
                                     const char * value) {
        store(value);
        return true;
    });
    cmd.register_named_arg(arg);
}

}  // namespace dnf5
