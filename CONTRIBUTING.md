# Contributing

Thank you for your interest in this project. Contributions are welcome.

## License of contributions

This project is licensed under the MIT License (see [LICENSE.md](LICENSE.md)). By submitting
a contribution, you agree that your contribution is provided under the same MIT License and
that you have the right to submit it.

Note that this repository also vendors third-party code under other licenses (notably the
AOSP Vehicle HAL under Apache-2.0); see [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
Do not relicense vendored third-party code, and preserve its existing license headers.

## Developer Certificate of Origin (DCO)

This project uses the [Developer Certificate of Origin](https://developercertificate.org/).
Every commit must be signed off, certifying that you wrote the change or otherwise have the
right to submit it under the project's license. Add a sign-off line to each commit:

```
Signed-off-by: Your Name <your.email@example.com>
```

Use `git commit --signoff` (`-s`) to add it automatically. The name and email in the
sign-off must match those of the commit author. Pull requests with unsigned commits will
not be merged.

## Before you start

- Open an issue to discuss substantial changes before investing significant effort.
- Keep changes focused; unrelated fixes belong in separate pull requests.

## Coding standards

- New source files must carry the SPDX header:
  `// SPDX-FileCopyrightText: <year> Vector Informatik GmbH`
  `// SPDX-License-Identifier: MIT`
- Follow the conventions in [AGENTS.md](AGENTS.md): test-driven development, C++17,
  `clang-format` formatting, and the `Test_*` / `ITest_*` test naming split.
- Do not use non-ASCII characters in code or comments.

## Submitting changes

1. Fork the repository and create a topic branch.
2. Add or update tests that capture the new behavior.
3. Ensure the project builds and the tests pass: configure with `cmake --preset debug`
   on Linux or `cmake --preset relwithdebinfo` on Windows (the `debug` preset is
   Linux-only, see the README), then build and run `ctest --preset <same>`.
4. Open a pull request describing the change and referencing any related issue.
