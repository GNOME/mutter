#!/bin/bash

set -e

usage() {
  cat <<-EOF
	Usage: $(basename $0) [OPTION…]

	Install common dependencies to a base image or system extension

	Options:
	  --libdir=DIR     Setup the projects with a different libdir
	  --destdir=DIR    Install the project to DIR, can be used
	                   several times to install to multiple destdirs

	  -h, --help       Display this help

	EOF
}

pkgconf() {
  local PKG_CONFIG_DIRS=(
    /usr/lib64/pkgconfig
    /usr/lib/pkgconfig
    /usr/share/pkgconfig
  )

  local search_dirs=()
  for destdir in "${DESTDIRS[@]}"; do
    search_dirs+=( "${PKG_CONFIG_DIRS[@]/#/$destdir}" )
  done

  ENV=(PKG_CONFIG_PATH=$(echo "${search_dirs[@]}" | tr ' ' :))

  env "${ENV[@]}" pkgconf --env-only "$@"
}

TEMP=$(getopt \
  --name=$(basename $0) \
  --options='h' \
  --longoptions='libdir:' \
  --longoptions='destdir:' \
  --longoptions='help' \
  -- "$@")

eval set -- "$TEMP"
unset TEMP

OPTIONS=()
DESTDIRS=()

while true; do
  case "$1" in
    --libdir)
      OPTIONS+=( --libdir=$2 )
      shift 2
    ;;

    --destdir)
      DESTDIRS+=( $2 )
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

[[ ${#DESTDIRS[@]} == 0 ]] && DESTDIRS+=( / )
OPTIONS+=( "${DESTDIRS[@]/#/--destdir=}" )

SCRIPTS_DIR="$(dirname $0)"

## Add necessary dependencies here, for example
# if ! pkgconf --atleast-version 1.23.0 wayland-server
# then
#     ./$SCRIPTS_DIR/install-meson-project.sh \
#       "${OPTIONS[@]}" \
#       https://gitlab.freedesktop.org/wayland/wayland.git \
#       1.23.0
# fi
