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
    "drivers/char/virtio_console.c",
    "drivers/virtio/virtio_input.c",
    "drivers/block/virtio_blk.c",
    "net/core/failover.c",
    "drivers/net/net_failover.c",
    "drivers/net/virtio_net.c",
    "drivers/net/pcs/pcs-xpcs.c",
    "drivers/net/pcs/pcs-xpcs.h",
    "drivers/net/pcs/pcs-xpcs-nxp.c",
    "drivers/net/pcs/pcs-xpcs-plat.c",
    "drivers/net/pcs/pcs-xpcs-wx.c",
]

def define_common_upstream_files():
    for file in COPY_FILES:
        copy_file(
            name = "copied-" + file,
            src = "//common:{}".format(file),
            out = file,
        )

    # TODO: Use hermetic_genrule when prebuilt `patch` tool is available.
    native.genrule(
        name = "patched-drivers/regulator/qti_fixed_regulator.c",
        srcs = [
            "//common:drivers/regulator/fixed.c",
            ":drivers/regulator/fixed.diff",
        ],
        outs = ["drivers/regulator/qti_fixed_regulator.c"],
        cmd = "patch --follow-symlinks -o $@ -i $(execpath :drivers/regulator/fixed.diff) $(execpath //common:drivers/regulator/fixed.c)",
    )
