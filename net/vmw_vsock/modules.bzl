def register_modules(registry):
    registry.register(
        name = "net/vmw_vsock/vsock",
        out = "vsock.ko",
        config = "CONFIG_VSOCKETS",
        srcs = [
            # do not sort
            "net/vmw_vsock/af_vsock.c",
            "net/vmw_vsock/af_vsock_tap.c",
            "net/vmw_vsock/vsock_addr.c",
        ],
        deps = [
            # do not sort
        ],
    )

    registry.register(
        name = "net/vmw_vsock/vsock_diag",
        out = "vsock_diag.ko",
        config = "CONFIG_VSOCKETS_DIAG",
        srcs = [
            # do not sort
            "net/vmw_vsock/vsock_diag.c",
        ],
        deps = [
            # do not sort
        ],
    )

    registry.register(
        name = "net/vmw_vsock/vmw_vsock_virtio_transport_common",
        out = "vsock_diag.ko",
        config = "CONFIG_VIRTIO_VSOCKETS_COMMON",
        srcs = [
            # do not sort
            "net/vmw_vsock/vmw_vsock_virtio_transport_common.c",
        ],
        deps = [
            # do not sort
        ],
    )

    registry.register(
        name = "net/vmw_vsock/vsock_loopback",
        out = "vsock_diag.ko",
        config = "CONFIG_VSOCKETS_LOOPBACK",
        srcs = [
            # do not sort
            "net/vmw_vsock/vsock_loopback.c",
        ],
        deps = [
            # do not sort
        ],
    )
