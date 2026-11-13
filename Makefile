# Build kernelsu.ko and ghostlock54 (single file, ko embedded)
#
# Artifacts (all next to the module sources, no build/ dir):
#   kernelsu/src/kernel/kernelsu.ko           - module build output (with debug info)
#   kernelsu/src/kernel/kernelsu.stripped.ko  - stripped copy embedded into ghostlock54
#                                               (STRIP=0: symlink to kernelsu.ko)
#   kernelsu/src/kernel/kernelsu_ko.o         - blob object linked into the binary
#
#   make                 # build module + ghostlock54
#   make STRIP=0         # keep debug info in the embedded ko (bigger binary)
#   make module          # build module only
#   make clean

KERNEL_DIR  ?= kernel
MODULE_SH   := kernelsu/build.sh
KO          := kernelsu/src/kernel/kernelsu.ko
KO_STRIP    := kernelsu/src/kernel/kernelsu.stripped.ko
KO_OBJ      := kernelsu/src/kernel/kernelsu_ko.o
BIN         := ghostlock54
CC          ?= clang
CFLAGS      ?= -O2 -pthread
STRIP       ?= 1

.PHONY: all module clean

all: module $(BIN)

module:
	KERNEL_DIR=$(KERNEL_DIR) sh $(MODULE_SH)
	@echo "[make] module -> $(KO)"

$(BIN): ghostlock54.c module
	@if [ "$(STRIP)" = "1" ]; then \
	  rm -f $(KO_STRIP); \
	  llvm-strip --strip-debug -o $(KO_STRIP) $(KO); \
	else \
	  ln -sfn $(CURDIR)/$(KO) $(KO_STRIP); \
	fi
	@echo "[make] embedded ko: $$(stat -Lc %s $(KO_STRIP)) bytes"
	llvm-objcopy -I binary -O elf64-littleaarch64 -B aarch64 \
	  --redefine-sym _binary_kernelsu_stripped_ko_start=_binary_kernelsu_ko_start \
	  --redefine-sym _binary_kernelsu_stripped_ko_end=_binary_kernelsu_ko_end \
	  --redefine-sym _binary_kernelsu_stripped_ko_size=_binary_kernelsu_ko_size \
	  $(KO_STRIP) $(KO_OBJ)
	$(CC) $(CFLAGS) ghostlock54.c $(KO_OBJ) -o $(BIN)
	@echo "[make] built: $(BIN) ($$(stat -c %s $(BIN)) bytes)"

clean:
	rm -f $(KO_STRIP) $(KO_OBJ) $(BIN)
	-$(MAKE) -C $(KERNEL_DIR) M=$(CURDIR)/kernelsu/src/kernel src=$(CURDIR)/kernelsu/src/kernel clean
