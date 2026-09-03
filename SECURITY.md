<!-- SPDX-FileCopyrightText: 2026 Vector Informatik GmbH -->
<!-- SPDX-License-Identifier: MIT -->

# Security Policy

## Project status

`sil-kit-android-bridge` is a **proof of concept**. It is not hardened, it has
not been security reviewed, and it should not be exposed to untrusted networks or
run in a production environment. See the note at the top of
[README.md](README.md).

That said, we would rather hear about a problem than not, and we will fix what we
can.

## Supported versions

Only the latest release is supported. There are no maintenance branches and no
backports while the project is on `0.x`.

| Version | Supported |
|---------|-----------|
| latest `0.x` release | yes |
| anything older | no |

## Reporting a vulnerability

**Please do not open a public GitHub issue for a security problem.**

Report it privately through GitHub's
[private vulnerability reporting](https://docs.github.com/en/code-security/security-advisories/guidance-on-reporting-and-writing-information-about-vulnerabilities/privately-reporting-a-security-vulnerability)
on this repository ("Security" tab -> "Report a vulnerability"), or by email to:

> **`support@vector.com`**

Please include:

- what you found and why you believe it is a security issue,
- the version or commit you tested,
- the platform (Linux distribution or Windows version) and how the bridge was
  invoked,
- a reproduction, ideally against `--mock` so no Android device is needed.

We will acknowledge your report and tell you whether we consider it in scope.
Because this is a proof of concept maintained alongside other work, we cannot
commit to a fix timeline. We will be straight with you about what we intend to
do.

## Scope

In scope: the bridge itself (`source/`), its handling of data arriving over the
SIL Kit RPC surface or the VHAL gRPC connection, and the build and packaging
scripts.

Out of scope: vulnerabilities in [SIL Kit](https://github.com/vectorgrp/sil-kit)
itself (report those to that project), in gRPC, Protobuf, OpenSSL or the other
dependencies listed in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md), and in
the vendored AOSP code (report those to AOSP). Also out of scope: the fact that
the bridge performs no authentication and no transport encryption on either side.
That is a known and documented property of a proof of concept, not a finding.
