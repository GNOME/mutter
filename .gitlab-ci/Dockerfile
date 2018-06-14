FROM fedora:29

RUN dnf -y update && dnf -y upgrade && \
    dnf install -y 'dnf-command(builddep)' && \
    dnf builddep -y mutter && \

# Until Fedora catches up with meson build-deps
    dnf install -y meson xorg-x11-server-Xorg gnome-settings-daemon-devel egl-wayland-devel xorg-x11-server-Xwayland && \

    dnf install -y intltool redhat-rpm-config make && \
    dnf clean all
