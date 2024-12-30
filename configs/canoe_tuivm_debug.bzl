load(":configs/canoe_tuivm.bzl", "canoe_tuivm_config")

canoe_tuivm_debug_config = canoe_tuivm_config | {
    "CONFIG_CMDLINE_FORCE": "n",
    "CONFIG_MSM_GPI_DMA_DEBUG": "y",
}
