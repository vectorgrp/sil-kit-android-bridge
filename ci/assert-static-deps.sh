#!/bin/sh
# SPDX-FileCopyrightText: 2026 Vector Informatik GmbH
# SPDX-License-Identifier: MIT
#
# Fails if a shipped ELF binary names a dependency that should have been linked
# in. The 0.4.x Linux release named the CI runner's libprotobuf.so.23 and
# libgrpc++.so.1 and started nowhere else; nothing noticed, because the runner
# had them installed.
#
# Usage: assert-static-deps.sh <binary>...

set -eu

# libSilKit has no static variant, and the C/C++ runtime comes from the
# distribution. Everything else in the graph is linked in.
forbidden='^lib(protobuf|protoc|grpc|grpc\+\+|gpr|absl|upb|re2|cares|ssl|crypto|gtest|gmock|fmt|spdlog)'

if [ "$#" -eq 0 ]; then
    echo "usage: $0 <binary>..." >&2
    exit 2
fi

rc=0
for bin in "$@"; do
    if [ ! -f "$bin" ]; then
        echo "ERROR: $bin does not exist" >&2
        rc=1
        continue
    fi

    needed=$(readelf -d "$bin" | sed -n 's/.*(NEEDED).*\[\(.*\)\]/\1/p')
    printf '== %s\n' "$bin"
    printf '%s\n' "$needed" | sed 's/^/   /'

    bad=$(printf '%s\n' "$needed" | grep -E "$forbidden" || true)
    if [ -n "$bad" ]; then
        printf 'ERROR: %s is dynamically linked against:\n' "$bin" >&2
        printf '%s\n' "$bad" | sed 's/^/   /' >&2
        rc=1
    fi
done

if [ "$rc" -eq 0 ]; then
    echo "OK: no dependency that should be linked in appears in DT_NEEDED"
fi
exit $rc
