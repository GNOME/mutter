FROM fedora:latest

RUN dnf -y update && dnf -y upgrade
RUN dnf install -y 'dnf-command(builddep)'
RUN dnf builddep -y mutter
RUN dnf install -y intltool redhat-rpm-config make
RUN rpm -U https://dl.fedoraproject.org/pub/fedora/linux/development/29/Everything/x86_64/os/Packages/w/wayland-protocols-devel-1.16-1.fc29.noarch.rpm
RUN dnf clean all
