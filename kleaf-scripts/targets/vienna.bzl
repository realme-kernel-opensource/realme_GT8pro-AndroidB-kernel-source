load(":configs/vienna_consolidate.bzl", "vienna_consolidate_config")
load(":configs/vienna_perf.bzl", "vienna_perf_config")
load(":kleaf-scripts/android_build.bzl", "define_typical_android_build")

target_name = "vienna"

def define_vienna():
    define_typical_android_build(
        name = "vienna",
        consolidate_config = vienna_perf_config | vienna_consolidate_config,
        perf_config = vienna_perf_config,
    )
