#!/bin/bash
# SPDX-License-Identifier: Apache-2.0
# (c) Copyright 2021 - 2022 Xilinx, Inc. All rights reserved.

source "$(dirname "$(which "$0")")"/guess.sh

"${TE_BASE}"/scripts/log.sh "$@"
