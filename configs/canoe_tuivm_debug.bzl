load(":configs/canoe_tuivm.bzl", "canoe_tuivm_config")

canoe_tuivm_debug_config = canoe_tuivm_config | {
    "CONFIG_CMDLINE_FORCE": "n",
    "CONFIG_GH_VIRTIO_DEBUG": "y",
    "CONFIG_MSM_GPI_DMA_DEBUG": "y",
    "CONFIG_QCOM_VM_ALIVE_LOG_ENCRYPT": "n",
}
