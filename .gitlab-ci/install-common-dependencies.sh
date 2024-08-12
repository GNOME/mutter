#!/bin/bash

set -e

usage() {
  cat <<-EOF
	Usage: $(basename $0) [OPTION…]

	Install common dependencies to a base image or system extension

	Options:
	  --libdir       Setup the projects with a different libdir
	  --destdir      Install the projects to an additional destdir

	  -h, --help     Display this help

	EOF
}

TEMP=$(getopt \
  --name=$(basename $0) \
  --options='' \
  --longoptions='libdir:' \
  --longoptions='destdir:' \
  --longoptions='help' \
  -- "$@")

eval set -- "$TEMP"
unset TEMP

OPTIONS=()

while true; do
  case "$1" in
    --libdir)
      OPTIONS+=( --libdir=$2 )
      shift 2
    ;;

    --destdir)
      OPTIONS+=( --destdir=$2 )
      shift 2
    ;;

    -h|--help)
      usage
      exit 0
    ;;

    --)
      shift
      break
    ;;
  esac
done

SCRIPTS_DIR="$(dirname $0)"

if ! pkgconf --atleast-version 1.2.3 libpipewire-0.3
then
     ./$SCRIPTS_DIR/install-meson-project.sh \
       "${OPTIONS[@]}" \
       -Dalsa=disabled \
       -Dbluez5=disabled \
       -Dexamples=disabled \
       -Dgstreamer=disabled \
       -Djack=disabled \
       -Dman=disabled \
       -Dpipewire-alsa=disabled \
       -Dpipewire-jack=disabled \
       -Dsystemd=enabled \
       -Dtests=disabled \
       https://gitlab.freedesktop.org/pipewire/pipewire.git \
       1.2.3
fi

if ! pkgconf --atleast-version 1.23.0 wayland-server
then
     ./$SCRIPTS_DIR/install-meson-project.sh \
       "${OPTIONS[@]}" \
       https://gitlab.freedesktop.org/wayland/wayland.git \
       1.23.0
fi

if ! pkgconf --atleast-version 1.36 wayland-protocols
then
    ./$SCRIPTS_DIR/install-meson-project.sh \
      "${OPTIONS[@]}" \
      https://gitlab.freedesktop.org/wayland/wayland-protocols.git \
      1.36
fi

if ! pkgconf --atleast-version 47.beta gsettings-desktop-schemas
then
    ./$SCRIPTS_DIR/install-meson-project.sh \
      "${OPTIONS[@]}" \
      https://gitlab.gnome.org/GNOME/gsettings-desktop-schemas.git \
      master
fi
