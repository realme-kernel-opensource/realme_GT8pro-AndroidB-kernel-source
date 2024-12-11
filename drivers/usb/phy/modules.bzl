def register_modules(registry):
    registry.register(
        name = "drivers/usb/phy/phy-generic",
        out = "phy-generic.ko",
        config = "CONFIG_NOP_USB_XCEIV",
        srcs = [
            # do not sort
            "drivers/usb/phy/phy-generic.c",
            "drivers/usb/phy/phy-generic.h",
        ],
    )

    registry.register(
        name = "drivers/usb/phy/phy-msm-m31-eusb2",
        out = "phy-msm-m31-eusb2.ko",
        config = "CONFIG_USB_M31_MSM_EUSB2_PHY",
        srcs = [
            # do not sort
            "drivers/usb/phy/phy-msm-m31-eusb2.c",
        ],
        deps = [
            # do not sort
            "drivers/usb/repeater/repeater",
            "drivers/firmware/qcom/qcom-scm",
            "drivers/virt/gunyah/gh_rm_drv",
            "drivers/virt/gunyah/gh_msgq",
            "drivers/virt/gunyah/gh_dbl",
            "arch/arm64/gunyah/gh_arm_drv",
        ],
    )

    registry.register(
        name = "drivers/usb/phy/phy-msm-ssusb-qmp",
        out = "phy-msm-ssusb-qmp.ko",
        config = "CONFIG_USB_MSM_SSPHY_QMP",
        srcs = [
            # do not sort
            "drivers/usb/phy/phy-msm-ssusb-qmp.c",
        ],
    )

    registry.register(
        name = "drivers/usb/phy/phy-qcom-emu",
        out = "phy-qcom-emu.ko",
        config = "CONFIG_USB_QCOM_EMU_PHY",
        srcs = [
            # do not sort
            "drivers/usb/phy/phy-qcom-emu.c",
        ],
    )
