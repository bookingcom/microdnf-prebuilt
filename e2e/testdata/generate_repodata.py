#!/usr/bin/env python3
"""Generates deterministic yum repodata for e2e tests."""

import argparse
import gzip
import hashlib
import os

PRIMARY_XML = b"""<?xml version="1.0" encoding="UTF-8"?>
<metadata xmlns="http://linux.duke.edu/metadata/common" xmlns:rpm="http://linux.duke.edu/metadata/rpm" packages="2">
<package type="rpm">
  <name>mystery-devel</name>
  <arch>noarch</arch>
  <version epoch="0" ver="19.67" rel="1"/>
  <checksum type="sha256" pkgid="YES">2ed8195c48718087c88abf18eb3d64d0d04ad6bef69e2d9f5111a7a287cb13f5</checksum>
  <summary>Devel subpackage.</summary>
  <description>Magical development files for mystery.</description>
  <packager></packager>
  <url></url>
  <time file="1392195164" build="1349876358"/>
  <size package="2329" installed="59" archive="556"/>
  <location xml:base="disagree" href="mystery-devel-19.67-1.noarch.rpm"/>
  <format>
    <rpm:license>GPLv2+</rpm:license>
    <rpm:vendor/>
    <rpm:group>Development/Libraries</rpm:group>
    <rpm:buildhost>aklap.brq.redhat.com</rpm:buildhost>
    <rpm:sourcerpm>mystery-19.67-1.src.rpm</rpm:sourcerpm>
    <rpm:header-range start="280" end="2081"/>
    <rpm:provides>
      <rpm:entry name="mystery-devel" flags="EQ" epoch="0" ver="19.67" rel="1"/>
    </rpm:provides>
  <file>/usr/bin/my</file>
  <file>/usr/bin/ry</file>
  <file>/usr/bin/ste</file>
  </format>
</package>
<package type="rpm">
  <name>tour</name>
  <arch>noarch</arch>
  <version epoch="0" ver="4" rel="6"/>
  <checksum type="sha256" pkgid="YES">76af5f29a71a65c9d991207c4627dcbd3512df33844a3d908fa74f1dcbc36321</checksum>
  <summary>tour package</summary>
  <description>Hawkey tour package to test filelists handling.</description>
  <packager>roll up &lt;roll@up.net&gt;</packager>
  <url></url>
  <time file="1404109239" build="1404109194"/>
  <size package="3000" installed="193" archive="1188"/>
  <location xml:base="disagree" href="tour-4-6.noarch.rpm"/>
  <format>
    <rpm:license>GPLv2+</rpm:license>
    <rpm:vendor/>
    <rpm:group>Utilities</rpm:group>
    <rpm:buildhost>localhost.localdomain</rpm:buildhost>
    <rpm:sourcerpm>tour-4-6.src.rpm</rpm:sourcerpm>
    <rpm:header-range start="280" end="2616"/>
    <rpm:provides>
      <rpm:entry name="tour" flags="EQ" epoch="0" ver="4" rel="6"/>
    </rpm:provides>
  <file>/etc/rollup</file>
  <file>/etc/takeyouaway</file>
  <file>/usr/bin/away</file>
  </format>
</package>
</metadata>
"""

FILELISTS_XML = b"""<?xml version="1.0" encoding="UTF-8"?>
<filelists xmlns="http://linux.duke.edu/metadata/filelists" packages="2">
<package pkgid="2ed8195c48718087c88abf18eb3d64d0d04ad6bef69e2d9f5111a7a287cb13f5" name="mystery-devel" arch="noarch">
  <version epoch="0" ver="19.67" rel="1"/>
  <file>/usr/bin/my</file>
  <file>/usr/bin/ry</file>
  <file>/usr/bin/ste</file>
</package>
<package pkgid="76af5f29a71a65c9d991207c4627dcbd3512df33844a3d908fa74f1dcbc36321" name="tour" arch="noarch">
  <version epoch="0" ver="4" rel="6"/>
  <file>/etc/rollup</file>
  <file>/etc/takeyouaway</file>
  <file>/usr/bin/away</file>
  <file>/usr/lib/python2.7/site-packages/tour/today.py</file>
  <file>/usr/lib/python2.7/site-packages/tour/today.pyc</file>
  <file>/usr/lib/python2.7/site-packages/tour/today.pyo</file>
</package>
</filelists>
"""

