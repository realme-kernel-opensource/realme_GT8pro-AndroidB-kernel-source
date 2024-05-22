load(":target_variants.bzl", "la_variants")
load(":msm_kernel_la.bzl", "define_msm_la")

target_name = "canoe"

def define_canoe():
    _canoe_in_tree_modules = [
        # keep sorted
        "drivers/clk/qcom/clk-dummy.ko",
        "drivers/clk/qcom/clk-qcom.ko",
        "drivers/cpuidle/governors/qcom_lpm.ko",
        "drivers/firmware/qcom/qcom-scm.ko",
        "drivers/irqchip/qcom-pdc.ko",
        "drivers/pinctrl/qcom/pinctrl-canoe.ko",
        "drivers/pinctrl/qcom/pinctrl-msm.ko",
        "drivers/regulator/debug-regulator.ko",
        "drivers/regulator/proxy-consumer.ko",
        "drivers/regulator/qti-fixed-regulator.ko",
        "drivers/regulator/stub-regulator.ko",
        "drivers/soc/qcom/cmd-db.ko",
        "drivers/soc/qcom/crm-v2.ko",
        "drivers/soc/qcom/qcom_rpmh.ko",
    ]

    _canoe_consolidate_in_tree_modules = _canoe_in_tree_modules + [
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
            mod_list = _canoe_consolidate_in_tree_modules
        else:
            mod_list = _canoe_in_tree_modules

        define_msm_la(
            msm_target = target_name,
            variant = variant,
            in_tree_module_list = mod_list,
        )
