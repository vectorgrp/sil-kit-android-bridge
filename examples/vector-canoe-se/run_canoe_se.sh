#!/bin/bash
# SPDX-FileCopyrightText: 2026 Vector Informatik GmbH
# SPDX-License-Identifier: MIT
set -eu
BASE=$(dirname "$0")
environment-make  -o "_OUT" $BASE/venvironment.yaml  -A Linux64
test-unit-make "$BASE/VhalTest.vtestunit.yaml"  -e  "_OUT/Default.venvironment/" -o "_OUT"
(
    cd _OUT
    canoe-se "Default.venvironment/" --test-unit "VhalTest.vtestunit"
)