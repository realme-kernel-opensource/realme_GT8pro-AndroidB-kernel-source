load(":drivers/firmware/qcom/si_core/modules.bzl", register_si_core = "register_modules")

def register_modules(registry):
    register_si_core(registry)

    registry.register(
        name = "drivers/firmware/qcom/qcom-scm",
        out = "qcom-scm.ko",
        config = "CONFIG_QCOM_SCM",
        srcs = [
            # do not sort
            "drivers/firmware/qcom/qcom_scm-legacy.c",
            "drivers/firmware/qcom/qcom_scm-smc.c",
            "drivers/firmware/qcom/qcom_scm.c",
            "drivers/firmware/qcom/qcom_tzmem.h",
            "drivers/firmware/qcom/qcom_scm.h",
            "drivers/firmware/qcom/qtee_shmbridge_internal.h",
        ],
        conditional_srcs = {
            "CONFIG_QTEE_SHM_BRIDGE": {
                True: [
                    # do not sort
                    "drivers/firmware/qcom/qtee_shmbridge.c",
                ],
            },
        },
        deps = [
            # do not sort
            "drivers/firmware/qcom/qcom_tzmem",
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
        name = "drivers/firmware/qcom/qcom_tzmem",
        out = "qcom_tzmem.ko",
        config = "CONFIG_QCOM_TZMEM",
        srcs = [
            # do not sort
            "drivers/firmware/qcom/qcom_tzmem.c",
            "drivers/firmware/qcom/qcom_tzmem.h",
        ],
        deps = [
            # do not sort
            "drivers/virt/gunyah/gh_rm_drv",
            "drivers/virt/gunyah/gunyah_rsc_mgr",
            "drivers/virt/gunyah/gunyah_platform_hooks",
            "arch/arm64/gunyah/gunyah_hypercall",
            "drivers/virt/gunyah/gh_msgq",
            "drivers/virt/gunyah/gh_dbl",
            "arch/arm64/gunyah/gh_arm_drv",
        ],
    )
