load("@bazel_skylib//lib:paths.bzl", "paths")
load(
    "//build/kernel/kleaf:kernel.bzl",
    "ddk_module",
    "kernel_module_group",
    "kernel_modules_install",
)
load(":kleaf-scripts/defconfig_fragment.bzl", "define_defconfig_fragment")

def _generate_ddk_target(
        module_map,
        target_variant,
        config_fragment,
        base_kernel):
    native.alias(
        name = "{}_base_kernel".format(target_variant),
        actual = base_kernel,
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

    return [module.name for module in matched_configurations]

def create_module_registry():
    module_map = {}

    def register(
            name,
            srcs,
            out,
            config = None,
            conditional_srcs = None,
            deps = None,
            includes = None,
            **kwargs):
        """Register a module with the registry.

        Args:
          name: A unique name for the module.
            For example: drivers/firmware/qcom/qcom_scm
            Conventionally, we do not add ".ko" suffix
          srcs: A list of source and header files to compile the module.
            These sources are "module-private" and are not exported to dependent
            modules.
          out: Desired name of the ko
            For example: qcom_scm.ko
          config: A Kconfig symbol which needs to be enabled for this module to
            be compiled.
            Optional. If unspecified, the module will be built for every target.
            For example: CONFIG_QCOM_SCM
          conditional_srcs: A dictionary mapping Kconfig symbols to additional
            sources which will be compiled.
            Note that the entire module is already dependent on the `config`
            symbol, and do need to again specify the config symbol.
            Example:
                conditional_srcs = {
                    "CONFIG_QTEE_SHM_BRIDGE": {
                        True: [
                            # do not sort
                            "drivers/firmware/qcom/qtee_shmbridge.c",
                        ],
                    },
                },
          deps: List of dependent modules, including optional dependencies.
            Note that transitive dependencies do not need to be listed: If you
            only depend on module_foo, and module_foo depends on module_bar,
            you need only list module_foo. The initial scripts to create the
            modules.bzl did *not* simplify the deps list and (unnecessarily)
            included transitive dependencies.
            Example:
              deps = [
                # do not sort
                "arch/arm64/gunyah/gunyah_hypercall",
              ]
          includes: See ddk_module() documentation.
          **kwargs: Additional ddk_module() arguments. See ddk_module() documentation.
        """
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

    def define_modules(
            target_variant,
            config_fragment,
            base_kernel):
        """Define register modules for a target/variant.

        Creates the following rules:
          {target_variant}_all_modules - kernel_module_group of all enabled modules
          {target_variant}_base_kernel - alias to `base_kernel`
          {target_variant}_config - ddk_config from the config_fragment and base_kernel
          {target_variant}/{module_name} - ddk_module for the target/variant

        Args:
            target_variant: Base name of the target
            config_fragment: A dictionary containg Kconfig symbols and their values.
              Analogous to a defconfig fragment, but as a starlark dictionary.
              See the files under configs/
            base_kernel: A kernel_build() to base the module build.

        Returns:
            The list of all enabled modules *without* the target_variant/ prefix.
            e.g. ["drivers/firmware/qcom/qcom_scm"]
        """
        return _generate_ddk_target(
            module_map,
            target_variant,
            config_fragment,
            base_kernel,
        )

    return struct(
        module_map = module_map,
        register = register,
        get = module_map.get,
        define_modules = define_modules,
    )
