load("@bazel_skylib//rules:write_file.bzl", "write_file")
load("//build:msm_kernel_extensions.bzl", "get_gki_ramdisk_prebuilt_binary", "get_vendor_ramdisk_binaries")
load("//build/bazel_common_rules/dist:dist.bzl", "copy_to_dist_dir")
load(
    "//build/kernel/kleaf:kernel.bzl",
    "kernel_build",
    "kernel_build_config",
    "kernel_images",
    "merged_kernel_uapi_headers",
    "super_image",
    "unsparsed_image",
)
load(":kleaf-scripts/abl.bzl", "define_abl_dist")
load(":kleaf-scripts/avb_boot_img.bzl", "avb_sign_boot_image")
load(":kleaf-scripts/consolidate.bzl", "define_consolidated_kernel")
load(":kleaf-scripts/dtbs.bzl", "define_qcom_dtbs")
load(":kleaf-scripts/modules_unprotected.bzl", "get_unprotected_vendor_modules_list")
load(":kleaf-scripts/msm_dtc.bzl", "define_dtc_dist")
load(":qcom_modules.bzl", "registry")

def define_common_android_rules():
    write_file(
        name = "qcom_la_image_config",
        out = "la.image.config",
        content = [
            "KERNEL_DIR=common",
            "SOC_DIR=soc-repo",
            "KERNEL_BINARY=Image",
            "DO_NOT_STRIP_MODULES=0",
            "",
        ],
    )

    kernel_build_config(
        name = "build.config.qcom.la.images",
        srcs = [
            # do not sort
            ":qcom_la_image_config",
            "//common:build.config.gki.aarch64",
        ],
    )

    define_consolidated_kernel()

