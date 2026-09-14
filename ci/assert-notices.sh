#!/bin/sh
# SPDX-FileCopyrightText: 2026 Vector Informatik GmbH
# SPDX-License-Identifier: MIT
#
# Fails if a package is missing a license text it is obliged to carry. The
# dependencies are linked into the executables, so the package redistributes
# them; Apache-2.0 4(d) and the BSD/MIT attribution clauses apply.
#
# Every component NOTICES-conan.md lists must have a directory with at least one
# file in it. The deployer writes that index from the Conan graph, so a
# dependency added later is covered without editing this script.
#
# Usage: assert-notices.sh <licenses-dir>

set -eu

dir=${1:-}
if [ -z "$dir" ]; then
    echo "usage: $0 <licenses-dir>" >&2
    exit 2
fi

index="$dir/NOTICES-conan.md"
rc=0

if [ ! -f "$index" ]; then
    echo "ERROR: $index is missing; the package ships linked-in code with no notices" >&2
    exit 1
fi

echo "== $dir"

# Rows are "| name | version | license | ... |". Take the first two fields and
# ignore the header, the separator, and any column added later.
components=$(awk -F'|' '
    /^[|]/ {
        name = $2; version = $3
        gsub(/^[ \t]+/, "", name);    gsub(/[ \t]+$/, "", name)
        gsub(/^[ \t]+/, "", version); gsub(/[ \t]+$/, "", version)
        if (name == "" || name == "Component") next
        if (name ~ /^-+$/) next
        print name "-" version
    }' "$index")

if [ -z "$components" ]; then
    echo "ERROR: $index lists no components" >&2
    exit 1
fi

for c in $components; do
    if [ ! -d "$dir/$c" ]; then
        echo "ERROR: $index lists $c but $dir/$c does not exist" >&2
        rc=1
    elif [ -z "$(find "$dir/$c" -type f 2>/dev/null)" ]; then
        echo "ERROR: $dir/$c is empty" >&2
        rc=1
    else
        echo "   $c: $(find "$dir/$c" -type f | wc -l) file(s)"
    fi
done

# Vendored, installed by cmake/Notices.cmake rather than by the deployer.
for c in aosp-vhal spdlog fmt; do
    if [ -z "$(find "$dir/$c" -type f 2>/dev/null)" ]; then
        echo "ERROR: vendored component '$c' has no license text in $dir" >&2
        rc=1
    else
        echo "   $c: vendored"
    fi
done

if [ "$rc" -eq 0 ]; then
    echo "OK: every component listed in NOTICES-conan.md ships its license text"
fi
exit $rc
