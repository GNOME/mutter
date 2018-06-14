FROM fedora:29

RUN dnf -y update && dnf -y upgrade
RUN dnf install -y 'dnf-command(builddep)'
RUN dnf builddep -y mutter

# Until Fedora catches up with meson build-deps
RUN dnf install -y meson xorg-x11-server-Xorg gnome-settings-daemon-devel egl-wayland-devel xorg-x11-server-Xwayland

RUN dnf install -y intltool redhat-rpm-config make
RUN dnf clean all
