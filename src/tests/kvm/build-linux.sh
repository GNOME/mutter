#!/bin/bash
#
# Script for building the Linux kernel from git. It aims to build a kernel image
# that is suitable for running in a virtual machine and is aimed to used for
# testing.
#
# Usage: build-linux.sh [REPO-URL] [BRANCH|TAG] [OUTPUT-FILE] [...CONFIGS]
#
# Where [..CONFIGS] can be any number of configuration options, e.g.
# --enable CONFIG_DRM_VKMS

set -e

# From scripts/subarch.include in linux
function get-subarch()
{
  uname -m | sed -e s/i.86/x86/ \
                 -e s/x86_64/x86/ \
                 -e s/sun4u/sparc64/ \
                 -e s/arm.*/arm/ -e s/sa110/arm/ \
                 -e s/s390x/s390/ -e s/parisc64/parisc/ \
                 -e s/ppc.*/powerpc/ -e s/mips.*/mips/ \
                 -e s/sh[234].*/sh/ -e s/aarch64.*/arm64/ \
                 -e s/riscv.*/riscv/
}

REPO="$1"
BRANCH_OR_TAG="$(cat $2)"
IMAGE="$3"

ARCH=$(uname -m)
SUBARCH=$(get-subarch)

shift
shift
shift

# ./scripts/config  --enable CONFIG_DRM_VKMS
CONFIGS=()
while [[ "x$1" != "x" ]]; do
  CONFIGS+=( "$1" )
  shift
done

echo Building Linux for $ARCH \($SUBARCH\)...

set -x

if [ -d linux ]; then
  pushd linux
  git fetch --depth=1 $REPO $BRANCH_OR_TAG
  git checkout FETCH_HEAD
else
  git clone --depth=1 --branch=$BRANCH_OR_TAG $REPO linux
  pushd linux
fi

# committer identity for git-am
git config user.name "Mutter CI"
git config user.email "mutter@gitlab.gnome.org"

cat << EOT | git am
From 811018a8475a4eb1bdb4ba4ab0d2a33515135de6 Mon Sep 17 00:00:00 2001
From: Sebastian Wick <sebastian.wick@redhat.com>
Date: Sun, 25 Oct 2026 14:47:22 +0100
Subject: [PATCH] drm/vkms: Use drm_dev_unplug() for ConfigFS device removal

VKMS ConfigFS devices can be disabled while userspace still holds open
file descriptors. Using drm_dev_unregister() leaves the device in a
state where ioctls are still accepted but internal locks can deadlock
with the teardown path. Specifically, drmModeGetConnector() triggers
fill_modes which acquires mode_config locks that contend with
drm_atomic_helper_shutdown().

Use drm_dev_unplug() instead, which marks the device as unplugged so
the ioctl layer rejects subsequent calls with -ENODEV and drm_poll()
returns EPOLLERR. This matches the pattern used by other hot-removable
DRM drivers (gud, hyperv, bochs).

Signed-off-by: Sebastian Wick <sebastian.wick@redhat.com>
---
 drivers/gpu/drm/vkms/vkms_drv.c | 2 +-
 1 file changed, 1 insertion(+), 1 deletion(-)

diff --git a/drivers/gpu/drm/vkms/vkms_drv.c b/drivers/gpu/drm/vkms/vkms_drv.c
index 5a640b531d88..716f9c966c21 100644
--- a/drivers/gpu/drm/vkms/vkms_drv.c
+++ b/drivers/gpu/drm/vkms/vkms_drv.c
@@ -259,7 +259,7 @@ void vkms_destroy(struct vkms_config *config)
 
 	fdev = config->dev->faux_dev;
 
-	drm_dev_unregister(&config->dev->drm);
+	drm_dev_unplug(&config->dev->drm);
 	drm_atomic_helper_shutdown(&config->dev->drm);
 	devres_release_group(&fdev->dev, NULL);
 	faux_device_destroy(fdev);
-- 
2.55.0

EOT

make defconfig
sync
make kvm_guest.config

echo Configuring kernel with ${CONFIGS[@]}...
vng --kconfig --config .config ${CONFIGS[@]}
make -j8 KCFLAGS="-Wno-error"

popd

TARGET_DIR="$(dirname "$IMAGE")"
mkdir -p "$TARGET_DIR"
mv linux/arch/$SUBARCH/boot/bzImage "$IMAGE"
mv linux/.config $TARGET_DIR/.config
#rm -rf linux
