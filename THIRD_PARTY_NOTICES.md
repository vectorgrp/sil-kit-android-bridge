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

### MIT (spdlog, fmt)

- spdlog: `Copyright (c) 2016 - present, Gabi Melman and spdlog contributors.`
  License text: `source/third_party/spdlog/LICENSE`.
- fmt: `Copyright (c) 2012 - present, Victor Zverovich and {fmt} contributors.`
  License text: `source/third_party/spdlog/include/spdlog/fmt/bundled/fmt.license.rst`.

## Link-time dependencies (not vendored)

These are resolved at build time and are not redistributed within this repository, but the
compiled binaries link against them:

| Component | License | Source |
|-----------|---------|--------|
| gRPC | Apache-2.0 | ConanCenter (Windows) / distribution packages (Linux) |
| Protobuf | BSD-3-Clause | pulled in with gRPC |
| GoogleTest / GoogleMock (tests only) | BSD-3-Clause | ConanCenter (Windows) / distribution packages (Linux) |
| Vector SIL Kit | see [SIL Kit](https://github.com/vectorgrp/sil-kit) | public GitHub releases |

If you redistribute compiled binaries, ensure the applicable notices for these link-time
dependencies accompany your distribution.
