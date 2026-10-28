#include "lock_layers.hpp"

#include <glib.h>
#include <json-c/json.h>
#include <libdnf5-cli/exception.hpp>
#include <libdnf5/base/base.hpp>
#include <libdnf5/rpm/nevra.hpp>
#include <libdnf5/rpm/package_query.hpp>
#include <libdnf5/utils/bgettext/bgettext-lib.h>
#include <libdnf5/utils/bgettext/bgettext-mark-domain.h>

#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <fstream>
#include <sstream>
#include <unistd.h>

namespace dnf5 {

using libdnf5::rpm::Package;
using libdnf5::rpm::PackageQuery;

namespace {

[[noreturn]] void fail(const std::string & message) {
    throw libdnf5::cli::CommandExitError(1, M_("{}"), message);
}

std::vector<std::string> string_array(json_object * obj, const char * key) {
    std::vector<std::string> out;
    json_object * array = nullptr;
    if (!json_object_object_get_ex(obj, key, &array) || !json_object_is_type(array, json_type_array)) {
        return out;
    }
    for (size_t i = 0; i < json_object_array_length(array); ++i) {
        if (auto * item = json_object_array_get_idx(array, i)) {
            out.emplace_back(json_object_get_string(item));
        }
    }
    return out;
}

// A string member, or nothing when it is absent or null.
std::optional<std::string> string_member(const std::string & path, size_t index, json_object * obj, const char * key) {
    json_object * value = nullptr;
    if (!json_object_object_get_ex(obj, key, &value) || json_object_is_type(value, json_type_null)) {
        return std::nullopt;
    }
    if (!json_object_is_type(value, json_type_string)) {
        fail("Lockfile '" + path + "': package " + std::to_string(index) + " has a non-string " + key);
    }
    return json_object_get_string(value);
}

std::string key_of(const std::string & name, const std::optional<std::string> & arch, const std::optional<std::string> & nevra, const std::vector<std::string> & installonly) {
    if (nevra && std::find(installonly.begin(), installonly.end(), name) != installonly.end()) {
        return *nevra;
    }
    return name + "." + arch.value_or("");
}

// What a lockfile's version string holds, for comparing it with a package's.
struct Evr {
    std::string epoch, version, release;
    const std::string & get_epoch() const { return epoch; }
    const std::string & get_version() const { return version; }
    const std::string & get_release() const { return release; }
};

}  // namespace

std::shared_ptr<LayerPackage> Layers::find(const Package & pkg, const std::vector<std::string> & installonly) const {
    auto it = by_key.find(key_of(pkg.get_name(), pkg.get_arch(), pkg.get_nevra(), installonly));
    return it == by_key.end() ? nullptr : it->second;
}

int evrcmp_str(const std::string & evr, const Package & pkg) {
    Evr parsed;
    auto colon = evr.find(':');
    parsed.epoch = colon == std::string::npos ? "" : evr.substr(0, colon);
    auto rest = colon == std::string::npos ? evr : evr.substr(colon + 1);
    auto dash = rest.rfind('-');
    parsed.version = dash == std::string::npos ? rest : rest.substr(0, dash);
    parsed.release = dash == std::string::npos ? "" : rest.substr(dash + 1);
    return libdnf5::rpm::evrcmp(parsed, pkg);
}

void load_lockfile(const std::string & path, const std::vector<std::string> & installonly, Layers & layers) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        fail("Failed to open file '" + path + "': " + std::strerror(errno));
    }
    std::stringstream buffer;
    buffer << file.rdbuf();
    auto content = buffer.str();

    json_tokener * tokener = json_tokener_new();
    json_object * root = json_tokener_parse_ex(tokener, content.data(), static_cast<int>(content.size()));
    const bool parsed = json_tokener_get_error(tokener) == json_tokener_success;
    json_tokener_free(tokener);
    if (!parsed || !root || !json_object_is_type(root, json_type_object)) {
        if (root) {
            json_object_put(root);
        }
        fail("Failed to parse lockfile JSON at '" + path + "'");
    }
    // Releases the document on every way out, including the exceptions of a malformed package.
    std::unique_ptr<json_object, decltype(&json_object_put)> guard(root, json_object_put);

    json_object * arch = nullptr;
    if (json_object_object_get_ex(root, "arch", &arch) && !layers.arch) {
        layers.arch = json_object_get_string(arch);
    }

    json_object * packages = nullptr;
    if (!json_object_object_get_ex(root, "packages", &packages) || !json_object_is_type(packages, json_type_array)) {
        return;
    }
    for (size_t i = 0; i < json_object_array_length(packages); ++i) {
        json_object * obj = json_object_array_get_idx(packages, i);
        if (!obj || !json_object_is_type(obj, json_type_object)) {
            fail("Lockfile '" + path + "': package " + std::to_string(i) + " is not an object");
        }
        auto pkg = std::make_shared<LayerPackage>();
        auto name = string_member(path, i, obj, "name");
        pkg->version = string_member(path, i, obj, "version");
        pkg->arch = string_member(path, i, obj, "arch");
        pkg->nevra = string_member(path, i, obj, "nevra");
        string_member(path, i, obj, "url");
        pkg->integrity = string_member(path, i, obj, "integrity");
        string_member(path, i, obj, "checksum");
        if (!name || name->empty()) {
            fail("Lockfile '" + path + "': package " + std::to_string(i) + " has no name");
        }
        pkg->name = *name;
        pkg->dependencies = string_array(obj, "dependencies");
        pkg->recommends = string_array(obj, "recommends");
        pkg->supplements = string_array(obj, "supplements");

        auto key = key_of(pkg->name, pkg->arch, pkg->nevra, installonly);
        if (auto old = layers.by_key.find(key); old != layers.by_key.end()) {
            layers.order.erase(std::remove(layers.order.begin(), layers.order.end(), old->second), layers.order.end());
        }
        layers.by_name[pkg->name] = pkg;
        layers.by_key[key] = pkg;
        layers.order.push_back(pkg);
    }
}

