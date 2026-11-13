# Build kernelsu.ko and ghostlock54 (single file, ko embedded)
#
# All artifacts live in build/ (no big copies: the full module is a symlink):
#   build/kernelsu.full.ko  - symlink -> kernelsu/src/kernel/kernelsu.ko
#   build/kernelsu.ko       - embedded copy: stripped (STRIP=1) or symlink (STRIP=0)
#   build/kernelsu_ko.o     - objcopy'd object linked into the binary
#
#   make                 # build module + ghostlock54
#   make STRIP=0         # keep debug info in the embedded ko (bigger binary)
#   make module          # build module only
#   make clean

KERNEL_DIR  ?= kernel
MODULE_SH   := kernelsu/build.sh
MOD_SRC_KO  := kernelsu/src/kernel/kernelsu.ko
BUILD_DIR   := build
KO_FULL     := $(BUILD_DIR)/kernelsu.full.ko
KO_EMBED    := $(BUILD_DIR)/kernelsu.ko
KO_OBJ      := $(BUILD_DIR)/kernelsu_ko.o
BIN         := ghostlock54
CC          ?= clang
CFLAGS      ?= -O2 -pthread
STRIP       ?= 1

.PHONY: all module clean

all: module $(BIN)

module:
	KERNEL_DIR=$(KERNEL_DIR) sh $(MODULE_SH)
	@mkdir -p $(BUILD_DIR)
	ln -sfn $(CURDIR)/$(MOD_SRC_KO) $(KO_FULL)
	@echo "[make] module -> $(KO_FULL) (symlink)"

$(BIN): ghostlock54.c module
	@mkdir -p $(BUILD_DIR)
	@if [ "$(STRIP)" = "1" ]; then \
	  llvm-strip --strip-debug -o $(KO_EMBED) $(KO_FULL); \
	else \
	  ln -sfn $(CURDIR)/$(KO_FULL) $(KO_EMBED); \
	fi
	@echo "[make] embedded ko: $$(stat -Lc %s $(KO_EMBED)) bytes"
	cd $(BUILD_DIR) && llvm-objcopy -I binary -O elf64-littleaarch64 -B aarch64 kernelsu.ko kernelsu_ko.o
	$(CC) $(CFLAGS) ghostlock54.c $(KO_OBJ) -o $(BIN)
	@echo "[make] built: $(BIN) ($$(stat -c %s $(BIN)) bytes)"

clean:
	rm -rf $(BUILD_DIR) $(BIN)
	-$(MAKE) -C $(KERNEL_DIR) M=$(CURDIR)/kernelsu/src/kernel src=$(CURDIR)/kernelsu/src/kernel clean
