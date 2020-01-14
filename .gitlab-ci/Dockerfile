# Rebuild and push with
#
#     cd .gitlab-ci/
#     podman build --format docker --no-cache -t registry.gitlab.gnome.org/gnome/mutter/master:v3 .
#     podman push registry.gitlab.gnome.org/gnome/mutter/master:v3
#

FROM fedora:31

RUN dnf -y update && dnf -y upgrade && \
    dnf install -y 'dnf-command(builddep)' && \
    dnf install -y 'dnf-command(copr)' && \
    dnf copr enable -y fmuellner/gnome-shell-ci && \
    dnf copr enable -y jadahl/mutter-ci && \
    dnf -y update && dnf -y upgrade && \

    dnf builddep -y mutter && \

    # Until Fedora catches up with new build-deps
    dnf install -y 'pkgconfig(graphene-gobject-1.0)' 'pkgconfig(sysprof-capture-3)' && \

    # For running unit tests
    dnf install -y xorg-x11-server-Xvfb mesa-dri-drivers dbus dbus-x11 '*/xvfb-run' gdm-lib accountsservice-libs gnome-control-center && \

    # GNOME Shell
    dnf builddep -y gnome-shell --setopt=install_weak_deps=False && \
    dnf remove -y gnome-bluetooth-libs-devel dbus-glib-devel upower-devel python3-devel && \
    dnf remove -y --noautoremove mutter mutter-devel && \

    dnf upgrade -y 'pkgconfig(libpipewire-0.3)' && \

    dnf clean all
