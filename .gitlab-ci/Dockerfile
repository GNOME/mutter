FROM fedora:latest

RUN dnf -y update && dnf -y upgrade
RUN dnf install -y 'dnf-command(builddep)'
RUN dnf builddep -y mutter

# Until Fedora catches up
RUN dnf install -y meson xorg-x11-server-Xorg gnome-settings-daemon-devel egl-wayland-devel xorg-x11-server-Xwayland
RUN dnf upgrade -y pipewire pipewire-devel
RUN rpm -U https://dl.fedoraproject.org/pub/fedora/linux/development/29/Everything/x86_64/os/Packages/w/wayland-protocols-devel-1.16-1.fc29.noarch.rpm

RUN dnf install -y intltool redhat-rpm-config make
RUN dnf clean all
