# General
If you do not know the exact answer regarding protocols and specifications be upfront and say so instead of fabricating an answer.
Do not use non-ASCII characters in code or comments.
Never commit to git without permission.
Never stage changes, unless asked.

# planning
keep plans in `design/` folder in the current project.
plans have a number, e.g. 001-, prefix in file.
derived plans have additional numbers, e.g. 001.01.
plans have a name that can be mapped to a feature branch.

## SIL Kit conventions
See @AGENTS-sil-kit.md for the SIL Kit (FOSS) repository layout and stability guarantees.

## Repository layout

- `source/` - VHAL / SIL Kit adapter code
  - `third_party/android-16.0.0_r3/` - vendored AOSP VHAL gRPC interface and proto definitions
  - `third_party/spdlog/` - vendored spdlog logging library
- `test/` - gtest unit & integration tests, the test harness (`TestHarness.hpp`,
  `SkParticipant.hpp`), and manual tools (`grpc-client-test`)
- `cmake/` - CMake helper modules (cross-platform gRPC / GoogleTest detection)

## Coding conventions

- do test driven development. always write tests that capture the new architecture/behavior first, then add the implementation and use tests as guard rails.
- Unit tests go in files `Test_*` and have no dependency on public APIs.
- Integration Tests go in files `ITest_*`, they should only depend on the network simulator and public SilKit.{so,dll} APIs
- SPDX license header on every new file: `// SPDX-FileCopyrightText: 2026 Vector Informatik GmbH` + `// SPDX-License-Identifier: MIT`, update for current year
- for platform support we are limited to C++17, but want to upgrade to C++20 or later in the future
- prefer C++ composition over inheritance. templates and static dispatch are to be preferred
- always use clang-format to format c++, aim for compile-time safety.
- header only declarations should first have the declarations, and then the implementations after the declaration ina block (this is currently not done everywhere)
- do not add gratuitous interfaces, however allow for testing and dependency injection.
- use trailing return type functions (e.g. auto foo() -> returnValue)

## building
use cmake preset debug

Linux resolves gRPC, Protobuf and GoogleTest from apt. Windows resolves them
with Conan 2: `cmake/fetch_conan.cmake` provisions a Conan 2 client into `.venv`
using `uv` and runs `conan install` at configure time, so the Conan 1 client that
corporate machines have on PATH is never used. Build from a Visual Studio
developer command prompt, and add `-DCONAN_SETTINGS=compiler.version=194` when
Visual Studio detects as msvc 195 - ConanCenter only publishes binaries for 194.
See the Prerequisites section in README.md. On Windows prefer the `relwithdebinfo`
preset: ConanCenter publishes Release binaries only, so `debug` builds every
dependency from source.