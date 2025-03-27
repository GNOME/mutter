#!/usr/bin/env bash

set -e

DIRNAME="$(dirname "$0")"
WRAPPER="$1"
WRAPPER_ARGS="$2"
TEST_RESULT="$3"

export XDG_RUNTIME_DIR="/tmp/sub-runtime-dir-$UID"
export GSETTINGS_SCHEMA_DIR="$PWD/build/data"
export G_SLICE="always-malloc"
export MALLOC_CHECK_="3"
export NO_AT_BRIDGE="1"
export GTK_A11Y="none"
export MALLOC_PERTURB_="123"

mkdir -p -m 700 $XDG_RUNTIME_DIR

glib-compile-schemas $GSETTINGS_SCHEMA_DIR
$DIRNAME/install-udev-rules.sh

status=0
"$WRAPPER" $WRAPPER_ARGS "${@:4}" || status=$?

echo $status > $TEST_RESULT
