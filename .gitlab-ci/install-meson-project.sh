#!/bin/bash

set -e

if [[ $# -lt 3 ]]; then
  echo Usage: $0 [options] [repo-url] [commit] [subdir]
  echo  Options:
  echo    -Dkey=val
  exit 1
fi

MESON_OPTIONS=()

while [[ $1 =~ ^-D ]]; do
  MESON_OPTIONS+=( "$1" )
  shift
done

REPO_URL="$1"
TAG_OR_BRANCH="$2"
SUBDIR="$3"
COMMIT="$4"

REPO_DIR="$(basename ${REPO_URL%.git})"

git clone --depth 1 "$REPO_URL" -b "$TAG_OR_BRANCH"
pushd "$REPO_DIR"
pushd "$SUBDIR"

if [ ! -z "$COMMIT" ]; then
  git fetch origin "$COMMIT"
  git checkout "$COMMIT"
fi

meson --prefix=/usr _build "${MESON_OPTIONS[@]}"
ninja -C _build install
popd
popd
rm -rf "$REPO_DIR"
