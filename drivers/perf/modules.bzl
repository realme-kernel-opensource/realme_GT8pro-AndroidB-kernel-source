def register_modules(registry):
    registry.register(
        name = "drivers/perf/qcom_llcc_pmu",
        out = "qcom_llcc_pmu.ko",
        config = "CONFIG_QCOM_LLCC_PMU",
        srcs = [
            # do not sort
            "drivers/perf/qcom_llcc_pmu.c",
        ],
        deps = [
            # do not sort
            "drivers/soc/qcom/cpu_phys_log_map",
        ],
    )
