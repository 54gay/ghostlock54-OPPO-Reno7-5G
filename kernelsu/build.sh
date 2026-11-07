#!/data/data/com.termux/files/usr/bin/bash
# Cross-compile kernelsu.ko out-of-tree against a prepared OPPO sm7325 kernel tree.
#
# The prepared OPPO sm7325 5.4.254 kernel source is bundled in this repo
# (../kernel) so this script works out of the box.
#
# Requirements: clang/lld/llvm toolchain (Termux: pkg install clang lld llvm)
#
# Usage:
#   sh build.sh                              # use ../kernel
#   KERNEL_DIR=/path/to/android_kernel_oppo_sm7325 sh build.sh   # custom tree
set -e
SRC=$(cd "$(dirname "$0")" && pwd)
SRC_DIR=$(cd "$(dirname "$0")" && pwd)
TREE=${KERNEL_DIR:-$SRC_DIR/../kernel}
if [ ! -d "$TREE" ]; then
  echo "[!] kernel tree not found: $TREE (set KERNEL_DIR)" >&2
  exit 1
fi
make -C "$TREE" M="$SRC/src/kernel" src="$SRC/src/kernel" \
  ARCH=arm64 CC=clang LD=ld.lld AR=llvm-ar NM=llvm-nm \
  OBJCOPY=llvm-objcopy OBJDUMP=llvm-objdump STRIP=llvm-strip \
  KSU54_KSU_VERSION=32680 \
  KCFLAGS="-mstack-protector-guard=sysreg -mstack-protector-guard-reg=sp_el0 -mstack-protector-guard-offset=1624" \
  -k modules
echo "built: $SRC/src/kernel/kernelsu.ko"
