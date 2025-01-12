def register_modules(registry):
    registry.register(
        name = "drivers/powercap/qcom/qptf",
        out = "qptf.ko",
        config = "CONFIG_QCOM_POWER_TELEMETRY_FRAMEWORK",
        srcs = [
            # do not sort
            "drivers/powercap/qcom/qptf.c",
            "drivers/powercap/qcom/qptf_internal.h",
        ],
    )
