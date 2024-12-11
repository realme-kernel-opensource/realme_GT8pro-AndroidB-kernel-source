def register_modules(registry):
    registry.register(
        name = "drivers/virt/gunyah/gh_ctrl",
        out = "gh_ctrl.ko",
        config = "CONFIG_GH_CTRL",
        srcs = [
            # do not sort
            "drivers/virt/gunyah/gh_ctrl.c",
            "drivers/virt/gunyah/hcall_ctrl.h",
        ],
    )

    registry.register(
        name = "drivers/virt/gunyah/gh_dbl",
        out = "gh_dbl.ko",
        config = "CONFIG_GH_DBL",
        srcs = [
            # do not sort
            "drivers/virt/gunyah/gh_dbl.c",
            "drivers/virt/gunyah/hcall_dbl.h",
        ],
    )

    registry.register(
        name = "drivers/virt/gunyah/gh_irq_lend",
        out = "gh_irq_lend.ko",
        config = "CONFIG_GH_IRQ_LEND",
        srcs = [
            # do not sort
            "drivers/virt/gunyah/gh_irq_lend.c",
            "drivers/virt/gunyah/gh_rm_drv_private.h",
            "drivers/virt/gunyah/rsc_mgr.h",
        ],
        deps = [
            # do not sort
            "drivers/virt/gunyah/gunyah_loader",
            "drivers/soc/qcom/mdt_loader",
            "drivers/soc/qcom/secure_buffer",
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
        name = "drivers/virt/gunyah/gh_mem_notifier",
        out = "gh_mem_notifier.ko",
        config = "CONFIG_GH_MEM_NOTIFIER",
        srcs = [
            # do not sort
            "drivers/virt/gunyah/gh_mem_notifier.c",
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

    registry.register(
        name = "drivers/virt/gunyah/gh_msgq",
        out = "gh_msgq.ko",
        config = "CONFIG_GH_MSGQ",
        srcs = [
            # do not sort
            "drivers/virt/gunyah/gh_msgq.c",
            "drivers/virt/gunyah/hcall_msgq.h",
        ],
    )

    registry.register(
        name = "drivers/virt/gunyah/gh_panic_notifier",
        out = "gh_panic_notifier.ko",
        config = "CONFIG_GH_PANIC_NOTIFIER",
        srcs = [
            # do not sort
            "drivers/virt/gunyah/gh_panic_notifier.c",
        ],
        deps = [
            # do not sort
            "drivers/virt/gunyah/gunyah_loader",
            "drivers/soc/qcom/mdt_loader",
            "drivers/soc/qcom/secure_buffer",
            "drivers/firmware/qcom/qcom-scm",
            "drivers/virt/gunyah/gh_rm_drv",
            "drivers/virt/gunyah/gh_msgq",
            "drivers/virt/gunyah/gh_dbl",
            "arch/arm64/gunyah/gh_arm_drv",
        ],
    )

    registry.register(
        name = "drivers/virt/gunyah/gh_rm_booster",
        out = "gh_rm_booster.ko",
        config = "CONFIG_GH_RM_BOOSTER",
        srcs = [
            # do not sort
            "drivers/virt/gunyah/gh_rm_booster.c",
            "drivers/virt/gunyah/gh_rm_booster.h",
            "drivers/virt/gunyah/gh_rm_drv_private.h",
            "drivers/virt/gunyah/rsc_mgr.h",
        ],
        deps = [
            # do not sort
            "drivers/virt/gunyah/gh_rm_drv",
            "drivers/virt/gunyah/gh_msgq",
            "drivers/virt/gunyah/gh_dbl",
            "arch/arm64/gunyah/gh_arm_drv",
        ],
    )

    registry.register(
        name = "drivers/virt/gunyah/gh_rm_drv",
        out = "gh_rm_drv.ko",
        config = "CONFIG_GH_RM_DRV",
        srcs = [
            # do not sort
            "drivers/virt/gunyah/gh_guest_pops.h",
            "drivers/virt/gunyah/gh_rm_core.c",
            "drivers/virt/gunyah/gh_rm_drv_private.h",
            "drivers/virt/gunyah/gh_rm_iface.c",
            "drivers/virt/gunyah/rsc_mgr.h",
        ],
        deps = [
            # do not sort
            "drivers/virt/gunyah/gunyah_rsc_mgr",
            "drivers/virt/gunyah/gunyah_platform_hooks",
            "arch/arm64/gunyah/gunyah_hypercall",
            "drivers/virt/gunyah/gh_msgq",
            "drivers/virt/gunyah/gh_dbl",
            "arch/arm64/gunyah/gh_arm_drv",
        ],
    )

    registry.register(
        name = "drivers/virt/gunyah/gh_virt_wdt",
        out = "gh_virt_wdt.ko",
        config = "CONFIG_GH_VIRT_WATCHDOG",
        srcs = [
            # do not sort
            "drivers/virt/gunyah/gh_virt_wdt.c",
        ],
        deps = [
            # do not sort
            "drivers/soc/qcom/qcom_wdt_core",
            "drivers/soc/qcom/minidump",
            "drivers/soc/qcom/smem",
            "drivers/soc/qcom/debug_symbol",
            "drivers/dma-buf/heaps/qcom_dma_heaps",
            "drivers/iommu/msm_dma_iommu_mapping",
            "drivers/soc/qcom/mem_buf/mem_buf_dev",
            "drivers/soc/qcom/secure_buffer",
            "drivers/firmware/qcom/qcom-scm",
            "drivers/virt/gunyah/gh_rm_drv",
            "drivers/virt/gunyah/gh_msgq",
            "drivers/virt/gunyah/gh_dbl",
            "arch/arm64/gunyah/gh_arm_drv",
        ],
    )

    registry.register(
        name = "drivers/virt/gunyah/gunyah_ioeventfd",
        out = "gunyah_ioeventfd.ko",
        config = "CONFIG_GUNYAH_IOEVENTFD",
        srcs = [
            # do not sort
            "drivers/virt/gunyah/gunyah_ioeventfd.c",
        ],
        deps = [
            # do not sort
            "drivers/virt/gunyah/gunyah_rsc_mgr",
            "drivers/virt/gunyah/gunyah_platform_hooks",
            "arch/arm64/gunyah/gunyah_hypercall",
        ],
    )

    registry.register(
        name = "drivers/virt/gunyah/gunyah_irqfd",
        out = "gunyah_irqfd.ko",
        config = "CONFIG_GUNYAH_IRQFD",
        srcs = [
            # do not sort
            "drivers/virt/gunyah/gunyah_irqfd.c",
        ],
        deps = [
            # do not sort
            "drivers/virt/gunyah/gunyah_rsc_mgr",
            "drivers/virt/gunyah/gunyah_platform_hooks",
            "arch/arm64/gunyah/gunyah_hypercall",
        ],
    )

    registry.register(
        name = "drivers/virt/gunyah/gunyah",
        out = "gunyah.ko",
        config = "CONFIG_GUNYAH",
        srcs = [
            # do not sort
            "drivers/virt/gunyah/gunyah.c",
        ],
        deps = [
            # do not sort
            "arch/arm64/gunyah/gunyah_hypercall",
        ],
    )

    registry.register(
        name = "drivers/virt/gunyah/gunyah_loader",
        out = "gunyah_loader.ko",
        config = "CONFIG_GH_SECURE_VM_LOADER",
        srcs = [
            # do not sort
            "drivers/virt/gunyah/gh_main.c",
            "drivers/virt/gunyah/gh_private.h",
            "drivers/virt/gunyah/gh_proxy_sched.h",
            "drivers/virt/gunyah/gh_secure_vm_loader.h",
            "drivers/virt/gunyah/gh_secure_vm_virtio_backend.c",
            "drivers/virt/gunyah/gh_secure_vm_virtio_backend.h",
            "drivers/virt/gunyah/hcall_virtio.h",
        ],
        conditional_srcs = {
            "CONFIG_GH_SECURE_VM_LOADER": {
                True: [
                    # do not sort
                    "drivers/virt/gunyah/gh_secure_vm_loader.c",
                ],
            },
            "CONFIG_GH_PROXY_SCHED": {
                True: [
                    # do not sort
                    "drivers/virt/gunyah/gh_proxy_sched.c",
                    "drivers/virt/gunyah/gh_proxy_sched_trace.h",
                ],
            },
        },
        deps = [
            # do not sort
            "drivers/soc/qcom/mdt_loader",
            "drivers/soc/qcom/secure_buffer",
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
        name = "drivers/virt/gunyah/gunyah_platform_hooks",
        out = "gunyah_platform_hooks.ko",
        config = "CONFIG_GUNYAH_PLATFORM_HOOKS",
        srcs = [
            # do not sort
            "drivers/virt/gunyah/gunyah_platform_hooks.c",
            "drivers/virt/gunyah/rsc_mgr.h",
        ],
    )

    registry.register(
        name = "drivers/virt/gunyah/gunyah_qcom",
        out = "gunyah_qcom.ko",
        config = "CONFIG_GUNYAH_QCOM_PLATFORM",
        srcs = [
            # do not sort
            "drivers/virt/gunyah/gunyah_qcom.c",
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
        name = "drivers/virt/gunyah/gunyah_rsc_mgr",
        out = "gunyah_rsc_mgr.ko",
        config = "CONFIG_GUNYAH",
        srcs = [
            # do not sort
            "drivers/virt/gunyah/rsc_mgr.c",
            "drivers/virt/gunyah/rsc_mgr.h",
            "drivers/virt/gunyah/rsc_mgr_rpc.c",
            "drivers/virt/gunyah/vm_mgr.c",
            "drivers/virt/gunyah/vm_mgr.h",
            "drivers/virt/gunyah/vm_mgr_mem.c",
        ],
        deps = [
            # do not sort
            "drivers/virt/gunyah/gunyah_platform_hooks",
            "arch/arm64/gunyah/gunyah_hypercall",
        ],
    )

    registry.register(
        name = "drivers/virt/gunyah/gunyah_vcpu",
        out = "gunyah_vcpu.ko",
        config = "CONFIG_GUNYAH",
        srcs = [
            # do not sort
            "drivers/virt/gunyah/gunyah_vcpu.c",
            "drivers/virt/gunyah/rsc_mgr.h",
            "drivers/virt/gunyah/vm_mgr.h",
        ],
        deps = [
            # do not sort
            "drivers/virt/gunyah/gunyah_rsc_mgr",
            "drivers/virt/gunyah/gunyah_platform_hooks",
            "arch/arm64/gunyah/gunyah_hypercall",
        ],
    )
