#!/usr/bin/env python3
"""Runs dnf_lock --lockfile on small metadata-only repos and checks what it locks on top of the layer below.

Args: <dnf_lock>
"""

import base64
import gzip
import hashlib
import json
import os
import subprocess
import sys
import tempfile

DNF_LOCK = os.path.abspath(sys.argv[1])
TMP = tempfile.mkdtemp()


def pkgid(name, ver, arch):
    return hashlib.sha256(f"{name}-{ver}.{arch}".encode()).hexdigest()


def entries(tag, deps):
    """deps: names, or (name, version) pairs for an EQ relation."""
    if not deps:
        return ""
    xml = "".join(
        f'<rpm:entry name="{d}"/>' if isinstance(d, str)
        else f'<rpm:entry name="{d[0]}" flags="EQ" epoch="0" ver="{d[1]}" rel="1"/>'
        for d in deps)
    return f"<rpm:{tag}>{xml}</rpm:{tag}>"


def write_repo(root, pkgs):
    """pkgs: dicts with name, and optionally ver, arch, provides, requires, obsoletes."""
    xml = []
    for p in pkgs:
        name, ver, arch = p["name"], p.get("ver", "1"), p.get("arch", "noarch")
        deps = (entries("provides", [(name, ver)] + p.get("provides", [])) +
                entries("requires", p.get("requires", [])) + entries("obsoletes", p.get("obsoletes", [])) +
                entries("recommends", p.get("recommends", [])))
        xml.append(f"""<package type="rpm"><name>{name}</name><arch>{arch}</arch>
<version epoch="0" ver="{ver}" rel="1"/><checksum type="sha256" pkgid="YES">{pkgid(name, ver, arch)}</checksum>
<summary>s</summary><description>d</description><packager/><url/><time file="1" build="1"/>
<size package="1" installed="1" archive="1"/><location href="{name}-{ver}-1.{arch}.rpm"/>
<format><rpm:license>MIT</rpm:license><rpm:vendor/><rpm:group>x</rpm:group><rpm:buildhost>h</rpm:buildhost>
<rpm:sourcerpm>{name}-{ver}-1.src.rpm</rpm:sourcerpm><rpm:header-range start="1" end="2"/>{deps}</format></package>""")
    primary = ('<?xml version="1.0" encoding="UTF-8"?>\n<metadata xmlns="http://linux.duke.edu/metadata/common" '
               f'xmlns:rpm="http://linux.duke.edu/metadata/rpm" packages="{len(xml)}">'
               + "".join(xml) + "</metadata>").encode()
    gz = gzip.compress(primary, mtime=0)
    os.makedirs(f"{root}/repo/repodata")
    with open(f"{root}/repo/repodata/primary.xml.gz", "wb") as f:
        f.write(gz)
    with open(f"{root}/repo/repodata/repomd.xml", "w", encoding="utf-8") as f:
        f.write(f"""<?xml version="1.0" encoding="UTF-8"?>
<repomd xmlns="http://linux.duke.edu/metadata/repo"><revision>1</revision><data type="primary">
<checksum type="sha256">{hashlib.sha256(gz).hexdigest()}</checksum>
<open-checksum type="sha256">{hashlib.sha256(primary).hexdigest()}</open-checksum>
<location href="repodata/primary.xml.gz"/><timestamp>1</timestamp><size>{len(gz)}</size>
<open-size>{len(primary)}</open-size></data></repomd>
""")
    os.makedirs(f"{root}/repos.d")
    with open(f"{root}/repos.d/layers.repo", "w", encoding="utf-8") as f:
        f.write(f"[layers]\nname=layers\nbaseurl=file://{root}/repo\ngpgcheck=0\nenabled=1\n")


def locked(name, ver="1", arch="noarch"):
    """A lockfile entry for a package of write_repo, as dnf_lock writes it."""
    integrity = "sha256-" + base64.b64encode(bytes.fromhex(pkgid(name, ver, arch))).decode()
    return {"name": name, "version": f"{ver}-1", "arch": arch, "nevra": f"{name}-{ver}-1.{arch}",
            "integrity": integrity}


def lock(case, repo, below, request, archlist="x86_64,i686,noarch"):
    """Locks request on top of the lockfile entries below; returns (exit code, locked NEVRAs, stderr, obsoleted)."""
    root = f"{TMP}/{case.replace(' ', '-')}"
    write_repo(root, repo)
    with open(f"{root}/below.lock.json", "w", encoding="utf-8") as f:
        json.dump({"version": 1, "arch": archlist, "packages": below}, f)
    r = subprocess.run([DNF_LOCK, f"--reposdir={root}/repos.d", f"--setopt=cachedir={root}/cache",
                        "--releasever=9", f"--archlist={archlist}", f"--lockfile={root}/below.lock.json",
                        "-o", f"{root}/out.json", *request], capture_output=True, text=True, check=False)
    nevras = []
    obsoleted = []
    if r.returncode == 0:
        with open(f"{root}/out.json", encoding="utf-8") as f:
            data = json.load(f)
            nevras = sorted(p["nevra"] for p in data["packages"])
            obsoleted = data.get("obsoleted", [])
    return r.returncode, nevras, r.stderr, obsoleted


