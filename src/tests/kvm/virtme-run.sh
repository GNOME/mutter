#!/usr/bin/bash

set -e

DIRNAME="$(dirname "$0")"
IMAGE="$1"
WRAPPER="$2"
WRAPPER_ARGS="$3"
TEST_EXECUTABLE="$4"
TEST_BUILD_DIR="$5"
VM_ENV="$6"

TEST_RESULT_FILE=$(mktemp -p "$TEST_BUILD_DIR" -t test-result-XXXXXX)
echo 1 > "$TEST_RESULT_FILE"

VIRTME_ENV="\
HOME=$HOME \
LD_LIBRARY_PATH=$LD_LIBRARY_PATH \
XDG_DATA_DIRS=$XDG_DATA_DIRS \
$VM_ENV \
"

if [[ "$(stat -c '%t:%T' -L /proc/$$/fd/0)" == "0:0" ]]; then
  mkfifo $XDG_RUNTIME_DIR/fake-stdin.$$
  exec 0<> $XDG_RUNTIME_DIR/fake-stdin.$$
  rm -f $XDG_RUNTIME_DIR/fake-stdin.$$
fi

virtme-run \
  --memory=256M \
  --rw \
  --pwd \
  --kimg "$IMAGE" \
  --script-sh "sh -c \"env $VIRTME_ENV $DIRNAME/run-kvm-test.sh \\\"$WRAPPER\\\" \\\"$WRAPPER_ARGS\\\" \\\"$TEST_EXECUTABLE\\\" \\\"$TEST_RESULT_FILE\\\"\"" \
  --qemu-opts -cpu host,pdcm=off -smp 2

TEST_RESULT="$(cat "$TEST_RESULT_FILE")"
rm "$TEST_RESULT_FILE"

exit "$TEST_RESULT"
