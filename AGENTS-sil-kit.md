# SIL Kit (FOSS) — Reference Context

## Repository layout

Top-level directories:
- `SilKit/` — the library itself: public headers, implementation, and tests
- `Utilities/` — standalone tools: registry, monitor, system controller
- `Demos/` — example applications and sample integrations
- `docs/` — Sphinx and Doxygen documentation sources
- `ThirdParty/` — vendored dependencies (avoid editing unless necessary)
- `cmake/` — shared CMake helper modules and packaging files

### SilKit/ public surface

- `SilKit/include/silkit/` — public C++ and C API headers; hourglass pattern that wraps the C-API to stay ABI- and API-compatible
- `SilKit/source/capi/` — C API implementation layer (the actual ABI: C functions, datatypes, typedefs)

### SilKit/source/ internal areas

- `util/` — shared helper code
- `config/` — configuration loading and support
- `tracing/` — tracing support
- `core/` — core runtime: participant internals, VAsio transport, service discovery, orchestration
- `services/` — per-bus implementations: CAN, Ethernet, LIN, FlexRay, PubSub, RPC, logging, metrics, orchestration
- `wire/` — internal wire message types (serializable versions of public frame types)
- `extensions/` — extension implementation
- `experimental/` — experimental features, including network simulator internals
- `dashboard/` — dashboard client and service code

The source tree compiles into many object libraries that are linked into the final `SilKit` shared library — a change in a small submodule affects the shared library without introducing a new binary.

## Stability guarantees

- Semver is used; see `SilKit/docs/for-developers/versioning.md`
- The networking layer is never broken; it has its own protocol versioning and capability system for backward compatibility
- The C-API is an ABI-stable boundary
- `SilKit/include/` is a header-only C++ API on top of the C-API (the hourglass); its namespaces are public API and must not change
- Unit tests use gtest/gmock; integration tests link against the public SilKit target
- Experimental namespaces/symbols are exempt from stability guarantees

## Tests

Tests are wired up through `SilKit/cmake/SilKitTest.cmake`. There are several aggregate executables:

- `SilKitUnitTests`
- `SilKitIntegrationTests` / `SilKitInternalIntegrationTests`
- `SilKitFunctionalTests` / `SilKitInternalFunctionalTests`

CTest registers one entry per test suite (not per binary) using `--gtest_filter=<suite>.*`, so `ctest` output is more granular than the binary count suggests. A new `*Test_*.cpp` added via the helper macro will appear as its own CTest suite.

## Navigating changes

When working on a change:

1. Find the public API or executable entry point involved.
2. Trace from the relevant `CMakeLists.txt` into the concrete source directory.
3. Identify the owning area under `SilKit/source/`, `Utilities/`, or `Demos/`.
4. Check nearby tests and existing docs before editing.

Quick orientation:
- `SilKit/include/` — understand the public API shape
- `SilKit/source/` — change behavior or internals
- `SilKit/source/capi/` — preserve or extend C API behavior

## Utilities/

Standalone tools built on top of the library: `SilKitRegistry/`, `SilKitSystemController/`, `SilKitMonitor/`. Included only when `SILKIT_BUILD_UTILITIES=ON`. Relevant when touching startup flows, participant discovery, or orchestration.

## Demos/

Example applications under `Demos/communication/` (CAN, Ethernet, FlexRay, LIN, PubSub, RPC), `Demos/api/`, and `Demos/tools/Benchmark/`. Some integration tests in `SilKit/IntegrationTests/` are based on demo applications — changes here can affect test behavior.

## ThirdParty/

Vendored dependencies. Avoid editing vendored code; prefer changes in SIL Kit integration code. If a change must touch `ThirdParty/`, document the reason clearly.
