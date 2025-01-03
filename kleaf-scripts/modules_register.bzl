load("@bazel_skylib//lib:paths.bzl", "paths")
load("//build/bazel_common_rules/dist:dist.bzl", "copy_to_dist_dir")
load(
    "//build/kernel/kleaf:kernel.bzl",
    "ddk_headers",
    "ddk_module",
    "kernel_compile_commands",
    "kernel_module_group",
    "kernel_modules_install",
)
load(":kleaf-scripts/defconfig_fragment.bzl", "define_defconfig_fragment")

def _generate_ddk_target(module_map, target_variant, config, config_fragment, base_kernel):
    native.alias(
        name = "{}_base_kernel".format(target_variant),
        actual = base_kernel,
        visibility = ["//visibility:public"],
    )

    #alias for base defconfig
    #in case of gki builds this will be common:arch/arm64/config/gki_defconfig
    #in case of NON_gki builds this will be msm-kernel:arch/arm64/config/generic_vmdefconfig
    native.alias(
        name = "{}_base_config".format(target_variant),
        actual = config,
        visibility = ["//visibility:public"],
    )

    define_defconfig_fragment(
        name = "{}_defconfig".format(target_variant),
        out = "{}.config".format(target_variant),
        config = config_fragment,
    )

    matched_configurations = []
    phony_configurations = []
    module_names = {}

    for config, modules in module_map.items():
        for obj in modules:
            if config_fragment.get(config) in ["y", "m"]:
                matched_configurations.append(obj)
                module_names[obj.name] = "{}/{}".format(target_variant, obj.name)
            else:
                phony_configurations.append(obj)

    for module in matched_configurations:
        deps = [":{}".format(module_names.get(dep)) for dep in module.deps if module_names.get(dep)]
        src_hdrs = [src for src in module.srcs if src.endswith(".h")]
        includes = (module.includes or []) + {paths.dirname(hdr): "" for hdr in src_hdrs}.keys()

        ddk_module(
            name = "{}/{}".format(target_variant, module.name),
            out = module.out,
            srcs = module.srcs,
            conditional_srcs = module.conditional_srcs,
            deps = deps + [
                # do not sort
                ":additional_msm_headers",
                "//common:all_headers",
            ],
            includes = includes,
            kernel_build = ":{}_base_kernel".format(target_variant),
            defconfig = ":{}_defconfig".format(target_variant),
            kconfig = ":kconfig.msm.generated",
            visibility = ["//visibility:public"],
            **module.extra_args
        )

    for module in phony_configurations:
        native.alias(
            name = "{}/{}".format(target_variant, module.name),
            actual = ":all_headers",
            visibility = ["//visibility:public"],
        )
    kernel_module_group(
        name = "{}_all_modules".format(target_variant),
        srcs = module_names.values(),
    )

    kernel_modules_install(
        name = "{}_modules_install".format(target_variant),
        kernel_modules = [":{}_all_modules".format(target_variant)],
        outs = ["modules.dep"],
    )

    kernel_compile_commands(
        name = "{}_compile_commands".format(target_variant),
        deps = [":{}_all_modules".format(target_variant)],
    )

    return [module.name for module in matched_configurations]

def create_module_registry():
    module_map = {}

    def register(name, srcs, out, config = None, conditional_srcs = None, deps = None, includes = None, **kwargs):
        if not module_map.get(config):
            module_map[config] = []
        module_map[config].append(struct(
            name = name,
            srcs = srcs,
            out = out,
            config = config,
            conditional_srcs = conditional_srcs,
            deps = deps or [],
            includes = includes,
            extra_args = kwargs,
        ))

    def define_modules(target_variant, config, config_fragment, base_kernel):
        return _generate_ddk_target(module_map, target_variant, config, config_fragment, base_kernel)

    return struct(
        module_map = module_map,
        register = register,
        get = module_map.get,
        define_modules = define_modules,
    )
