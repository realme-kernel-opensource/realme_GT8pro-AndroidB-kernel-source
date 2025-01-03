load(
    "//build:msm_kernel_extensions.bzl",
    "define_extras",
)
load(
    "//build/kernel/kleaf:kernel.bzl",
    "kernel_build",
    "kernel_build_config",
)
load(":kleaf-scripts/image_opts.bzl", "vm_image_opts")

def define_qc_core_header_config(name):
    native.genrule(
        name = name,
        outs = ["build.config.qc_core.header"],
        cmd_bash = "echo \"KERNEL_DIR=common\" > $@",
    )
    native.genrule(
        name = "signing_key",
        srcs = ["//soc-repo:certs/qcom_x509.genkey"],
        outs = ["signing_key.pem"],
        tools = ["//prebuilts/build-tools:linux-x86/bin/openssl"],
        cmd_bash = """
          $(location //prebuilts/build-tools:linux-x86/bin/openssl) req -new -nodes -utf8 -sha256 -days 36500 \
            -batch -x509 -config $(location //soc-repo:certs/qcom_x509.genkey) \
            -outform PEM -out "$@" -keyout "$@"
        """,
    )

    native.genrule(
        name = "verity_key",
        srcs = ["//soc-repo:certs/qcom_x509.genkey"],
        outs = ["verity_cert.pem", "verity_key.pem"],
        tools = ["//prebuilts/build-tools:linux-x86/bin/openssl"],
        cmd_bash = """
          $(location //prebuilts/build-tools:linux-x86/bin/openssl) req -new -nodes -utf8 -newkey rsa:1024 -days 36500 \
            -batch -x509 -config $(location //soc-repo:certs/qcom_x509.genkey) \
            -outform PEM -out $(location verity_cert.pem) -keyout $(location verity_key.pem)
        """,
    )

def _gen_qc_core_build_config(name, defconfig):
    """Generates a generic VM build config."""
    rule_name = "build.config." + name
    out_file_name = rule_name + ".generated"
    vm_opts = vm_image_opts()
    native.genrule(
        name = rule_name,
        srcs = [defconfig],
        outs = [out_file_name],
        cmd_bash = """
        cat << 'EOF' > $@
# No modules
KERNEL_DIR="common"
IN_KERNEL_MODULES=
DTC_FLAGS="-@"
export DTC_INCLUDE=$${{ROOT_DIR}}/msm-kernel/include
LLVM=1
export CLANG_PREBUILT_BIN=prebuilts/clang/host/linux-x86/clang-r522817/bin
export BUILDTOOLS_PREBUILT_BIN=build/kernel/build-tools/path/linux-x86
VARIANTS=(debug-defconfig defconfig)
VARIANT=defconfig
MSM_ARCH=sun_tuivm
ARCH=arm64
PREFERRED_USERSPACE={userspace}
VM_DTB_IMG_CREATE={vm_dtb_img_create}
KERNEL_OFFSET={kernel_offset}
DTB_OFFSET={dtb_offset}
RAMDISK_OFFSET={ramdisk_offset}
CMDLINE_CPIO_OFFSET={cmdline_cpio_offset}
VM_SIZE_EXT4={vm_size_ext4}
DUMMY_IMG_SIZE={dummy_img_size}

EOF
""".format(
            userspace = vm_opts.preferred_usespace,
            vm_dtb_img_create = vm_opts.vm_dtb_img_create,
            kernel_offset = vm_opts.kernel_offset,
            dtb_offset = vm_opts.dtb_offset,
            ramdisk_offset = vm_opts.ramdisk_offset,
            cmdline_cpio_offset = vm_opts.cmdline_cpio_offset,
            vm_size_ext4 = vm_opts.vm_size_ext4,
            dummy_img_size = vm_opts.dummy_img_size,
        ),
    )

    return ":" + rule_name

def define_qc_core_kernel(name, defconfig, defconfig_fragments = None):
    kernel_build_config(
        name = name + "_build_config",
        srcs = [
            # do not sort
            ":qc_core_kernel_header_config",
            "//common:build.config.aarch64",
            _gen_qc_core_build_config(name, defconfig),
        ],
    )

    kernel_build(
        name = name,
        srcs = ["//common:kernel_aarch64_sources"],
        outs = [
            # keep sorted
            "Image",
            "Module.symvers",
            "System.map",
            "certs/signing_key.x509",
            "gen_init_cpio",
            "modules.builtin",
            "modules.builtin.modinfo",
            "scripts/sign-file",
            "vmlinux",
            "vmlinux.symvers",
        ],
        build_config = name + "_build_config",
        defconfig = defconfig,
        keep_module_symvers = True,
        module_signing_key = ":signing_key",
        strip_modules = True,
        post_defconfig_fragments = defconfig_fragments,
        make_goals = [
            "Image",
            "modules",
        ],
        system_trusted_key = ":verity_cert.pem",
    )

def define_qtvm():
    define_qc_core_header_config("qc_core_kernel_header_config")
    define_qc_core_kernel("kernel_aarch64_qtvm", "arch/arm64/configs/generic_vm_defconfig")
    define_qc_core_kernel("kernel_aarch64_qtvm_debug", "arch/arm64/configs/generic_vm_defconfig", defconfig_fragments = ["arch/arm64/configs/generic_vm_debug.fragment"])