// The package's checksum as the lockfile's "integrity" (algorithm-base64) and, in `hex`, its "checksum".
std::string integrity_of(const Package & pkg, std::string & hex) {
    auto checksum = pkg.get_checksum();
    hex = checksum.get_checksum();
    if (hex.empty()) {
        return "";
    }
    std::string raw;
    for (size_t i = 0; i + 1 < hex.size(); i += 2) {
        raw.push_back(static_cast<char>(std::stoi(hex.substr(i, 2), nullptr, 16)));
    }
    gchar * b64 = g_base64_encode(reinterpret_cast<const guchar *>(raw.data()), raw.size());
    std::string integrity = checksum.get_type_str() + "-" + b64;
    g_free(b64);
    return integrity;
}

bool integrity_differs(const std::optional<std::string> & layer_integrity, const Package & pkg) {
    if (!layer_integrity || layer_integrity->empty()) {
        return false;
    }
    std::string hex;
    auto integrity = integrity_of(pkg, hex);
    return !integrity.empty() && integrity != *layer_integrity;
}

namespace {

void add_deps(std::ostream & out, const char * tag, const libdnf5::rpm::ReldepList & deps) {
    for (int i = 0; i < deps.size(); ++i) {
        out << tag << ' ' << deps.get(i).to_string() << '\n';
    }
}

// A dependency string as pool_dep2str writes it: a rich dependency in parentheses, or "name" / "name <op> evr".
// False when it is malformed.
bool valid_dep(const std::string & dep) {
    if (dep.empty()) {
        return false;
    }
    if (dep.front() == '(') {
        return true;
    }
    std::istringstream words(dep);
    std::string name, op, evr, extra;
    words >> name >> op >> evr;
    if (name.empty()) {
        return false;
    }
    if (op.empty()) {
        return true;
    }
    return op.find_first_not_of("<=>") == std::string::npos && !evr.empty();
}

void add_dep_strings(std::ostream & out, const char * tag, const std::vector<std::string> & deps) {
    for (const auto & dep : deps) {
        if (valid_dep(dep)) {
            out << tag << ' ' << dep << '\n';
        }
    }
}

void add_header(std::ostream & out, const std::string & name, const std::string & evr, const std::string & arch) {
    auto dash = evr.rfind('-');
    out << "=Pkg: " << name << ' ' << (dash == std::string::npos ? evr : evr.substr(0, dash)) << ' '
        << (dash == std::string::npos ? "-" : evr.substr(dash + 1)) << ' ' << arch << '\n';
}

// The layer holds the package at a NEVRA the repositories lack, so its own provides are unknown. Gives the synthetic
// package those of the repositories' newest build of the name: sonames and file paths rarely change between builds, and
// a package that provides only its name leaves the layer's other packages, whose requires come from the repositories,
// unsatisfied. The build's provides on the name itself (name, name = evr, name(arch) = evr) are its own and are skipped.
void add_provides_of_name(std::ostream & out, libdnf5::Base & base, const LayerPackage & inst, const std::vector<std::string> & archlist) {
    PackageQuery query(base);
    query.filter_name(inst.name);
    if (inst.arch) {
        query.filter_arch(*inst.arch);
    } else if (!archlist.empty()) {
        query.filter_arch(archlist);
    }
    query.filter_latest_evr();
    if (query.empty()) {
        return;
    }
    const auto provides = (*query.begin()).get_provides();
    for (int i = 0; i < provides.size(); ++i) {
        auto dep = provides.get(i).to_string();
        auto base_name = dep.substr(0, dep.find(' '));
        if (base_name.compare(0, inst.name.size(), inst.name) == 0 &&
            (base_name.size() == inst.name.size() || base_name[inst.name.size()] == '(')) {
            continue;
        }
        out << "=Prv: " << dep << '\n';
    }
}

}  // namespace

