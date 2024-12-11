def register_modules(registry):
    registry.register(
        name = "drivers/edac/qcom_edac",
        out = "qcom_edac.ko",
        config = "CONFIG_EDAC_QCOM",
        srcs = [
            # do not sort
            "drivers/edac/edac_device.h",
            "drivers/edac/edac_mc.h",
            "drivers/edac/qcom_edac.c",
        ],
    )
