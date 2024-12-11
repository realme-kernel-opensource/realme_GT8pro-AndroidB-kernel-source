def register_modules(registry):
    registry.register(
        name = "drivers/cpufreq/qcom-cpufreq-thermal",
        out = "qcom-cpufreq-thermal.ko",
        config = "CONFIG_ARM_QCOM_CPUFREQ_THERMAL",
        srcs = [
            # do not sort
            "drivers/cpufreq/qcom-cpufreq-thermal.c",
        ],
    )
