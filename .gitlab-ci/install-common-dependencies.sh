#!/usr/bin/env bash

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

pip_install() {
  local pkg=$1

  for destdir in "${DESTDIRS[@]}"; do
    local pypaths=($destdir/usr/lib*/python3*/site-packages)
    if ! pip3 list "${pypaths[@]/#/--path=}" | grep $pkg >/dev/null
    then
      sudo pip3 install --ignore-installed \
        --root-user-action ignore \
        --prefix $destdir/usr \
        $pkg
    fi
  done
}

check_gsettings_key() {
  local schema=$1
  local key=$2

  local rv=0

  for destdir in "${DESTDIRS[@]}"; do
    local schemadir=$(realpath $destdir/usr/share/glib-2.0/schemas/)
    local targetdir=$(mktemp --directory)

    if ! glib-compile-schemas --targetdir $targetdir $schemadir 2>/dev/null ||\
       ! env -i "XDG_DATA_DIRS=/dev/null" \
           gsettings --schemadir $targetdir get $schema $key >/dev/null 2>&1
    then
      rv=1
    fi

    rm -rf $targetdir
  done

  return $rv
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

if ! pkgconf --atleast-version 1.85.1 gjs-1.0
then
    ./$SCRIPTS_DIR/install-meson-project.sh \
      "${OPTIONS[@]}" \
      https://gitlab.gnome.org/GNOME/gjs.git \
      --commit b46026efc15d5348a47bf1aa47f1ea9bf3821ee0 \
      master
fi

if ! pkgconf --atleast-version 1.24 wayland-server
then
    ./$SCRIPTS_DIR/install-meson-project.sh \
      "${OPTIONS[@]}" \
      https://gitlab.freedesktop.org/wayland/wayland.git \
      1.24.0
fi

if ! pkgconf --atleast-version 1.44 wayland-protocols
then
    ./$SCRIPTS_DIR/install-meson-project.sh \
      "${OPTIONS[@]}" \
      https://gitlab.freedesktop.org/wayland/wayland-protocols.git \
      1.44
fi

if ! pkgconf --atleast-version 2.85.0 glib-2.0
then
    ./$SCRIPTS_DIR/install-meson-project.sh \
      "${OPTIONS[@]}" \
      https://gitlab.gnome.org/GNOME/glib.git \
      2.85.2
fi

if ! pkgconf --atleast-version 2.0i.beta.2 glycin-2
then
    ./$SCRIPTS_DIR/install-meson-project.sh \
      "${OPTIONS[@]}" \
      https://gitlab.gnome.org/GNOME/glycin.git \
      2.0.beta.2
fi

if ! check_gsettings_key org.gnome.desktop.screensaver restart-enabled
then
    ./$SCRIPTS_DIR/install-meson-project.sh \
      "${OPTIONS[@]}" \
      https://gitlab.gnome.org/GNOME/gsettings-desktop-schemas.git \
      49.beta
fi