OTHER_XML = b"""<?xml version="1.0" encoding="UTF-8"?>
<otherdata xmlns="http://linux.duke.edu/metadata/other" packages="2">
<package pkgid="2ed8195c48718087c88abf18eb3d64d0d04ad6bef69e2d9f5111a7a287cb13f5" name="mystery-devel" arch="noarch">
  <version epoch="0" ver="19.67" rel="1"/>
</package>
<package pkgid="76af5f29a71a65c9d991207c4627dcbd3512df33844a3d908fa74f1dcbc36321" name="tour" arch="noarch">
  <version epoch="0" ver="4" rel="6"/>
</package>
</otherdata>
"""

PRESTODELTA_XML = b"""<?xml version="1.0" encoding="UTF-8"?>
<prestodelta>
  <newpackage name="tour" epoch="0" version="4" release="6" arch="noarch">
     <delta oldepoch="0" oldversion="4" oldrelease="5">
      <filename>drpms/tour-4-5_4-6.noarch.drpm</filename>
      <sequence>tour-4-5-f0da3099074a883dcf9dd4b34f06f8c34110</sequence>
      <size>3132</size>
      <checksum type="sha256">c3c3d572a46b1a66906d42ca1763ef3620f70258aaac4c14bf463ed53716d444</checksum>
    </delta>
    </newpackage>
</prestodelta>
"""

UPDATEINFO_XML = b"""<?xml version="1.0" ?>
<updates>
  <update from="updates@fedoraproject.org" status="stable" type="bugfix" version="1.4">
    <id>FEDORA-2008-9969</id>
    <title>lvm2-2.02.39-7.fc10</title>
    <release>Fedora 10</release>
    <issued date="2008-12-09 11:31:26"/>
    <references>
      <reference href="https://bugzilla.redhat.com/show_bug.cgi?id=472090" id="472090" title="/etc/init.d/clvmd points to /usr/sbin for LVM tools" type="bugzilla"/>
      <reference href="https://bugzilla.gnome.com/show_bug.cgi?id=472091" id="472091" title="CVE DATA" type="cve"/>
    </references>
    <description>An example update to the tour package.</description>
    <pkglist>
      <collection short="F10">
        <name>Fedora 10</name>
        <package arch="noarch" name="tour" version="4" release="7" src="http:///tour.noarch.rpm">
          <filename>tour.noarch.rpm</filename>
          <reboot_suggested>True</reboot_suggested>
        </package>
      </collection>
    </pkglist>
  </update>
  <update from="updates@example.com" status="stable" type="security" version="1.4">
    <id>BEATLES-1967-1127</id>
    <title>Magical Mystery Tour</title>
    <release>Beatles collection</release>
    <issued date="2012-01-23 01:23:45"/>
    <references>
      <reference href="https://bugzilla.redhat.com/show_bug.cgi?id=0#john" id="0#john" title="John" type="bugzilla"/>
      <reference href="https://bugzilla.redhat.com/show_bug.cgi?id=0#paul" id="0#paul" title="Paul" type="bugzilla"/>
      <reference href="https://bugzilla.redhat.com/show_bug.cgi?id=0#george" id="0#george" title="Gerorge" type="bugzilla"/>
      <reference href="https://bugzilla.redhat.com/show_bug.cgi?id=0#ringo" id="0#ringo" title="Ringo" type="bugzilla"/>
      <reference href="https://www.redhat.com/security/data/cve/CVE-1967-BEATLES" id="CVE-1967-BEATLES" title="CVE 1967 BEATLES" type="cve"/>
    </references>
    <description>An importent update to Beatles songs.</description>
    <pkglist>
      <collection short="B">
        <name>Beatles collection</name>
        <package arch="noarch" name="mystery-devel" epoch="0" version="19.67" release="1" src="http:///mystery.src.rpm">
          <filename>mystery-devel-19.67-1.noarch.rpm</filename>
          <reboot_suggested>False</reboot_suggested>
        </package>
        <package arch="noarch" name="tour" version="4" release="6" src="http:///tour.src.rpm">
          <filename>tour-4-6.noarch.rpm</filename>
          <reboot_suggested>True</reboot_suggested>
        </package>
      </collection>
    </pkglist>
  </update>
</updates>
"""

