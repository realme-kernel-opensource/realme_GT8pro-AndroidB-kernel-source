def register_modules(registry):
    registry.register(
        name = "drivers/pinctrl/qcom/pinctrl-canoe",
        out = "pinctrl-canoe.ko",
        config = "CONFIG_PINCTRL_CANOE",
        srcs = [
            # do not sort
            "drivers/pinctrl/qcom/pinctrl-canoe.c",
            "drivers/pinctrl/qcom/pinctrl-msm.h",
        ],
        deps = [
            # do not sort
            "drivers/pinctrl/qcom/pinctrl-msm",
            "drivers/firmware/qcom/qcom-scm",
            "drivers/virt/gunyah/gh_rm_drv",
            "drivers/virt/gunyah/gunyah_rsc_mgr",
            "drivers/virt/gunyah/gunyah_platform_hooks",
            "arch/arm64/gunyah/gunyah_hypercall",
            "drivers/virt/gunyah/gh_msgq",
            "drivers/virt/gunyah/gh_dbl",
            "arch/arm64/gunyah/gh_arm_drv",
        ],
    )

    registry.register(
        name = "drivers/pinctrl/qcom/pinctrl-msm",
        out = "pinctrl-msm.ko",
        config = "CONFIG_PINCTRL_MSM",
        srcs = [
            # do not sort
            "drivers/pinctrl/qcom/pinctrl-msm.c",
            "drivers/pinctrl/qcom/pinctrl-msm.h",
        ],
        deps = [
            # do not sort
            "drivers/firmware/qcom/qcom-scm",
            "drivers/virt/gunyah/gh_rm_drv",
            "drivers/virt/gunyah/gunyah_rsc_mgr",
            "drivers/virt/gunyah/gunyah_platform_hooks",
            "arch/arm64/gunyah/gunyah_hypercall",
            "drivers/virt/gunyah/gh_msgq",
            "drivers/virt/gunyah/gh_dbl",
            "arch/arm64/gunyah/gh_arm_drv",
        ],
    )

    registry.register(
        name = "drivers/pinctrl/qcom/pinctrl-spmi-gpio",
        out = "pinctrl-spmi-gpio.ko",
        config = "CONFIG_PINCTRL_QCOM_SPMI_PMIC",
        srcs = [
            # do not sort
            "drivers/pinctrl/qcom/pinctrl-spmi-gpio.c",
        ],
    )

    registry.register(
        name = "drivers/pinctrl/qcom/pinctrl-spmi-mpp",
        out = "pinctrl-spmi-mpp.ko",
        config = "CONFIG_PINCTRL_QCOM_SPMI_PMIC",
        srcs = [
            # do not sort
            "drivers/pinctrl/qcom/pinctrl-spmi-mpp.c",
        ],
    )

    registry.register(
        name = "drivers/pinctrl/qcom/pinctrl-sun",
        out = "pinctrl-sun.ko",
        config = "CONFIG_PINCTRL_SUN",
        srcs = [
            # do not sort
            "drivers/pinctrl/qcom/pinctrl-msm.h",
            "drivers/pinctrl/qcom/pinctrl-sun.c",
        ],
        deps = [
            # do not sort
            "drivers/pinctrl/qcom/pinctrl-msm",
            "drivers/firmware/qcom/qcom-scm",
            "drivers/virt/gunyah/gh_rm_drv",
            "drivers/virt/gunyah/gh_msgq",
            "drivers/virt/gunyah/gh_dbl",
            "arch/arm64/gunyah/gh_arm_drv",
        ],
    )
