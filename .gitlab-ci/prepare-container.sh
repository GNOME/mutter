#!/usr/bin/env bash

VOLUME=$1
REPO_URL=$2
HTTPS_URL=$(echo "$REPO_URL" | sed -E -e 's/^git@([^:]+):/https:\/\/\1\//' -e 's/^ssh:\/\/git@/https:\/\//')
REF=$3

REPO=$(basename $REPO_URL)
DIR=${REPO%%.git}

cd $VOLUME

if [ ! -d $DIR ]; then
  echo Cloning and building branch/tag $REF from $HTTPS_URL
  git clone $HTTPS_URL -b $REF
  cd $DIR
  meson setup build
  ninja -C build
fi

dnf install -y gdb
