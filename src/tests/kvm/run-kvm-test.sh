#!/usr/bin/bash

set -e

WRAPPER="$1"
WRAPPER_ARGS="$2"
TEST_EXECUTABLE="$3"
TEST_RESULT="$4"

export XDG_RUNTIME_DIR="/tmp/sub-runtime-dir-$UID"
export GSETTINGS_SCHEMA_DIR="$PWD/build/data"
export G_SLICE="always-malloc"
export MALLOC_CHECK_="3"
export NO_AT_BRIDGE="1"
export MALLOC_PERTURB_="123"

mkdir -p -m 700 $XDG_RUNTIME_DIR

glib-compile-schemas $GSETTINGS_SCHEMA_DIR

"$WRAPPER" $WRAPPER_ARGS "$TEST_EXECUTABLE"

echo $? > $TEST_RESULT
