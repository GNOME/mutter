FROM fedora:29

RUN dnf -y update && dnf -y upgrade && \
    dnf install -y 'dnf-command(builddep)' && \
    dnf builddep -y mutter && \

    # Until Fedora catches up with meson build-deps
    dnf install -y meson xorg-x11-server-Xorg gnome-settings-daemon-devel egl-wayland-devel xorg-x11-server-Xwayland && \

    # For running unit tests
    dnf install -y xorg-x11-server-Xvfb mesa-dri-drivers dbus dbus-x11 && \

    # Unpackaged versions
    dnf install -y https://copr-be.cloud.fedoraproject.org/results/jadahl/mutter-ci/fedora-29-x86_64/00836095-gsettings-desktop-schemas/gsettings-desktop-schemas-3.30.1-1.20181206git918efdd69be53.fc29.x86_64.rpm https://copr-be.cloud.fedoraproject.org/results/jadahl/mutter-ci/fedora-29-x86_64/00836095-gsettings-desktop-schemas/gsettings-desktop-schemas-devel-3.30.1-1.20181206git918efdd69be53.fc29.x86_64.rpm && \

    dnf install -y intltool redhat-rpm-config make && \
    dnf clean all
