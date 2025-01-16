# SPDX-License-Identifier: GPL-2.0

"""
Macros to define rules for sources which we want to take directly from ACK.

These files are very lightly modified or built exactly from ACK and aren't
suitable to be system_dlkm modules. We cannot reference them directly in DDK
module sources due to DDK limitations that exist today.
"""

load("@bazel_skylib//rules:copy_file.bzl", "copy_file")

COPY_FILES = [
    "drivers/leds/trigger/ledtrig-pattern.c",
]

def define_common_upstream_files():
    for file in COPY_FILES:
        copy_file(
            name = "copied-" + file,
            src = "//common:{}".format(file),
            out = file,
        )
