FROM fedora:29

RUN dnf -y update && dnf -y upgrade && \
    dnf install -y 'dnf-command(builddep)' && \
    dnf builddep -y mutter && \

    # Until Fedora catches up with meson build-deps
    dnf install -y meson xorg-x11-server-Xorg gnome-settings-daemon-devel egl-wayland-devel xorg-x11-server-Xwayland && \

    # For running unit tests
    dnf install -y xorg-x11-server-Xvfb mesa-dri-drivers dbus dbus-x11 && \

    dnf install -y intltool redhat-rpm-config make git && \

    # Git dependencies
    # gsettings-desktop-schemas
        mkdir gsettings-desktop-schemas && \
        cd gsettings-desktop-schemas && \
            git clone https://gitlab.gnome.org/GNOME/gsettings-desktop-schemas.git && \
            gsettings-desktop-schemas/autogen.sh --prefix /usr && \
            make && \
            make install && \
        cd .. && \
        rm -rf gsettings-desktop-schemas && \

    dnf clean all
