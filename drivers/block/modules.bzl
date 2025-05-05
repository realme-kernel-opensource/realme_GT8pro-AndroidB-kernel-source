load(":drivers/block/zram/modules.bzl", register_zram = "register_modules")

def register_modules(registry):
    register_zram(registry)
