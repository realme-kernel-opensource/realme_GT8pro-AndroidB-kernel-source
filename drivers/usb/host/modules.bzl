def register_modules(registry):
    registry.register(
        name = "drivers/usb/host/xhci-sideband",
        out = "xhci-sideband.ko",
        config = "CONFIG_USB_XHCI_SIDEBAND",
        srcs = [
            # do not sort
            "drivers/usb/host/xhci-sideband.c",
        ],
    )