# Synthetic metadata-only repository for the dnf5 lock feature tests
# (forcearch, excludepkgs, modules, best/nobest, weak dependencies).
# Each tuple: (name, epoch, version, release, arch, [required package names]).
FEATURE_PACKAGES = [
    # --forcearch: a noarch package served from a $basearch URL.
    ("libfoo", 0, "2.0", "1", "noarch", []),
    # --excludepkgs: release cut-off globs and full NEVRAs.
    ("ca-certificates", 0, "2025.2.80_v9.0.305", "1.el9", "noarch", []),
    ("ca-certificates", 0, "2026.2.92_v9.0.319", "1.el9", "noarch", []),
    ("certuser", 0, "1.0", "1", "noarch", ["ca-certificates"]),
    ("expat", 0, "2.7.2", "1.el10", "x86_64", []),
    ("expat", 0, "2.7.3", "1.el10_2.7", "x86_64", []),
    # best / nobest: the newest bestapp is not installable.
    ("bestapp", 0, "1.0", "1", "noarch", []),
    ("bestapp", 0, "2.0", "1", "noarch", ["missing-lib"]),
    # --module-enable: non-modular mysql 8.0 and the modular mysql:8.4 stream.
    ("mysql", 0, "8.0.44", "1.el9", "x86_64", []),
    ("mysql", 0, "8.4.7", "1.module+el9.7.0+90001+e2e00084", "x86_64", []),
    # weak dependencies: weakapp Recommends weakdep, which nothing requires.
    ("weakapp", 0, "1.0", "1", "noarch", []),
    ("weakdep", 0, "1.0", "1", "noarch", []),
    # Supplements: suppapp is pulled in only because suppbase is installed.
    ("suppbase", 0, "1.0", "1", "noarch", []),
    ("suppapp", 0, "1.0", "1", "noarch", []),
    # Rich (boolean) weak dependencies, written as rpm:entry names like rpm does.
    ("richapp", 0, "1.0", "1", "noarch", []),
    ("richdep", 0, "1.0", "1", "noarch", []),
    ("richtrigger", 0, "1.0", "1", "noarch", []),
    ("richbase", 0, "1.0", "1", "noarch", []),
    ("richsupp", 0, "1.0", "1", "noarch", []),
]

# Weak dependencies of FEATURE_PACKAGES by package name and rpm:<tag>.
FEATURE_WEAK = {
    "weakapp": {"recommends": ["weakdep"]},
    "suppapp": {"supplements": ["suppbase"]},
    "richapp": {"recommends": ["(richdep if richtrigger)"]},
    "richsupp": {"supplements": ["(richbase and richtrigger)"]},
}

FEATURE_MODULES_YAML = b"""---
document: modulemd
version: 2
data:
  name: mysql
  stream: "8.4"
  version: 9070020251010000000
  context: e2e00084
  arch: x86_64
  summary: MySQL Module
  description: MySQL 8.4 stream for the dnf5 lock e2e tests.
  license:
    module:
    - MIT
  dependencies:
  - buildrequires:
      platform: [el9]
    requires:
      platform: [el9]
  profiles:
    server:
      rpms:
      - mysql
  artifacts:
    rpms:
    - mysql-0:8.4.7-1.module+el9.7.0+90001+e2e00084.x86_64
...
"""


def _xml_doc(root, attrs, body):
    head = '<?xml version="1.0" encoding="UTF-8"?>\n'
    return (head + f"<{root} {attrs}>\n" + "\n".join(body) + f"\n</{root}>\n").encode()


