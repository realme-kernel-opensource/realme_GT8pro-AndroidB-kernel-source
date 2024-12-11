def register_modules(registry):
    registry.register(
        name = "drivers/char/rdbg",
        out = "rdbg.ko",
        config = "CONFIG_MSM_RDBG",
        srcs = [
            # do not sort
            "drivers/char/rdbg.c",
        ],
        deps = [
            # do not sort
            "drivers/soc/qcom/smem",
        ],
    )
