# Rebuild and push with
#
#     cd .gitlab-ci/
#     podman build --format docker --no-cache -t registry.gitlab.gnome.org/gnome/mutter/master:v4 .
#     podman push registry.gitlab.gnome.org/gnome/mutter/master:v4
#

FROM fedora:32

RUN dnf -y update && dnf -y upgrade && \
    dnf install -y 'dnf-command(builddep)' && \
    dnf install -y 'dnf-command(copr)' && \
    dnf copr enable -y jadahl/mutter-ci && \

    dnf builddep -y mutter --setopt=install_weak_deps=False && \

    # For running unit tests
    dnf install -y xorg-x11-server-Xvfb mesa-dri-drivers dbus dbus-x11 \
        '*/xvfb-run' gdm-lib accountsservice-libs gnome-control-center gcovr \
        --setopt=install_weak_deps=False && \

    # GNOME Shell
    dnf builddep -y gnome-shell --setopt=install_weak_deps=False && \
    dnf remove -y gnome-bluetooth-libs-devel && \
    dnf remove -y --noautoremove mutter mutter-devel && \

    dnf clean all
