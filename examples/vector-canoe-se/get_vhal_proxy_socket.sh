#!/bin/sh
# SPDX-FileCopyrightText: 2026 Vector Informatik GmbH
# SPDX-License-Identifier: MIT
pgrep -al vhal_proxy | awk '
/cvd/ { print $NF }
'
