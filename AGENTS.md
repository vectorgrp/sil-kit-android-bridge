# AGENTS.md

Guidance for coding agents. The rules are the same as for human contributors
and live in the contributor docs:

- [CONTRIBUTING.md](CONTRIBUTING.md): repository layout, coding conventions
- [docs/testing.md](docs/testing.md): test rules, targets, helpers, adding a test
- [docs/building.md](docs/building.md): prerequisites, Conan, packaging, releasing

Read CONTRIBUTING.md and docs/testing.md before changing code.

## Build and test

```sh
cmake --preset relwithdebinfo
cmake --build --preset relwithdebinfo
ctest --preset relwithdebinfo --output-on-failure
```

On Windows, run these from a Visual Studio developer command prompt.
