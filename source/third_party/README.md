# Third-Party Dependencies

This directory contains vendored copies of external dependencies.

## Policy

Dependencies are copied in intentionally, not managed via git submodules.
This ensures:

- Reproducible builds without network access at configure/build time
- Deliberate, reviewed updates tied to known upstream versions
- Clear version anchoring per AOSP branch/tag (e.g. `android-16.0.0_r3/`, `android-17.0.0_r1/`)

## Contents

| Directory | Upstream | Version | License |
|-----------|----------|---------|---------|
| `android-16.0.0_r3/` | [AOSP `hardware/interfaces/automotive/vehicle`](https://android.googlesource.com/platform/hardware/interfaces/) | `android-16.0.0_r3` | Apache-2.0 |
| `spdlog/` | [gabime/spdlog](https://github.com/gabime/spdlog) | v1.17.0 | MIT |
| `spdlog/include/spdlog/fmt/bundled/` | [fmtlib/fmt](https://github.com/fmtlib/fmt) (bundled by spdlog) | bundled | MIT |

The original upstream directory hierarchy is preserved inside each versioned directory
so that diffs against upstream are straightforward.

The AOSP subtree carries the upstream `NOTICE` file (the full Apache-2.0 license text) at its
module root, `android-16.0.0_r3/hardware/interfaces/NOTICE`, matching the original AOSP layout.
spdlog and fmt retain their original MIT license files (`spdlog/LICENSE`,
`spdlog/include/spdlog/fmt/bundled/fmt.license.rst`).
See the top-level [THIRD_PARTY_NOTICES.md](../../THIRD_PARTY_NOTICES.md) for the full aggregate.

## Updating the AOSP VHAL interface

To upgrade to a newer Android release:

1. Create `third_party/<aosp-branch-tag>/` (e.g. `android-17.0.0_r1/`), named after the
   exact AOSP branch/tag being vendored.
2. Copy `hardware/interfaces/automotive/vehicle/aidl/impl/current/` from the new
   AOSP release into the new directory, preserving the original structure.
3. Copy the upstream `NOTICE` (and any `LICENSE`) file, preserving its upstream location
   (e.g. `hardware/interfaces/NOTICE`), so the Apache-2.0 attribution travels with the code.
4. Update `ANDROID_VHAL_DIR` in `source/CMakeLists.txt` to point to the new path.
5. Build and run `grpc-client-test` against a matching VHAL server to verify.
6. Remove the old versioned directory once the upgrade is confirmed.
