#!/bin/bash

set -e

DIRNAME="$(dirname "$0")"
IMAGE="$1"
WRAPPER="$2"
WRAPPER_ARGS="$3"
TEST_BUILD_DIR="$4"
VM_ENV="$5"

TEST_RESULT_FILE=$(mktemp -p "$TEST_BUILD_DIR" test-result-XXXXXX)
echo 1 > "$TEST_RESULT_FILE"

VIRTME_ENV="\
HOME=$HOME \
LD_LIBRARY_PATH=$LD_LIBRARY_PATH \
XDG_DATA_DIRS=$XDG_DATA_DIRS \
MUTTER_DEBUG=$MUTTER_DEBUG \
$VM_ENV \
"

cleanup_paths=""
trap '{ rm -rf $cleanup_paths; }' EXIT

if [ ! -v $MUTTER_DEBUG_FORCE_KMS_MODE ]; then
  VIRTME_ENV="$VIRTME_ENV MUTTER_DEBUG_FORCE_KMS_MODE=$MUTTER_DEBUG_FORCE_KMS_MODE"
fi
if [ ! -v $MUTTER_DEBUG_KMS_THREAD_TYPE ]; then
  VIRTME_ENV="$VIRTME_ENV MUTTER_DEBUG_KMS_THREAD_TYPE=$MUTTER_DEBUG_KMS_THREAD_TYPE"
fi

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

SCRIPT="\
  env $VIRTME_ENV $DIRNAME/run-kvm-test.sh \
  \\\"$WRAPPER\\\" \\\"$WRAPPER_ARGS\\\" \
  \\\"$TEST_RESULT_FILE\\\" \
  $(printf "\"%s\" " "${@:6}")\
"

if [ -v "$MUTTER_TEST_ROOT" ]; then
  ISOLATE_DIRS_ARGS="--rwdir=$MUTTER_TEST_ROOT"
fi

echo Running tests in virtual machine ...
virtme-run \
  --memory=1024M \
  --rw \
  --pwd \
  --kimg "$IMAGE" \
  --script-sh "sh -c \"$SCRIPT\"" \
  $ISOLATE_DIRS_ARGS \
  --qemu-opts -cpu host,pdcm=off -smp 2
VM_RESULT=$?
if [ $VM_RESULT != 0 ]; then
  echo Virtual machine exited with a failure: $VM_RESULT
else
  echo Virtual machine terminated.
fi

TEST_RESULT="$(cat "$TEST_RESULT_FILE")"
rm "$TEST_RESULT_FILE"

echo Test result exit status: $TEST_RESULT

exit "$TEST_RESULT"
