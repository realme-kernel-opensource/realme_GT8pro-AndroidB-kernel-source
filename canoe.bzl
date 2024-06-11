load(":target_variants.bzl", "la_variants")
load(":msm_kernel_la.bzl", "define_msm_la")
load(":image_opts.bzl", "boot_image_opts")

target_name = "canoe"

def define_canoe():
    _canoe_in_tree_modules = [
        # keep sorted
        "drivers/bus/mhi/devices/mhi_dev_satellite.ko",
        "drivers/bus/mhi/devices/mhi_dev_uci.ko",
        "drivers/bus/mhi/host/mhi.ko",
        "drivers/clk/qcom/clk-dummy.ko",
        "drivers/clk/qcom/clk-qcom.ko",
        "drivers/clk/qcom/gcc-canoe.ko",
        "drivers/clk/qcom/gdsc-regulator.ko",
        "drivers/cpuidle/governors/qcom_lpm.ko",
        "drivers/firmware/qcom/qcom-scm.ko",
        "drivers/hwspinlock/qcom_hwspinlock.ko",
        "drivers/irqchip/qcom-pdc.ko",
        "drivers/pci/controller/pci-msm-drv.ko",
        "drivers/pinctrl/qcom/pinctrl-canoe.ko",
        "drivers/pinctrl/qcom/pinctrl-msm.ko",
        "drivers/power/reset/qcom-dload-mode.ko",
        "drivers/power/reset/qcom-reboot-reason.ko",
        "drivers/regulator/debug-regulator.ko",
        "drivers/regulator/proxy-consumer.ko",
        "drivers/regulator/qti-fixed-regulator.ko",
        "drivers/regulator/stub-regulator.ko",
        "drivers/soc/qcom/boot_stats.ko",
        "drivers/soc/qcom/cmd-db.ko",
        "drivers/soc/qcom/crm-v2.ko",
        "drivers/soc/qcom/eud.ko",
        "drivers/soc/qcom/qcom_ramdump.ko",
        "drivers/soc/qcom/qcom_rpmh.ko",
        "drivers/usb/dwc3/dwc3-msm.ko",
        "drivers/usb/gadget/function/usb_f_ccid.ko",
        "drivers/usb/gadget/function/usb_f_cdev.ko",
        "drivers/usb/gadget/function/usb_f_gsi.ko",
        "drivers/usb/gadget/function/usb_f_qdss.ko",
        "drivers/usb/phy/phy-generic.ko",
        "drivers/usb/phy/phy-qcom-emu.ko",
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

    kernel_vendor_cmdline_extras = ["bootconfig"]

    for variant in la_variants:
        board_kernel_cmdline_extras = []
        board_bootconfig_extras = []

        if variant == "consolidate":
            mod_list = _canoe_consolidate_in_tree_modules
            board_bootconfig_extras += ["androidboot.serialconsole=1"]
            board_kernel_cmdline_extras += [
                # do not sort
                "console=ttyMSM0,115200n8",
                "qcom_geni_serial.con_enabled=1",
                "earlycon",
            ]
            kernel_vendor_cmdline_extras += [
                # do not sort
                "console=ttyMSM0,115200n8",
                "qcom_geni_serial.con_enabled=1",
                "earlycon",
            ]
        else:
            mod_list = _canoe_in_tree_modules
            board_kernel_cmdline_extras += ["nosoftlockup console=ttynull qcom_geni_serial.con_enabled=0"]
            kernel_vendor_cmdline_extras += ["nosoftlockup console=ttynull qcom_geni_serial.con_enabled=0"]
            board_bootconfig_extras += ["androidboot.serialconsole=0"]

        define_msm_la(
            msm_target = target_name,
            variant = variant,
            in_tree_module_list = mod_list,
            boot_image_opts = boot_image_opts(
                earlycon_addr = "qcom_geni,0x00a9c000",
                kernel_vendor_cmdline_extras = kernel_vendor_cmdline_extras,
                board_kernel_cmdline_extras = board_kernel_cmdline_extras,
                board_bootconfig_extras = board_bootconfig_extras,
            ),
        )
