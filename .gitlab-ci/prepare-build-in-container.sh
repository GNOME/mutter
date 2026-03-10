#!/usr/bin/env bash

if [ $# != 2 ] && [ $# != 0 ]; then
  echo Usage: $0 [repo ref]
  exit 1
fi

set -e

DIR=$(dirname $0)

if [ $# = 2 ]; then
  REPO=$1
  REF=$2
else
  REPO=$(git remote get-url origin)
  REF=main
fi

BUILD_TAG=$(uname -m)-$(grep 'BASE_TAG:' $DIR/../.gitlab-ci.yml | sed -E "s/.*'(.*)'/\1/")
FEDORA_VERSION=$(grep 'FDO_DISTRIBUTION_VERSION:' $DIR/../.gitlab-ci.yml | sed -E "s/.*: (.*)/\1/")
PROJECT=$(basename $(git rev-parse --show-toplevel))
ID=$PROJECT-$BUILD_TAG

echo Using the image and container name $ID

if ! buildah inspect --type=image $ID >& /dev/null; then
  echo Preparing container image and volume ...
  buildah from --name $ID registry.gitlab.gnome.org/gnome/mutter/fedora/$FEDORA_VERSION:$BUILD_TAG
  buildah add $ID $DIR/prepare-container.sh
  buildah commit $ID $ID
  buildah rm $ID
  podman volume create $ID
  podman run --rm --volume $ID:/src $ID ./prepare-container.sh /src $REPO $REF
  echo \#
  echo \#
else
  echo \#
  echo \#
  echo \# Container and volume exists already, manual update needed...
fi

echo \# Running interactive shell using image $ID
echo \#
echo \#

podman run \
  --interactive \
  --tty \
  --rm \
  --name $ID \
  --volume $ID:/src \
  --workdir /src/$PROJECT \
  $ID \
  bash --login
