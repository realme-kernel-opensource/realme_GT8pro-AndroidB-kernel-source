def register_modules(registry):
    registry.register(
        name = "drivers/net/pcs/pcs-xpcs",
        out = "pcs-xpcs.ko",
        config = "CONFIG_PCS_XPCS",
        srcs = [
            # do not sort
            "drivers/net/pcs/pcs-xpcs-plat.c",
            "drivers/net/pcs/pcs-xpcs-nxp.c",
            "drivers/net/pcs/pcs-xpcs-wx.c",
            "drivers/net/pcs/pcs-xpcs.c",
            "drivers/net/pcs/pcs-xpcs.h",
        ],
    )
