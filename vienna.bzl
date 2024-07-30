load(":msm_kernel_la.bzl", "define_msm_la")
load(":target_variants.bzl", "la_variants")

target_name = "vienna"

def define_vienna():
    _vienna_in_tree_modules = [
        # keep sorted
        "drivers/clk/qcom/clk-dummy.ko",
        "drivers/clk/qcom/clk-qcom.ko",
        "drivers/clk/qcom/gdsc-regulator.ko",
        "drivers/dma-buf/heaps/qcom_dma_heaps.ko",
        "drivers/firmware/qcom/qcom-scm.ko",
        "drivers/iommu/arm/arm-smmu/arm_smmu.ko",
        "drivers/iommu/iommu-logger.ko",
        "drivers/iommu/msm_dma_iommu_mapping.ko",
        "drivers/iommu/qcom_iommu_debug.ko",
        "drivers/iommu/qcom_iommu_util.ko",
        "drivers/irqchip/qcom-pdc.ko",
        "drivers/pinctrl/qcom/pinctrl-msm.ko",
        "drivers/pinctrl/qcom/pinctrl-vienna.ko",
        "drivers/regulator/stub-regulator.ko",
        "drivers/soc/qcom/cmd-db.ko",
        "drivers/soc/qcom/mem_buf/mem_buf.ko",
        "drivers/soc/qcom/mem_buf/mem_buf_dev.ko",
        "drivers/soc/qcom/qcom_rpmh.ko",
        "drivers/soc/qcom/secure_buffer.ko",
    ]

    _vienna_consolidate_in_tree_modules = _vienna_in_tree_modules + [
        # keep sorted
        "drivers/misc/lkdtm/lkdtm.ko",
        "kernel/locking/locktorture.ko",
        "kernel/rcu/rcutorture.ko",
        "kernel/torture.ko",
        "lib/atomic64_test.ko",
        "lib/test_user_copy.ko",
    ]

    for variant in la_variants:
        if variant == "consolidate":
            mod_list = _vienna_consolidate_in_tree_modules
        else:
            mod_list = _vienna_in_tree_modules

        define_msm_la(
            msm_target = target_name,
            variant = variant,
            in_tree_module_list = mod_list,
        )
