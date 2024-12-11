def register_modules(registry):
    registry.register(
        name = "drivers/phy/qualcomm/phy-qcom-ufs",
        out = "phy-qcom-ufs.ko",
        config = "CONFIG_PHY_QCOM_UFS",
        srcs = [
            # do not sort
            "drivers/phy/qualcomm/phy-qcom-ufs-i.h",
            "drivers/phy/qualcomm/phy-qcom-ufs.c",
        ],
    )

    registry.register(
        name = "drivers/phy/qualcomm/phy-qcom-ufs-qmp-v4-canoe",
        out = "phy-qcom-ufs-qmp-v4-canoe.ko",
        config = "CONFIG_PHY_QCOM_UFS_V4_CANOE",
        srcs = [
            # do not sort
            "drivers/phy/qualcomm/phy-qcom-ufs-i.h",
            "drivers/phy/qualcomm/phy-qcom-ufs-qmp-v4-canoe.c",
            "drivers/phy/qualcomm/phy-qcom-ufs-qmp-v4-canoe.h",
        ],
        deps = [
            # do not sort
            "drivers/phy/qualcomm/phy-qcom-ufs",
        ],
    )

    registry.register(
        name = "drivers/phy/qualcomm/phy-qcom-ufs-qmp-v4-sun",
        out = "phy-qcom-ufs-qmp-v4-sun.ko",
        config = "CONFIG_PHY_QCOM_UFS_V4_SUN",
        srcs = [
            # do not sort
            "drivers/phy/qualcomm/phy-qcom-ufs-i.h",
            "drivers/phy/qualcomm/phy-qcom-ufs-qmp-v4-sun.c",
            "drivers/phy/qualcomm/phy-qcom-ufs-qmp-v4-sun.h",
        ],
        deps = [
            # do not sort
            "drivers/phy/qualcomm/phy-qcom-ufs",
        ],
    )

    registry.register(
        name = "drivers/phy/qualcomm/phy-qcom-ufs-qrbtc-sdm845",
        out = "phy-qcom-ufs-qrbtc-sdm845.ko",
        config = "CONFIG_PHY_QCOM_UFS_QRBTC_SDM845",
        srcs = [
            # do not sort
            "drivers/phy/qualcomm/phy-qcom-ufs-i.h",
            "drivers/phy/qualcomm/phy-qcom-ufs-qrbtc-sdm845.c",
            "drivers/phy/qualcomm/phy-qcom-ufs-qrbtc-sdm845.h",
        ],
        deps = [
            # do not sort
            "drivers/phy/qualcomm/phy-qcom-ufs",
        ],
    )
