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
From 633be2bc828b77d8bc253e2df381f1c45f895e5a Mon Sep 17 00:00:00 2001
From: Jakub Jelinek <jakub@redhat.com>
Date: Mon, 20 Jan 2025 13:00:46 +0100
Subject: [PATCH] include/linux: Adjust headers for C23

GCC 15 changed default from -std=gnu17 to -std=gnu23.  In C23
among many other changes bool, false and true are keywords
(like in C++), so defining those using typedef or enum is invalid.

The following patch adjusts the include/linux/ headers to be C23
compatible.  _Bool and the C23 bool are ABI compatible, false/true
have the same values but different types (previously in the kernel
case it was an anonymous enum, in C23 it is bool), so if something uses
say sizeof(false) or typeof(true), those do change, but I doubt that
is used anywhere in the kernel.

The last change is about va_start.  In C23 ellipsis can be specified
without any arguments before it, like
int foo(...)
{
	va_list ap;
	va_start(ap);
	int ret = va_arg(ap, int);
	va_end(ap);
	return ret;
}
and unlike in C17 and earlier, va_start is macro with variable argument
count.  Normally one should use it with just one argument or for backwards
compatibility with C17 and earlier with two arguments, second being the last
named argument.  Of course, if there is no last named argument, only the
single argument va_start is an option.  The stdarg.h change attempts to be
compatible with older versions of GCC and with clang as well.  Both GCC 13-14
and recent versions of clang define va_start for C23 as
#define va_start(v, ...) __builtin_va_start(v, 0)
The problem with that definition is that it doesn't emit warnings when one
uses complete nonsense in there (e.g. va_start(ap, 8) or
va_start(ap, +-*, /, 3, 4.0)), so for GCC 15 it uses a different builtin
which takes care about warnings for unexpected va_start uses (as suggested
by the C23 standard).  Hopefully clang will one day implement that too.

Anyway, without these changes, kernel could detect compiler defaulting to
C23 and use say -std=gnu17 option instead, but even in that case IMHO this
patch doesn't hurt.

Signed-off-by: Jakub Jelinek <jakub@redhat.com>
---
 include/linux/stdarg.h | 10 ++++++++++
 include/linux/stddef.h |  2 ++
 include/linux/types.h  |  2 ++
 3 files changed, 14 insertions(+)

diff --git a/include/linux/stdarg.h b/include/linux/stdarg.h
index c8dc7f4f390c4..af7eb0ed138ed 100644
--- a/include/linux/stdarg.h
+++ b/include/linux/stdarg.h
@@ -3,7 +3,17 @@
 #define _LINUX_STDARG_H

 typedef __builtin_va_list va_list;
+#if defined(__STDC_VERSION__) && __STDC_VERSION__ > 201710L
+#define va_start(v, ...) __builtin_va_start(v, 0)
+#ifdef __has_builtin
+#if __has_builtin(__builtin_c23_va_start)
+#undef va_start
+#define va_start(...)	__builtin_c23_va_start(__VA_ARGS__)
+#endif
+#endif
+#else
 #define va_start(v, l)	__builtin_va_start(v, l)
+#endif
 #define va_end(v)	__builtin_va_end(v)
 #define va_arg(v, T)	__builtin_va_arg(v, T)
 #define va_copy(d, s)	__builtin_va_copy(d, s)
diff --git a/include/linux/stddef.h b/include/linux/stddef.h
index 929d67710cc51..16508c74fca94 100644
--- a/include/linux/stddef.h
+++ b/include/linux/stddef.h
@@ -7,10 +7,12 @@
 #undef NULL
 #define NULL ((void *)0)

+#if !defined(__STDC_VERSION__) || __STDC_VERSION__ < 202311L
 enum {
 	false	= 0,
 	true	= 1
 };
+#endif

 #undef offsetof
 #define offsetof(TYPE, MEMBER)	__builtin_offsetof(TYPE, MEMBER)
diff --git a/include/linux/types.h b/include/linux/types.h
index 1c509ce8f7f61..4e08ce851059d 100644
--- a/include/linux/types.h
+++ b/include/linux/types.h
@@ -32,7 +32,9 @@ typedef __kernel_timer_t	timer_t;
 typedef __kernel_clockid_t	clockid_t;
 typedef __kernel_mqd_t		mqd_t;

+#if !defined(__STDC_VERSION__) || __STDC_VERSION__ < 202311L
 typedef _Bool			bool;
+#endif

 typedef __kernel_uid32_t	uid_t;
 typedef __kernel_gid32_t	gid_t;
--
GitLab
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
