// The `rpmdb-mark` command of dnf5: records packages in an rpmdb without installing them, as the libdnf-based `mark`
// tool does (dnf/mark.c). It needs no root, no chroot and no package payload; only the headers go into the database.

#ifndef DNF_DNF5_RPMDB_MARK_HPP
#define DNF_DNF5_RPMDB_MARK_HPP

#include <dnf5/context.hpp>

#include <string>
#include <vector>

namespace dnf5 {

class RpmdbMarkCommand : public Command {
public:
    explicit RpmdbMarkCommand(Context & context) : Command(context, "rpmdb-mark") {}
    void set_parent_command() override;
    void set_argument_parser() override;
    void configure() override;
    void load_additional_packages() override;
    void run() override;

private:
    /// Gets the database in dbpath. Not dnf5's --installroot, which stays an empty scratch root.
    std::string rpmdb_root;
    std::string dbpath{"/var/lib/rpm"};
    std::vector<std::string> archlist, specs;
    bool nodeps{false};
};

}  // namespace dnf5

#endif  // DNF_DNF5_RPMDB_MARK_HPP
