FROM fedora:latest

RUN dnf -y update && dnf -y upgrade
RUN dnf install -y 'dnf-command(builddep)'
RUN dnf builddep -y mutter
RUN dnf install -y intltool redhat-rpm-config make
RUN dnf clean all