def check(case, got, want, stderr):
    if got != want:
        sys.exit(f"FAIL {case}: got {got}, want {want}\n{stderr}")
    print(f"ok   {case}")


CASE = "installonly versions below are all installed"
rc, nevras, err, obsoleted = lock(
    CASE,
    [{"name": "kernel", "ver": "1", "arch": "x86_64"}, {"name": "kernel", "ver": "2", "arch": "x86_64"},
     {"name": "needs-kernel-1", "requires": [("kernel", "1")]}],
    [locked("kernel", "1", "x86_64"), locked("kernel", "2", "x86_64")],
    ["needs-kernel-1"])
check(CASE, (rc, nevras), (0, ["needs-kernel-1-1-1.noarch"]), err)

CASE = "every arch below is installed"
rc, nevras, err, obsoleted = lock(
    CASE,
    [{"name": "foo", "arch": "x86_64"}, {"name": "foo", "arch": "i686", "provides": ["foo-32bit"]},
     {"name": "needs-foo-32bit", "requires": ["foo-32bit"]}],
    [locked("foo", arch="i686"), locked("foo", arch="x86_64")],
    ["needs-foo-32bit"])
check(CASE, (rc, nevras), (0, ["needs-foo-32bit-1-1.noarch"]), err)

CASE = "a later layer still replaces a package of an earlier one"
rc, nevras, err, obsoleted = lock(
    CASE,
    [{"name": "tour", "ver": "1"}, {"name": "tour", "ver": "2"}, {"name": "app", "requires": ["tour"]}],
    [locked("tour", "1"), locked("tour", "2")],
    ["app", "tour"])
check(CASE, (rc, nevras), (0, ["app-1-1.noarch"]), err)

CASE = "obsoleting a package below records it and warns"
rc, nevras, err, obsoleted = lock(CASE, [{"name": "old"}, {"name": "new", "obsoletes": ["old"]}], [locked("old")], ["new"])
check(CASE, (rc, nevras, obsoleted, "the solve obsoletes old-1-1.noarch from a layer below" in err),
      (0, ["new-1-1.noarch"], [{"name": "old", "version": "1-1", "arch": "noarch", "nevra": "old-1-1.noarch"}], True), err)

# A layer entry without integrity means "same build". The old code compared it with the repo's integrity
# via g_strcmp0 (NULL vs "sha256-..." is nonzero), so a requested package installed at the same NEVRA was
# locked again. "dep" (a Recommends of app, absent below) is new and still locked; "extra" is unrelated.
APP1 = [{"name": "app", "ver": "1", "recommends": ["dep"]}, {"name": "dep"}, {"name": "extra"}]
APP12 = [{"name": "app", "ver": "2", "recommends": ["dep"]}, *APP1]
NO_INTEGRITY = {k: v for k, v in locked("app", "1").items() if k != "integrity"}
for CASE, repo, entry, want in [
        ("a layer entry without integrity at the same NEVRA is unchanged", APP1, NO_INTEGRITY,
         ["dep-1-1.noarch", "extra-1-1.noarch"]),
        ("a layer entry with an empty integrity at the same NEVRA is unchanged", APP1,
         {**NO_INTEGRITY, "integrity": ""}, ["dep-1-1.noarch", "extra-1-1.noarch"]),
        ("a layer entry without integrity at another NEVRA is replaced", APP12, NO_INTEGRITY,
         ["app-2-1.noarch", "dep-1-1.noarch", "extra-1-1.noarch"]),
        ("a layer entry with another integrity counts as changed", APP1,
         {**NO_INTEGRITY, "integrity": "sha256-AAAA"}, ["app-1-1.noarch", "dep-1-1.noarch", "extra-1-1.noarch"])]:
    rc, nevras, err, obsoleted = lock(CASE, repo, [entry], ["app", "extra"])
    check(CASE, (rc, nevras), (0, want), err)

# dnf installs the Recommends of what it installs (install_weak_deps=True), unless a layer below has them.
CASE = "a Recommends missing below is locked with its package"
rc, nevras, err, obsoleted = lock(CASE, [{"name": "app", "recommends": ["extra"]}, {"name": "extra"}, {"name": "base"}],
                                  [locked("base")], ["app"])
check(CASE, (rc, nevras), (0, ["app-1-1.noarch", "extra-1-1.noarch"]), err)

CASE = "a Recommends installed below is not locked again"
rc, nevras, err, obsoleted = lock(CASE, [{"name": "app", "recommends": ["extra"]}, {"name": "extra"}],
                                  [locked("extra")], ["app"])
check(CASE, (rc, nevras), (0, ["app-1-1.noarch"]), err)

for CASE, entry, message in [
        ("a lockfile entry without a name is an error",
         {"version": "1-1", "arch": "noarch", "nevra": "tour-1-1.noarch"}, "package 0 has no name"),
        ("a lockfile entry with a non-string name is an error",
         {**locked("tour"), "name": 123}, "package 0 has a non-string name"),
        ("a lockfile entry with a non-string nevra is an error",
         {**locked("tour"), "nevra": 5}, "package 0 has a non-string nevra")]:
    rc, nevras, err, obsoleted = lock(CASE, [{"name": "tour"}, {"name": "app", "requires": ["tour"]}], [entry], ["app"])
    check(CASE, (rc != 0, message in err), (True, True), err)
