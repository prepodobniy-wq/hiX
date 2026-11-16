#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
# Exit 0 when rustc can build no_std code for i386.
. "$(dirname -- "$0")/rust-common.sh"
hix_rust_available
