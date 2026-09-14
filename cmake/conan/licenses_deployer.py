# SPDX-FileCopyrightText: 2026 Vector Informatik GmbH
# SPDX-License-Identifier: MIT
#
# Conan 2 deployer that collects the license texts of every redistributed
# dependency, run as part of the `conan install` in cmake/fetch_conan.cmake.
#
# gRPC, Protobuf, abseil, OpenSSL, re2, c-ares and zlib are all static libraries
# (shared=False), so they end up inside the shipped executables and their
# licenses have to travel with them - Apache-2.0 section 4(d) in particular.
# Every ConanCenter package carries its texts in <package_folder>/licenses; a
# dependency that does not is a hard error, since it would ship without one.
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

from conan.errors import ConanException


def deploy(graph, output_folder, **kwargs):
    conanfile = graph.root.conanfile
    entries = []
    missing = []

    for _, dep in conanfile.dependencies.host.items():
        name = str(dep.ref.name)
        version = str(dep.ref.version)

        if dep.package_folder is None:
            missing.append("{} (no package folder)".format(dep.ref))
            continue

        src = os.path.join(dep.package_folder, "licenses")
        texts = _files_under(src)
        if not texts:
            missing.append("{} (no licenses/ in the package)".format(dep.ref))
            continue

        dst = os.path.join(output_folder, "{}-{}".format(name, version))
        # Rewritten on every configure: a version bump must not leave the texts
        # of the previous one behind.
        if os.path.isdir(dst):
            shutil.rmtree(dst)
        shutil.copytree(src, dst)
        entries.append((name, version, str(dep.license or "unknown"), len(texts)))

    # Fatal, not a warning. Every one of these is linked into a shipped
    # executable, so a package without its text is a licence violation that a
    # warning would scroll past.
    if missing:
        raise ConanException(
            "licenses_deployer: no license text for "
            + ", ".join(sorted(missing))
            + ". These are redistributed inside the executables, so their notices "
              "must ship. Vendor the text by hand or drop the dependency.")

    _write_index(os.path.join(output_folder, "NOTICES-conan.md"), sorted(entries))
    conanfile.output.info(
        "licenses_deployer: collected {} dependencies into {}".format(
            len(entries), output_folder))


def _files_under(path):
    if not os.path.isdir(path):
        return []
    found = []
    for root, _, names in os.walk(path):
        found.extend(os.path.join(root, n) for n in names)
    return found


def _write_index(path, entries):
    lines = [
        "# Third-party licenses (Conan dependencies)",
        "",
        "These components are linked into the executables shipped alongside this file.",
        "Each one's full license text is in the directory named after it.",
        "",
        "| Component | Version | License | Files |",
        "|-----------|---------|---------|-------|",
    ]
    for name, version, license_id, count in entries:
        lines.append("| {} | {} | {} | {} |".format(name, version, license_id, count))
    lines.append("")

    with open(path, "w", encoding="utf-8", newline="\n") as handle:
        handle.write("\n".join(lines))
