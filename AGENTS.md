# General
If you do not know the exact answer regarding protocols and specifications be upfront and say so instead of fabricating an answer.
Do not use non-ASCII characters in code or comments.
Never commit to git without permission.
Never stage changes, unless asked.

# planning
Working notes and implementation plans go in a `design/` folder, which is
gitignored and stays local. This is a public repository: do not commit planning
documents to it. What belongs in the repository is the change itself, its tests,
and a pull request that explains the reasoning.

If you keep plans there: number them with a `001-` prefix, give derived plans an
extra number (`001.01-`), and name them so the name maps to a feature branch.

## Repository layout

- `source/` - VHAL / SIL Kit adapter code
  - `third_party/android-16.0.0_r3/` - vendored AOSP VHAL gRPC interface and proto definitions
  - `third_party/spdlog/` - vendored spdlog logging library
- `test/` - gtest unit & integration tests, the test harness (`TestHarness.hpp`,
  `SkParticipant.hpp`), and manual tools (`grpc-client-test`)
- `cmake/` - CMake helper modules (cross-platform gRPC / GoogleTest detection)

## Coding conventions

- do test driven development. always write tests that capture the new architecture/behavior first, then add the implementation and use tests as guard rails.
- tests assert on logic, never on timing. CI runners are heavily loaded and skew
  execution time arbitrarily, so no assertion may depend on something happening
  within a duration. Wait on a condition (an event, a state query, a barrier that
  proves the system settled); a timeout is only ever a hang guard, set far beyond
  any plausible delay, and never a threshold a passing test approaches. Proving a
  negative ("this must not match") is done by ordering it after a positive that
  establishes the same precondition, not by waiting a while and seeing nothing.
- SIL Kit participants in tests must create every controller (RPC client/server,
  publisher/subscriber) *before* `StartLifecycle()`. The first lifecycle state is
  `ParticipantState::ServicesCreated`; a controller created after that point can
  miss the service discovery round and then never match its peer, which shows up
  as a load-dependent flake rather than a clean failure. `test/SkParticipant.hpp`
  enforces this by throwing from `AddRpcClient()` once `Start()` has run.
- Unit tests go in files `Test_*` and have no dependency on public APIs.
- Integration Tests go in files `ITest_*`, they should only depend on the network simulator and public SilKit.{so,dll} APIs
- SPDX license header on every new file: `// SPDX-FileCopyrightText: 2026 Vector Informatik GmbH` + `// SPDX-License-Identifier: MIT`, update for current year
- for platform support we are limited to C++17, but want to upgrade to C++20 or later in the future
- prefer C++ composition over inheritance. templates and static dispatch are to be preferred
- always use clang-format to format c++, aim for compile-time safety.
- header only declarations should first have the declarations, and then the implementations after the declaration ina block (this is currently not done everywhere)
- do not add gratuitous interfaces, however allow for testing and dependency injection.
- use trailing return type functions (e.g. auto foo() -> returnValue)
- prefix project-owned identifiers (CMake variables, compile definitions,
  environment variables) with `SKAB_` / `skab_`. Plain `SK_` and `SAB_` are used
  by other projects and collide.

## versioning
`cmake/Version.cmake` is the single source of the project version: it holds the
major/minor/patch triple, the top-level `CMakeLists.txt` includes it before
`project()`, `conanfile.py` parses it in `set_version()`, and the release
workflow checks the pushed `v*` tag against it. A bump is a one-line edit there;
CPack, the `SKAB_VHAL_VERSION` compile definition and the Conan graph all follow.
Keep the `set()` lines plain - the non-CMake readers use a regular expression.

Releases are cut by pushing a `v*` tag; CI then leaves a draft release with the
archives attached, for a human to publish. Never publish a release by hand
before CI has attached them - under immutable releases that seals the release
and burns the tag name for good. See the Releasing section in README.md.

## building
use cmake preset `debug` on Linux, `relwithdebinfo` on Windows. The `debug`
preset is disabled on Windows: ConanCenter publishes no Debug binaries, so a
Debug configure would compile the whole dependency tree from source.

Linux resolves gRPC, Protobuf and GoogleTest from apt. Windows resolves them
with Conan 2: `cmake/fetch_conan.cmake` provisions a Conan 2 client into
`_build/conan-venv` using `uv` and runs `conan install` at configure time, so the
Conan 1 client that corporate machines have on PATH is never used. Build from a
Visual Studio developer command prompt. The Conan home is `_build/conan2`, so a
build writes nothing outside the repository; it carries
`cmake/conan/compatibility.py`, which lets a newer MSVC reuse the msvc 194
binaries ConanCenter publishes - do not pin `compiler.version` or
`compiler.cppstd` in a profile to achieve that, both make things worse. See the
Prerequisites section in README.md.

Dependencies are linked statically, so their license texts have to be
redistributed: `cmake/conan/licenses_deployer.py` collects them from the Conan
packages and `cmake/Notices.cmake` installs them alongside the vendored ones.
Adding or removing a dependency means checking `THIRD_PARTY_NOTICES.md`.