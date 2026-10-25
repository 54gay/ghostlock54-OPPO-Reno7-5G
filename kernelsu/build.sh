#!/system/bin/sh
# Cross-compile kernelsu.ko out-of-tree against a prepared OPPO sm7325 kernel tree.
#
# Requirements:
#  - prepared kernel source tree (details in kernelsu/SUMMARY_5.4_KSU.md):
#      * android_kernel_oppo_sm7325 (branch oppo/sm7325_t_13.1_reno7) with
#        Makefile SUBLEVEL=254, localversion -qgki-gaa96eae94d32,
#        CONFIG_KSU=m, generated headers from the device kheaders,
#        Module.symvers in 5-field format, drivers/kernelsu symlink + Kconfig
#        and Makefile entries
#  - clang/lld/llvm toolchain (Termux: pkg install clang lld llvm)
#
# Usage:
#   KERNEL_DIR=/path/to/android_kernel_oppo_sm7325 sh build.sh
set -e
SRC=$(cd "$(dirname "$0")" && pwd)
TREE=${KERNEL_DIR:?set KERNEL_DIR to the prepared kernel source tree}
make -C "$TREE" M="$SRC/src/kernel" src="$SRC/src/kernel" \
  ARCH=arm64 CC=clang LD=ld.lld AR=llvm-ar NM=llvm-nm \
  OBJCOPY=llvm-objcopy OBJDUMP=llvm-objdump STRIP=llvm-strip \
  KSU54_KSU_VERSION=32680 \
  KCFLAGS="-mstack-protector-guard=sysreg -mstack-protector-guard-reg=sp_el0 -mstack-protector-guard-offset=1624" \
  -k modules
echo "built: $SRC/src/kernel/kernelsu.ko"
