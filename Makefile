# Build kernelsu.ko and ghostlock54 (single file, ko embedded)
#
#   make                 # build module + ghostlock54
#   make STRIP=1         # strip debug info from the embedded ko (smaller binary)
#   make module          # build module only, then copy to kernelsu/kernelsu.ko
#   make clean

KERNEL_DIR  ?= kernel
MODULE_SH   := kernelsu/build.sh
MOD_SRC_KO  := kernelsu/src/kernel/kernelsu.ko
CANON_KO    := kernelsu/kernelsu.ko
BUILD_DIR   := build
KO_OBJ      := $(BUILD_DIR)/kernelsu_ko.o
BIN         := ghostlock54
CC          ?= clang
CFLAGS      ?= -O2 -pthread
STRIP       ?= 1

.PHONY: all module clean

all: module $(BIN)

module:
	KERNEL_DIR=$(KERNEL_DIR) sh $(MODULE_SH)
	cp -f $(MOD_SRC_KO) $(CANON_KO)
	@echo "[make] module -> $(CANON_KO)"

$(BIN): ghostlock54.c module
	@mkdir -p $(BUILD_DIR)
	@cp -f $(CANON_KO) $(BUILD_DIR)/kernelsu.ko
	@if [ "$(STRIP)" = "1" ]; then \
	  llvm-strip --strip-debug $(BUILD_DIR)/kernelsu.ko; \
	  echo "[make] stripped embedded ko: $$(stat -c %s $(BUILD_DIR)/kernelsu.ko) bytes"; \
	fi
	cd $(BUILD_DIR) && llvm-objcopy -I binary -O elf64-littleaarch64 -B aarch64 kernelsu.ko kernelsu_ko.o
	$(CC) $(CFLAGS) ghostlock54.c $(KO_OBJ) -o $(BIN)
	@echo "[make] built: $(BIN) ($$(stat -c %s $(BIN)) bytes)"

clean:
	rm -rf $(BUILD_DIR) $(BIN)
	-$(MAKE) -C $(KERNEL_DIR) M=$(CURDIR)/kernelsu/src/kernel src=$(CURDIR)/kernelsu/src/kernel clean
