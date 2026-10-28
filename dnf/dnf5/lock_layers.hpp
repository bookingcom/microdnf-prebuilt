#ifndef DNF_DNF5_LOCK_LAYERS_HPP
#define DNF_DNF5_LOCK_LAYERS_HPP

#include <libdnf5/base/base.hpp>
#include <libdnf5/rpm/package.hpp>

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace dnf5 {

/// A package a lockfile of a layer below records.
struct LayerPackage {
    std::string name;
    std::optional<std::string> version;
    std::optional<std::string> arch;
    std::optional<std::string> nevra;
    std::optional<std::string> integrity;
    std::vector<std::string> dependencies;
    std::vector<std::string> recommends;
    std::vector<std::string> supplements;
};

/// What the layers below hold, merged in order: a later layer overrides an earlier one's package with the same key.
struct Layers {
    /// In the order of the system repo.
    std::vector<std::shared_ptr<LayerPackage>> order;
    /// By name.arch, or by NEVRA for an installonly package, whose versions are installed side by side.
    std::map<std::string, std::shared_ptr<LayerPackage>> by_key;
    /// By name: the latest layer's package.
    std::map<std::string, std::shared_ptr<LayerPackage>> by_name;
    /// The "arch" of the first lockfile that has one.
    std::optional<std::string> arch;

    /// The layer's copy of a package of the repositories, if it holds one.
    std::shared_ptr<LayerPackage> find(const libdnf5::rpm::Package & pkg, const std::vector<std::string> & installonly) const;
};

/// The package's checksum as the lockfile's "integrity" (algorithm-base64) and, in `hex`, its "checksum".
std::string integrity_of(const libdnf5::rpm::Package & pkg, std::string & hex);

/// Compares an EVR string with the package's EVR, as rpm does.
int evrcmp_str(const std::string & evr, const libdnf5::rpm::Package & pkg);

/// Adds the lockfile at `path` to `layers`. Throws a CommandExitError when it cannot be read or is malformed.
void load_lockfile(const std::string & path, const std::vector<std::string> & installonly, Layers & layers);

/// True if the layer's recorded integrity says `pkg` is a different build. A missing or empty integrity on either side
/// says nothing, so it counts as the same build.
bool integrity_differs(const std::optional<std::string> & layer_integrity, const libdnf5::rpm::Package & pkg);

/// Fills the system repo with the packages of `layers`, as if installed: a package the repositories hold takes their
/// metadata, one they lack becomes a synthetic package from the lockfile. A package whose integrity differs is left out
/// so that the solver fetches the repositories' build.
void load_system_repo(libdnf5::Base & base, const Layers & layers, const std::vector<std::string> & archlist);

}  // namespace dnf5

#endif  // DNF_DNF5_LOCK_LAYERS_HPP