void load_system_repo(libdnf5::Base & base, const Layers & layers, const std::vector<std::string> & archlist) {
    std::ostringstream out;
    for (const auto & inst : layers.order) {
        PackageQuery query(base);
        query.filter_name(inst->name);
        if (!archlist.empty()) {
            query.filter_arch(archlist);
        }
        std::optional<Package> found;
        for (const auto & pkg : query) {
            if (inst->nevra ? pkg.get_nevra() == *inst->nevra : !inst->version || pkg.get_evr() == *inst->version) {
                found = pkg;
                break;
            }
        }

        if (found) {
            // The repositories' build is another one: leave the package out so that the solver fetches it.
            if (integrity_differs(inst->integrity, *found)) {
                continue;
            }
            add_header(out, found->get_name(), found->get_evr(), found->get_arch());
            if (auto vendor = found->get_vendor(); !vendor.empty()) {
                out << "=Vnd: " << vendor << '\n';
            }
            add_deps(out, "=Prv:", found->get_provides());
            add_deps(out, "=Req:", found->get_requires());
            add_deps(out, "=Con:", found->get_conflicts());
            add_deps(out, "=Obs:", found->get_obsoletes());
            // dnf leaves out the providers of Recommends an installed package left unmet.
            add_deps(out, "=Rec:", found->get_recommends());
            add_deps(out, "=Sup:", found->get_supplements());
        } else {
            add_header(out, inst->name, inst->version.value_or("0"), inst->arch.value_or("noarch"));
            out << "=Prv: " << inst->name << " = " << inst->version.value_or("0") << '\n';
            out << "=Prv: " << inst->name << '\n';
            add_provides_of_name(out, base, *inst, archlist);
            for (const auto & dep : inst->dependencies) {
                out << "=Req: " << dep << '\n';
            }
            // Recorded by the lock that put the package in the layer.
            add_dep_strings(out, "=Rec:", inst->recommends);
            add_dep_strings(out, "=Sup:", inst->supplements);
        }
    }

    auto path = base.get_config().get_cachedir_option().get_value() + "/lock-layers-XXXXXX";
    int fd = mkstemp(path.data());
    if (fd < 0) {
        fail("Cannot create " + path);
    }
    auto text = out.str();
    const bool written = write(fd, text.data(), text.size()) == static_cast<ssize_t>(text.size());
    close(fd);
    if (!written) {
        unlink(path.c_str());
        fail("Cannot write " + path);
    }
    base.get_repo_sack()->create_system_repo_from_libsolv_testcase(path);
    unlink(path.c_str());
}

}  // namespace dnf5
