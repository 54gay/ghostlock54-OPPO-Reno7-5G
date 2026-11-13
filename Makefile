# Build kernelsu.ko and ghostlock54 (single file, ko embedded)
#
# Artifacts (no build/ dir):
#   kernelsu/kernelsu.ko    - symlink -> kernelsu/src/kernel/kernelsu.ko (STRIP=0)
#                             or stripped copy (STRIP=1, ~325KB)
#   kernelsu/kernelsu_ko.o  - objcopy'd object linked into the binary
#
#   make                 # build module + ghostlock54
#   make STRIP=0         # keep debug info in the embedded ko (bigger binary)
#   make module          # build module only
#   make clean

KERNEL_DIR  ?= kernel
MODULE_SH   := kernelsu/build.sh
KO_SRC      := kernelsu/src/kernel/kernelsu.ko
KO          := kernelsu/kernelsu.ko
KO_OBJ      := kernelsu/kernelsu_ko.o
BIN         := ghostlock54
CC          ?= clang
CFLAGS      ?= -O2 -pthread
STRIP       ?= 1

.PHONY: all module clean

all: module $(BIN)

module:
	KERNEL_DIR=$(KERNEL_DIR) sh $(MODULE_SH)
	@echo "[make] module -> $(KO_SRC)"

$(BIN): ghostlock54.c module
	@if [ "$(STRIP)" = "1" ]; then \
	  rm -f $(KO); \
	  llvm-strip --strip-debug -o $(KO) $(KO_SRC); \
	else \
	  ln -sfn $(CURDIR)/$(KO_SRC) $(KO); \
	fi
	@echo "[make] embedded ko: $$(stat -Lc %s $(KO)) bytes"
	llvm-objcopy -I binary -O elf64-littleaarch64 -B aarch64 $(KO) $(KO_OBJ)
	$(CC) $(CFLAGS) ghostlock54.c $(KO_OBJ) -o $(BIN)
	@echo "[make] built: $(BIN) ($$(stat -c %s $(BIN)) bytes)"

clean:
	rm -f $(KO) $(KO_OBJ) $(BIN)
	-$(MAKE) -C $(KERNEL_DIR) M=$(CURDIR)/kernelsu/src/kernel src=$(CURDIR)/kernelsu/src/kernel clean
