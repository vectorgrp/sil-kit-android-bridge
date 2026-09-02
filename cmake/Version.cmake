# SPDX-FileCopyrightText: 2026 Vector Informatik GmbH
# SPDX-License-Identifier: MIT
#
# Single source of truth for the project version.
#
# Included by the top-level CMakeLists.txt before project(), and parsed
# textually by conanfile.py and .github/workflows/ci.yml. Keep the three set()
# calls below in exactly this shape - one plain decimal literal each, one per
# line - the other readers use a regular expression and do not evaluate CMake.
#
# Bumping the version is a one-line edit here. A release is cut by pushing a
# v<major>.<minor>.<patch> tag; CI refuses a tag that disagrees with this file.
set(SKAB_VERSION_MAJOR 0)
set(SKAB_VERSION_MINOR 2)
set(SKAB_VERSION_PATCH 0)

set(SKAB_VERSION "${SKAB_VERSION_MAJOR}.${SKAB_VERSION_MINOR}.${SKAB_VERSION_PATCH}")
