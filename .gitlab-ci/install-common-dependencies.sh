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

## Add necessary dependencies here, for example
# if ! pkgconf --atleast-version 1.23.0 wayland-server
# then
#     ./$SCRIPTS_DIR/install-meson-project.sh \
#       "${OPTIONS[@]}" \
#       https://gitlab.freedesktop.org/wayland/wayland.git \
#       1.23.0
# fi
