# SPDX-FileCopyrightText: 2026 Vector Informatik GmbH
# SPDX-License-Identifier: MIT
#
# Conan 2 deployer that collects the license texts of every redistributed
# dependency, run as part of the `conan install` in cmake/fetch_conan.cmake.
#
# gRPC, Protobuf, abseil, OpenSSL, re2, c-ares and zlib are all static libraries
# (shared=False), so they end up inside the shipped executables and their
# licenses have to travel with them - Apache-2.0 section 4(d) in particular.
# Every ConanCenter package carries its texts in <package_folder>/licenses.
#
# Output layout, consumed by cmake/Notices.cmake:
#
#   <output_folder>/NOTICES-conan.md      index: name, version, SPDX id
#   <output_folder>/<name>-<version>/...  the package's licenses/ folder verbatim
#
# Only conanfile.dependencies.host is walked. That excludes build context tools
# (nasm, strawberryperl, cmake) and test_requires (GoogleTest), none of which are
# linked into anything that gets distributed.

import os
import shutil


def deploy(graph, output_folder, **kwargs):
    conanfile = graph.root.conanfile
    entries = []

    for _, dep in conanfile.dependencies.host.items():
        if dep.package_folder is None:
            conanfile.output.warning(
                "licenses_deployer: {} has no package folder, skipping".format(dep.ref))
            continue

        name = str(dep.ref.name)
        version = str(dep.ref.version)
        entries.append((name, version, str(dep.license or "unknown")))

        src = os.path.join(dep.package_folder, "licenses")
        if not os.path.isdir(src):
            conanfile.output.warning(
                "licenses_deployer: {} ships no licenses folder".format(dep.ref))
            continue

        dst = os.path.join(output_folder, "{}-{}".format(name, version))
        # Rewritten on every configure: a version bump must not leave the texts
        # of the previous one behind.
        if os.path.isdir(dst):
            shutil.rmtree(dst)
        shutil.copytree(src, dst)

    _write_index(os.path.join(output_folder, "NOTICES-conan.md"), sorted(entries))
    conanfile.output.info(
        "licenses_deployer: collected {} dependencies into {}".format(
            len(entries), output_folder))


def _write_index(path, entries):
    lines = [
        "# Third-party licenses (Conan dependencies)",
        "",
        "These components are linked into the executables shipped alongside this file.",
        "Each one's full license text is in the directory named after it.",
        "",
        "| Component | Version | License |",
        "|-----------|---------|---------|",
    ]
    for name, version, license_id in entries:
        lines.append("| {} | {} | {} |".format(name, version, license_id))
    lines.append("")

    with open(path, "w", encoding="utf-8", newline="\n") as handle:
        handle.write("\n".join(lines))
