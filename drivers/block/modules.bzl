load(":drivers/block/zram/modules.bzl", register_zram = "register_modules")

def register_modules(registry):
    register_zram(registry)

    registry.register(
        name = "drivers/block/virtio_blk",
        out = "virtio_blk.ko",
        config = "CONFIG_VIRTIO_BLK",
        srcs = [
            # do not sort
            "drivers/block/virtio_blk.c",
        ],
    )