def feature_repodata():
    """Returns (primary, filelists, other) XML for FEATURE_PACKAGES."""
    primary = []
    stubs = []  # filelists.xml and other.xml carry the same per-package stub
    for name, epoch, ver, rel, arch, requires in FEATURE_PACKAGES:
        pkgid = hashlib.sha256(f"{name}-{epoch}:{ver}-{rel}.{arch}".encode()).hexdigest()
        version = f'  <version epoch="{epoch}" ver="{ver}" rel="{rel}"/>'
        primary += [
            '<package type="rpm">',
            f"  <name>{name}</name>",
            f"  <arch>{arch}</arch>",
            version,
            f'  <checksum type="sha256" pkgid="YES">{pkgid}</checksum>',
            f"  <summary>{name} test package</summary>",
            f"  <description>{name} test package.</description>",
            "  <packager></packager>",
            "  <url></url>",
            '  <time file="1404109239" build="1404109194"/>',
            '  <size package="1000" installed="0" archive="0"/>',
            f'  <location href="features/{name}-{ver}-{rel}.{arch}.rpm"/>',
            "  <format>",
            "    <rpm:license>MIT</rpm:license>",
            "    <rpm:vendor/>",
            "    <rpm:group>Unspecified</rpm:group>",
            "    <rpm:buildhost>localhost</rpm:buildhost>",
            f"    <rpm:sourcerpm>{name}-{ver}-{rel}.src.rpm</rpm:sourcerpm>",
            '    <rpm:header-range start="0" end="0"/>',
            "    <rpm:provides>",
            f'      <rpm:entry name="{name}" flags="EQ" epoch="{epoch}" ver="{ver}" rel="{rel}"/>',
            "    </rpm:provides>",
        ]
        if requires:
            primary.append("    <rpm:requires>")
            primary += [f'      <rpm:entry name="{dep}"/>' for dep in requires]
            primary.append("    </rpm:requires>")
        for tag, deps in FEATURE_WEAK.get(name, {}).items():
            primary.append(f"    <rpm:{tag}>")
            primary += [f'      <rpm:entry name="{dep}"/>' for dep in deps]
            primary.append(f"    </rpm:{tag}>")
        primary += ["  </format>", "</package>"]
        stubs += [f'<package pkgid="{pkgid}" name="{name}" arch="{arch}">', version, "</package>"]

    count = f'packages="{len(FEATURE_PACKAGES)}"'
    return (
        _xml_doc(
            "metadata",
            f'xmlns="http://linux.duke.edu/metadata/common" xmlns:rpm="http://linux.duke.edu/metadata/rpm" {count}',
            primary,
        ),
        _xml_doc("filelists", f'xmlns="http://linux.duke.edu/metadata/filelists" {count}', stubs),
        _xml_doc("otherdata", f'xmlns="http://linux.duke.edu/metadata/other" {count}', stubs),
    )


def compress(data):
    return gzip.compress(data, mtime=0)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--out-dir", required=True)
    parser.add_argument("--scenario", choices=["default", "features"], default="default")
    args = parser.parse_args()

    os.makedirs(args.out_dir, exist_ok=True)

    if args.scenario == "features":
        primary_xml, filelists_xml, other_xml = feature_repodata()
        files = {
            "primary.xml.gz": ("primary", primary_xml),
            "filelists.xml.gz": ("filelists", filelists_xml),
            "other.xml.gz": ("other", other_xml),
            "modules.yaml.gz": ("modules", FEATURE_MODULES_YAML),
        }
    else:
        files = {
            "primary.xml.gz": ("primary", PRIMARY_XML),
            "filelists.xml.gz": ("filelists", FILELISTS_XML),
            "other.xml.gz": ("other", OTHER_XML),
            "prestodelta.xml.gz": ("prestodelta", PRESTODELTA_XML),
            "updateinfo.xml.gz": ("updateinfo", UPDATEINFO_XML),
        }

    meta_entries = []
    for fname, (dtype, raw) in files.items():
        compressed = compress(raw)
        out_path = os.path.join(args.out_dir, fname)
        with open(out_path, "wb") as f:
            f.write(compressed)

        c_sha = hashlib.sha256(compressed).hexdigest()
        o_sha = hashlib.sha256(raw).hexdigest()
        c_size = len(compressed)
        o_size = len(raw)

        entry = f"""<data type="{dtype}">
  <checksum type="sha256">{c_sha}</checksum>
  <open-checksum type="sha256">{o_sha}</open-checksum>
  <location href="repodata/{fname}"/>
  <timestamp>1404109454</timestamp>
  <size>{c_size}</size>
  <open-size>{o_size}</open-size>
</data>"""
        meta_entries.append(entry)

    repomd = f"""<?xml version="1.0" encoding="UTF-8"?>
<repomd xmlns="http://linux.duke.edu/metadata/repo" xmlns:rpm="http://linux.duke.edu/metadata/rpm">
 <revision>1404109454</revision>
{chr(10).join(meta_entries)}
</repomd>
"""
    with open(os.path.join(args.out_dir, "repomd.xml"), "w", encoding="utf-8") as f:
        f.write(repomd)


if __name__ == "__main__":
    main()