def define_single_android_build(
        name,
        variant,
        config_fragment,
        base_kernel,
        dtb_target = None,
        build_img_opts = None,
        first_stage_modules = None):
    stem = "{}_{}".format(name, variant)
    config = "//common:arch/arm64/configs/gki_defconfig"
    modules = registry.define_modules(stem, config, config_fragment, base_kernel)

    native.genrule(
        name = "{}_vendor_dlkm_modules_list_generated".format(stem),
        srcs = [],
        outs = ["modules.list.vendor_dlkm.{}".format(stem)],
        cmd_bash = """
          touch "$@"
          for module in {mod_list}; do
            basename "$$module".ko >> "$@"
          done
        """.format(mod_list = " ".join(modules)),
    )

    if dtb_target:
        dtb_list, dtbo_list = define_qcom_dtbs(
            stem = stem,
            target = dtb_target,
        )
    else:
        dtb_list = None
        dtbo_list = None

    native.alias(
        name = "{}_abl".format(stem),
        actual = "//bootable/bootloader/edk2:{}_abl".format(stem),
    )

    kernel_build_config(
        name = "{}_build_config".format(stem),
        srcs = [
            # do not sort
            ":qcom_la_image_config",
            "//common:build.config.gki.aarch64",
            "build.config.msm.common",
            "build.config.msm.perf",
        ],
    )

    if build_img_opts != None:
        board_cmdline_extras = " ".join(build_img_opts.board_kernel_cmdline_extras)

        if board_cmdline_extras:
            native.genrule(
                name = "{}_extra_cmdline".format(stem),
                outs = ["board_extra_cmdline_{}".format(stem)],
                cmd_bash = """
                    echo {} > "$@"
                """.format(board_cmdline_extras),
            )

        board_bc_extras = " ".join(build_img_opts.board_bootconfig_extras)

        if board_bc_extras:
            native.genrule(
                name = "{}_extra_bootconfig".format(stem),
                outs = ["board_extra_bootconfig_{}".format(stem)],
                cmd_bash = """
                    echo {} > "$@"
                """.format(board_bc_extras),
            )

    kernel_images(
        name = "{}_images".format(stem),
        kernel_modules_install = ":{}_modules_install".format(stem),
        kernel_build = "{}_dtb_build".format(stem),
        base_kernel_images = "{}_images".format(base_kernel),
        dtbo_srcs = [":{}_dtb_build/{}".format(stem, dtbo) for dtbo in dtbo_list] if dtbo_list else None,
        gki_ramdisk_prebuilt_binary = get_gki_ramdisk_prebuilt_binary(),
        build_vendor_boot = True,
        build_vendor_kernel_boot = False,
        build_initramfs = True,
        build_dtbo = True,
        build_vendor_dlkm = True,
        modules_list = "modules-lists/modules.list.msm.{}".format(name),
        vendor_dlkm_modules_list = ":{}_vendor_dlkm_modules_list_generated".format(stem),
        system_dlkm_modules_blocklist = "modules-lists/modules.systemdlkm_blocklist.msm.{}".format(name),
        vendor_dlkm_modules_blocklist = "modules-lists/modules.vendor_blocklist.msm.{}".format(name),
        vendor_ramdisk_binaries = get_vendor_ramdisk_binaries(stem),
        deps = [
            "modules-lists/modules.list.msm.{}".format(name),
            "modules-lists/modules.vendor_blocklist.msm.{}".format(name),
            "modules-lists/modules.systemdlkm_blocklist.msm.{}".format(name),
        ],
    )

    avb_sign_boot_image(
        name = "{}_avb_sign_boot_image".format(stem),
        artifacts = "{}_gki_artifacts".format(base_kernel),
        avbtool = "//prebuilts/kernel-build-tools:linux-x86/bin/avbtool",
        key = "//tools/mkbootimg:gki/testdata/testkey_rsa4096.pem",
        props = [
            "com.android.build.boot.os_version:13",
            "com.android.build.boot.security_patch:2023-05-05",
        ],
    )

    native.filegroup(
        name = "{}_system_dlkm_image_file".format(stem),
        srcs = ["{}_images".format(base_kernel)],
        output_group = "system_dlkm.flatten.ext4.img",
    )

    native.filegroup(
        name = "{}_vendor_dlkm_image_file".format(stem),
        srcs = [":{}_images".format(stem)],
        output_group = "vendor_dlkm.img",
    )

    super_image(
        name = "{}_super_image".format(stem),
        system_dlkm_image = ":{}_system_dlkm_image_file".format(stem),
        vendor_dlkm_image = ":{}_vendor_dlkm_image_file".format(stem),
    )

    unsparsed_image(
        name = "{}_unsparsed_image".format(stem),
        src = "{}_super_image".format(stem),
        out = "super_unsparsed.img",
    )

    native.genrule(
        name = "{}_merge_msm_uapi_headers".format(stem),
        srcs = [
            # do not sort
            ":{}_merged_kernel_uapi_headers".format(stem),
            "msm_uapi_headers",
        ],
        outs = ["{}_kernel-uapi-headers.tar.gz".format(stem)],
        cmd_bash = """
                    mkdir -p intermediate_dir
                    for file in $(SRCS)
                    do
                    tar xf $$file -C intermediate_dir
                    done
                    tar czf $(OUTS) -C intermediate_dir usr/
                    rm -rf intermediate_dir
        """,
    )

    merged_kernel_uapi_headers(
        name = "{}_merged_kernel_uapi_headers".format(stem),
        kernel_build = ":{}_dtb_build".format(stem),
    )

    native.genrule(
        name = "{}_tar_kernel_headers".format(stem),
        srcs = [
            # do not sort
            ":additional_msm_headers",
            "//common:all_headers",
        ],
        outs = ["{}_kernel-headers.tar.gz".format(stem)],
        cmd_bash = """
                    mkdir -p kernel-headers
                    for file in $(SRCS)
                    do
                    cp -r --parents $$file kernel-headers
                    done
                    tar czf $(OUTS) kernel-headers
                    rm -rf kernel-headers
        """,
    )

    dist_data = [
        "{}_gki_artifacts".format(base_kernel),
        ":{}_modules_install".format(stem),
        "{}_dtb_build".format(stem),
        ":{}_images".format(stem),
        "{}_images".format(base_kernel),
        "{}_super_image".format(stem),
        "{}_unsparsed_image".format(stem),
        "{}_avb_sign_boot_image".format(stem),
        "{}_merge_msm_uapi_headers".format(stem),
        "{}_dtb_build_config".format(stem),
        "{}_tar_kernel_headers".format(stem),
    ] + [
        ":{}/{}".format(stem, module) for module in modules
    ]

    vendor_dlkm_module_unprotected_list = get_unprotected_vendor_modules_list(stem)
    vendor_unprotected_dlkm = " ".join(vendor_dlkm_module_unprotected_list)
    if vendor_unprotected_dlkm:
        write_file(
            name = "{}_vendor_dlkm_module_unprotectedlist".format(stem),
            out = "{}/vendor_dlkm.modules.unprotectedlist".format(stem),
            content = [vendor_unprotected_dlkm, ""],
        )
        dist_data.extend(["{}_vendor_dlkm_module_unprotectedlist".format(stem)])

    if build_img_opts != None:
        board_cmdline_extras = " ".join(build_img_opts.board_kernel_cmdline_extras)

        if board_cmdline_extras:
            dist_data.append("{}_extra_cmdline".format(stem))

        board_bc_extras = " ".join(build_img_opts.board_bootconfig_extras)

        if board_bc_extras:
            dist_data.append("{}_extra_bootconfig".format(stem))

    copy_to_dist_dir(
        name = "{}_dist".format(stem),
        data = dist_data,
        dist_dir = "out/msm-kernel-{}-{}/dist".format(name, variant),
        flat = True,
        log = "info",
        allow_duplicate_filenames = True,
        mode_overrides = {
            # do not sort
            "**/*.elf": "755",
            "**/vmlinux": "755",
            "**/Image": "755",
            "**/*.dtb*": "755",
            "**/LinuxLoader*": "755",
            "**/*": "644",
        },
    )

    define_abl_dist(stem, name, variant)

    define_dtc_dist(stem, name, variant)

