#!/usr/bin/env bash

set -e

DIRNAME="$(dirname "$0")"
IMAGE="$1"
WRAPPER="$2"
WRAPPER_ARGS="$3"
TEST_BUILD_DIR="$4"
VM_ENV="$5"

TEST_RESULT_FILE=$(mktemp -p "$TEST_BUILD_DIR" test-result-XXXXXX)
echo 1 > "$TEST_RESULT_FILE"

SCRIPT_FILE=$(mktemp -p "$TEST_BUILD_DIR" test-script-XXXXXX)

cat > "$SCRIPT_FILE" <<__EOF__
export HOME=$HOME \
       LD_LIBRARY_PATH=$LD_LIBRARY_PATH \
       XDG_DATA_DIRS=$XDG_DATA_DIRS \
       MUTTER_DEBUG=$MUTTER_DEBUG \
       CLUTTER_DEBUG=$CLUTTER_DEBUG \
       COGL_DEBUG=$COGL_DEBUG \
       $VM_ENV
__EOF__

cleanup_paths=""
trap '{ rm -rf $cleanup_paths; }' EXIT

if [ ! -v "$XDG_RUNTIME_DIR" ]; then
  tmpdir=$(mktemp -d --tmpdir mutter-runtime-XXXXXX)
  chmod 700 "$tmpdir"
  cleanup_paths="$cleanup_paths $tmpdir"
  export XDG_RUNTIME_DIR="$tmpdir"
fi

if [[ "$(stat -c '%t:%T' -L /proc/$$/fd/0)" == "0:0" ]]; then
  mkfifo $XDG_RUNTIME_DIR/fake-stdin.$$
  exec 0<> $XDG_RUNTIME_DIR/fake-stdin.$$
  rm -f $XDG_RUNTIME_DIR/fake-stdin.$$
fi

cat >> "$SCRIPT_FILE" <<__EOF__
$DIRNAME/run-kvm-test.sh \
    "$WRAPPER" "$WRAPPER_ARGS" \
    "$TEST_RESULT_FILE" \
    $(printf "\"%s\" " "${@:6}")
__EOF__

chmod +x "$SCRIPT_FILE"

if [ -v "$MUTTER_TEST_ROOT" ]; then
  ISOLATE_DIRS_ARGS="--rwdir=$MUTTER_TEST_ROOT"
fi

echo Running tests in virtual machine ...
vng \
  --run "$IMAGE" \
  --memory=1024M \
  --rw \
  --exec "$SCRIPT_FILE" \
  --cpus 2 \
  $ISOLATE_DIRS_ARGS \
  --qemu-opts="-cpu host,pdcm=off"
VM_RESULT=$?
if [ $VM_RESULT != 0 ]; then
  echo Virtual machine exited with a failure: $VM_RESULT
else
  echo Virtual machine terminated.
fi

TEST_RESULT="$(cat "$TEST_RESULT_FILE")"
rm "$TEST_RESULT_FILE"
rm "$SCRIPT_FILE"

echo Test result exit status: $TEST_RESULT

exit "$TEST_RESULT"
