# SPDX-FileCopyrightText: 2026 Vector Informatik GmbH
# SPDX-License-Identifier: MIT
#
# Single source of the project version. Included by CMakeLists.txt before
# project(); conanfile.py and .github/workflows/ci.yml parse these set() lines
# with a regex, so keep them plain.
set(SKAB_VERSION_MAJOR 0)
set(SKAB_VERSION_MINOR 3)
set(SKAB_VERSION_PATCH 1)

set(SKAB_VERSION "${SKAB_VERSION_MAJOR}.${SKAB_VERSION_MINOR}.${SKAB_VERSION_PATCH}")
