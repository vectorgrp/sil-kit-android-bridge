# Third-Party Notices

This project is distributed under the MIT License (see [LICENSE.md](LICENSE.md)) for the
code authored by Vector Informatik GmbH. It additionally bundles and/or links against
third-party components under their own licenses, listed below. The MIT license of this
project does **not** relicense these components; each retains its original license.

## Bundled (vendored) components

| Component | Location | Upstream | License |
|-----------|----------|----------|---------|
| AOSP Vehicle HAL (VHAL) | `source/third_party/android-16.0.0_r3/` | [AOSP `hardware/interfaces/automotive/vehicle`](https://android.googlesource.com/platform/hardware/interfaces/) (tag `android-16.0.0_r3`) | Apache-2.0 |
| AOSP VHAL property enum | `source/VhalProperty.hpp` | Generated from AOSP `VehicleProperty.aidl` | Apache-2.0 |
| SIL Kit `CommandlineParser` | `source/CommandlineParser.hpp` | [vectorgrp/sil-kit](https://github.com/vectorgrp/sil-kit) (`Utilities/`) | MIT |
| spdlog | `source/third_party/spdlog/` | [gabime/spdlog](https://github.com/gabime/spdlog) v1.17.0 | MIT |
| fmt (bundled by spdlog) | `source/third_party/spdlog/include/spdlog/fmt/bundled/` | [fmtlib/fmt](https://github.com/fmtlib/fmt) | MIT |

### Apache-2.0 (AOSP)

The vendored AOSP VHAL code and the generated `source/VhalProperty.hpp` are licensed under the
Apache License, Version 2.0. Every AOSP source, header, and `.proto` file retains its original
`Copyright (C) <year> The Android Open Source Project` header. The full Apache-2.0 license text
and attribution notice is provided in:

    source/third_party/android-16.0.0_r3/hardware/interfaces/NOTICE

A copy of the Apache License, Version 2.0 is available at
<http://www.apache.org/licenses/LICENSE-2.0>.

### MIT (SIL Kit CommandlineParser, spdlog, fmt)

- SIL Kit `CommandlineParser`: `Copyright (c) Vector Informatik GmbH.` Copied
  verbatim from the SIL Kit repository, same copyright holder and same MIT license
  as this project, so it is listed here for provenance rather than because it adds
  a licensing obligation. License text: [LICENSE.md](LICENSE.md).

- spdlog: `Copyright (c) 2016 - present, Gabi Melman and spdlog contributors.`
  License text: `source/third_party/spdlog/LICENSE`.
- fmt: `Copyright (c) 2012 - present, Victor Zverovich and {fmt} contributors.`
  License text: `source/third_party/spdlog/include/spdlog/fmt/bundled/fmt.license.rst`.

## Link-time dependencies (not vendored)

These are resolved at build time and are not present in this repository, but the compiled
binaries link against them. **They are linked statically on both platforms**, so a
distributed binary contains them and their notices have to travel with it.

| Component | License | Linkage | Source |
|-----------|---------|---------|--------|
| gRPC | Apache-2.0 | static | ConanCenter |
| Protobuf | BSD-3-Clause | static | pulled in with gRPC |
| abseil | Apache-2.0 | static | pulled in with gRPC |
| OpenSSL | Apache-2.0 | static | pulled in with gRPC |
| re2 | BSD-3-Clause | static | pulled in with gRPC |
| c-ares | MIT | static | pulled in with gRPC |
| zlib | Zlib | static | pulled in with gRPC |
| Vector SIL Kit | MIT | shared; `SilKit.dll` ships in the Windows archive, `libsilkit` comes from a `.deb` | public GitHub releases |
| GoogleTest / GoogleMock | BSD-3-Clause | tests only, not packaged | ConanCenter |

Building with `-DSKAB_STATIC_DEPS=OFF` uses the distribution's packages and links them
dynamically instead; the `.deb` then declares them through `dpkg-shlibdeps` and the
distribution's own copyright files apply. The released artifacts are not built that way.

### Notices shipped with the binaries

The packages produced by `cmake --build --target package` carry the full license text of
every component listed above, in `docs/licenses/` next to this file: at the top of the zip,
and under `/usr/share/doc/sil-kit-android-bridge/` in the `.deb`. There is one directory per
component, and an index in `NOTICES-conan.md` giving the exact versions and SPDX identifiers. The texts for the Conan-resolved components are collected at configure
time by [cmake/conan/licenses_deployer.py](cmake/conan/licenses_deployer.py), and everything
is installed by [cmake/Notices.cmake](cmake/Notices.cmake). That index is authoritative for
versions; the tables in this file name components, not versions.

Neither half is left to inspection. The deployer fails the configure if a dependency ships
no license text, and `ci/assert-notices.sh` fails CI if the packaged archive is missing a
directory for anything the index lists, or any of the vendored components. Run it yourself
against an unpacked package:

    ci/assert-notices.sh <unpacked>/docs/licenses

Apache-2.0 section 4(d) is the operative obligation for gRPC, abseil and OpenSSL, and it is
satisfied by that directory. If you repackage these binaries by other means, carry those
files along.
