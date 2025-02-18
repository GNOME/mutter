#!/usr/bin/env bash

set -e

SCRIPTS_DIR="$(dirname $0)"

# Location for dependencies to be bundled with the extension
DESTDIR="$(realpath $1)"

# GNOME OS specific setup arguments
LIBDIR="lib/$(gcc -print-multiarch)"

# Install common dependencies
./$SCRIPTS_DIR/install-common-dependencies.sh --libdir=$LIBDIR --destdir=$DESTDIR --destdir=/

# Install below missing dependencies that are exclusive to GNOME OS

./$SCRIPTS_DIR/install-meson-project.sh \
    --libdir=$LIBDIR --destdir=$DESTDIR --destdir=/ \
    https://gitlab.gnome.org/GNOME/zenity.git \
    master
