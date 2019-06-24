FROM fedora:30

RUN dnf -y update && dnf -y upgrade && \
    dnf install -y 'dnf-command(builddep)' && \
    dnf install -y 'dnf-command(copr)' && \
    dnf copr enable -y fmuellner/gnome-shell-ci && \
    dnf copr enable -y hergertme/sysprof-3 && \

    dnf builddep -y mutter && \

    # Until Fedora catches up with meson build-deps
    dnf install -y meson xorg-x11-server-Xorg gnome-settings-daemon-devel egl-wayland-devel xorg-x11-server-Xwayland && \

    # For running unit tests
    dnf install -y xorg-x11-server-Xvfb mesa-dri-drivers dbus dbus-x11 '*/xvfb-run' gdm-lib accountsservice-libs && \

    # Unpackaged versions
    dnf install -y https://copr-be.cloud.fedoraproject.org/results/jadahl/mutter-ci/fedora-29-x86_64/00834984-gsettings-desktop-schemas/gsettings-desktop-schemas-3.30.1-1.20181206git918efdd69be53.fc29.x86_64.rpm https://copr-be.cloud.fedoraproject.org/results/jadahl/mutter-ci/fedora-29-x86_64/00834984-gsettings-desktop-schemas/gsettings-desktop-schemas-devel-3.30.1-1.20181206git918efdd69be53.fc29.x86_64.rpm && \
    dnf install -y sysprof-devel && \

    dnf install -y intltool redhat-rpm-config make && \

    # GNOME Shell
    dnf builddep -y gnome-shell --setopt=install_weak_deps=False && \
    dnf remove -y gnome-bluetooth-libs-devel dbus-glib-devel upower-devel python3-devel && \
    dnf remove -y --noautoremove mutter mutter-devel && \

    dnf clean all
