load("//build/kernel/kleaf:kernel.bzl", "kernel_module_group")
load(":configs/canoe_consolidate.bzl", "canoe_consolidate_config")
load(":configs/canoe_perf.bzl", "canoe_perf_config")
load(":configs/canoe_tuivm.bzl", "canoe_tuivm_config")
load(":kleaf-scripts/android_build.bzl", "define_typical_android_build")
load(":kleaf-scripts/image_opts.bzl", "boot_image_opts")
load(":kleaf-scripts/vm_build.bzl", "define_typical_vm_build")
load(":target_variants.bzl", "la_variants")

target_name = "canoe"

def define_canoe():
    kernel_vendor_cmdline_extras = ["bootconfig"]

    for variant in la_variants:
        board_kernel_cmdline_extras = []
        board_bootconfig_extras = []

        if variant == "consolidate":
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

            consolidate_build_img_opts = boot_image_opts(
                earlycon_addr = "qcom_geni,0x00a9c000",
                kernel_vendor_cmdline_extras = kernel_vendor_cmdline_extras,
                board_kernel_cmdline_extras = board_kernel_cmdline_extras,
                board_bootconfig_extras = board_bootconfig_extras,
            )

        else:
            board_kernel_cmdline_extras += ["nosoftlockup console=ttynull qcom_geni_serial.con_enabled=0"]
            kernel_vendor_cmdline_extras += ["nosoftlockup console=ttynull qcom_geni_serial.con_enabled=0"]
            board_bootconfig_extras += ["androidboot.serialconsole=0"]

            perf_build_img_opts = boot_image_opts(
                earlycon_addr = "qcom_geni,0x00a9c000",
                kernel_vendor_cmdline_extras = kernel_vendor_cmdline_extras,
                board_kernel_cmdline_extras = board_kernel_cmdline_extras,
                board_bootconfig_extras = board_bootconfig_extras,
            )

    define_typical_android_build(
        name = "canoe",
        consolidate_config = canoe_perf_config | canoe_consolidate_config,
        perf_config = canoe_perf_config,
        consolidate_build_img_opts = consolidate_build_img_opts,
        perf_build_img_opts = perf_build_img_opts,
    )

def define_canoe_tuivm():
    define_typical_vm_build(
        name = "canoe-tuivm",
        config = canoe_tuivm_config,
        debug_config = canoe_tuivm_config,  # TODO: canoe_tuivm_debug_config
        dtb_target = "canoe-tuivm",
    )

def define_canoe_oemvm():
    define_typical_vm_build(
        name = "canoe-oemvm",
        config = canoe_tuivm_config,
        debug_config = canoe_tuivm_config,  # TODO: canoe_tuivm_debug_config
        dtb_target = "canoe-oemvm",
    )