def define_android_build(
        name,
        configs,
        consolidate_build_img_opts,
        perf_build_img_opts,
        **kwargs):
    for (variant, options) in configs.items():
        if variant == "perf":
            build_img_opts = perf_build_img_opts
        else:
            build_img_opts = consolidate_build_img_opts
        define_single_android_build(
            name = name,
            variant = variant,
            build_img_opts = build_img_opts,
            **(options | kwargs)
        )

def define_typical_android_build(
        name,
        perf_config,
        consolidate_config,
        perf_kwargs = None,
        consolidate_kwargs = None,
        consolidate_build_img_opts = None,
        perf_build_img_opts = None,
        **kwargs):
    """Define a typical Android build target.

    Defines a typical Android build with two variants: one optimized for
    performance and another that has uses the debug(consolidate) kernel.

    Args:
      name: The name of this target.
      perf_config: The configuration to use when building the performance (GKI)
        build.
      consolidate_config: The configuration to use when building the
        consolidate (debug) build.
      perf_kwargs: Additional keyword arguments to pass to
        `define_single_android_build` when building the performance variant.
      consolidate_kwargs: Additional keyword arguments to pass to
        `define_single_android_build` when building the consolidate variant.
      **kwargs: Additional keyword arguments to pass to
        `define_single_android_build` for all variants.
    """

    if perf_kwargs == None:
        perf_kwargs = dict()
    if consolidate_kwargs == None:
        consolidate_kwargs = dict()

    define_android_build(
        name,
        configs = {
            "perf": {
                "config_fragment": perf_config,
                "base_kernel": "//common:kernel_aarch64",
            } | perf_kwargs,
            "consolidate": {
                "config_fragment": consolidate_config,
                "base_kernel": "//soc-repo:kernel_aarch64_consolidate",
            } | consolidate_kwargs,
        },
        dtb_target = name,
        consolidate_build_img_opts = consolidate_build_img_opts,
        perf_build_img_opts = perf_build_img_opts,
        **kwargs
    )
