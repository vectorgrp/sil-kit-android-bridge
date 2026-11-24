# Contributing

Contributions are welcome. For a substantial change, open an issue first.

## License and sign-off

Contributions are accepted under the MIT License ([LICENSE.md](LICENSE.md)).
Vendored third-party code keeps its own license and headers; see
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

Every commit needs a [Developer Certificate of Origin](https://developercertificate.org/)
sign-off, with the same name and email as the commit author:

```
Signed-off-by: Your Name <your.email@example.com>
```

`git commit -s` adds it for you. Pull requests with unsigned commits are not
merged.

## Repository layout

| Path | Contents |
|------|----------|
| `source/` | the bridge |
| `source/third_party/` | vendored AOSP VHAL interface and spdlog |
| `test/` | unit and integration tests, test helpers, manual tools |
| `cmake/`, `ci/` | build modules and CI checks |
| `examples/vector-canoe-se/` | CANoe Server Edition test configuration |
| `docs/` | documentation |

## Coding conventions

- Write the test first, watch it fail, then implement. See
  [docs/testing.md](docs/testing.md).
- C++17. Format with `clang-format`. ASCII only in code and comments.
- Prefer composition over inheritance, and templates and static dispatch over
  virtual interfaces. Add an interface only where a test needs to inject a
  dependency.
- Use trailing return types: `auto foo() -> int`.
- In header-only code, put all declarations first and the implementations in
  one block after them.
- Prefix project-owned CMake variables, compile definitions and environment
  variables with `SKAB_` / `skab_`. `SK_` and `SAB_` collide with other projects.
- Every new file carries the SPDX header, with the current year:
  ```
  // SPDX-FileCopyrightText: 2026 Vector Informatik GmbH
  // SPDX-License-Identifier: MIT
  ```
- Adding or removing a dependency means updating
  [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) and `conan.lock` (see
  [docs/building.md](docs/building.md#updating-conanlock)).

## Submitting a change

1. Fork the repository and create a topic branch. Keep the change focused.
2. Build and run the tests: [docs/building.md](docs/building.md),
   [docs/testing.md](docs/testing.md).
3. Open a pull request that explains the change and links its issue.
