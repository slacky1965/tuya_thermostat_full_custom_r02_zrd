# Build for the CA51F2 / CA51F253L3 (thermostat display MCU).
#
# Two compilers are supported:
#   * clang (DEFAULT on this branch) - the mcs51-llvm toolchain
#     (C:/opt/mcs51-llvm-toolchain-windows-amd64): clang --target=mcs51,
#     ld.lld -m elf32-mcs51, llvm-objcopy. LTO is on by default (LTO=0
#     disables it). Stage 1: the application only; the flasher blob is still
#     produced by SDCC and linked in as data (byte-identical to the proven
#     SDCC blob), so the OTA image stays complete.
#   * sdcc - the legacy toolchain (C:/SDCC/bin), unchanged behavior.
#
# Switching compilers requires `make clean` (objects and intermediate
# formats differ). Flashing targets work with either compiler.
#
# Usage:
#   make                        build build/osrctherm_r02_v<ver>_CA51F253L3.{bin,...}
#   make COMPILER=sdcc          same via SDCC
#   make LTO=0                  clang without LTO (size A/B comparison)
#   make clean                  remove build products
#   make flash-cachip [PORT=COMx]   build + upload via tools/cachip-isp-serial_new.py
# (target 'all' is the default; root-level makefile delegates here)

COMPILER ?= clang
LTO ?= 1
# LTO optimisation level for ld.lld (--lto-O<n>). MEASURED 2026-10-10:
# --lto-O1 (the first guess) costs 891 B; O2 == O3 == default (smallest),
# O0 == O1. Compile stays -Oz: -Os overflows the flash by ~3.7 KB on this
# backend. Keep the value overridable for size A/B.
LTO_O ?= --lto-O2

SDCC     := sdcc
PACKIHX  := packihx
MAKEBIN  := makebin
CP       := cp

# Resolve 'sh' from PATH. This make.exe would otherwise reuse the legacy
# MSYS-1 sh.exe sitting in its own directory (C:/TelinkSDK/bin), whose argv
# quoting is incompatible with the MSYS-2 sed shipped with Git for Windows:
# the sed script arrives corrupted, APP_VER_HEX comes out empty and parsing
# stops at 'APP_VERSION not found'. With SHELL set, the shell and the sed
# always come from the same PATH (Git sh in Git Bash, Git sh in PowerShell).
SHELL := sh

# Make the toolchains reachable even when the invoking IDE (e.g. Eclipse) does
# not include it in PATH. Recipes run under MSYS /bin/sh, so the prepended
# entries use POSIX form (':' separated); 'C:/x' maps to '/c/x'.
LLVM_TC   := C:/opt/mcs51-llvm-toolchain-windows-amd64/bin
TC_SDCC   := C:/SDCC/bin
TC_TELINK := C:/TelinkIoTStudio/bin
export PATH := $(patsubst C:%,/c%,$(LLVM_TC)):$(patsubst C:%,/c%,$(TC_SDCC)):$(patsubst C:%,/c%,$(TC_TELINK)):$(PATH)

# memory layout of the CA51F253L3
CONFIG_H  := include/config.h
FLASH_CODE_SIZE := $(shell sed -n '/define FLASH_CODE_SIZE /s/.*  *//p' $(CONFIG_H))

# APP_VERSION from config.h, formatted for the file name. APP_VERSION is a BCD
# u16 (hi byte = major, lo byte = minor; 0x100 = v1.00), so the low byte prints
# as two hex digits and the high byte without padding.
# Two gotchas: (1) a '#' inside a $(shell ...) call starts a make comment and
# truncates the line, so the sed pattern anchors on 'define APP_VERSION' only;
# (2) the FLASH_CODE_SIZE pattern above cannot be reused - the APP_VERSION line
# carries a trailing comment whose inner blank runs would be eaten by '.*  *'.
APP_VER_HEX := $(shell sed -n '/define APP_VERSION /s/.*[[:space:]]\+\(0x[0-9A-Fa-f]\+\).*/\1/p' $(CONFIG_H))
ifeq ($(APP_VER_HEX),)
$(error APP_VERSION not found in $(CONFIG_H))
endif
APP_VER_STR := $(shell printf 'v%X.%02X' $$(( ($(APP_VER_HEX)) >> 8 )) $$(( ($(APP_VER_HEX)) & 255 )))

# output project/device name (build/<PROJ>.<ihx|hex|bin>), versioned
PROJ := osrctherm_r02_$(APP_VER_STR)_CA51F253L3

# Flashing (Cachip ISP script in the project tools dir).
#   make flash-cachip              -> uploads to PORT (below)
#   make flash-cachip PORT=COM7    -> uploads to COM7
PYTHON     ?= py
PORT       ?= COM4
CACHIP_ISP := ./tools/cachip-isp-serial_new.py
MAKE_OTA   := ./tools/make_ca51f2_ota.py
# Prints used/limit from the .mem file when a link overflows (sdld writes the
# numbers there but prints only the bare "?ASlink-Error-Insufficient" lines).
MEM_REPORT := ./tools/mem_report.py
ORIG_FW	   := ../bin/orig/firmware_CA51F2_orig.bin

BUILD := build
SRCS  := $(wildcard src/*.c)
HDRS  := $(wildcard include/*.h) $(wildcard ../include_common/*.h)

# XRAM flasher: a separate small program linked at code 0x8000, converted to a
# C byte array (build/flasher_blob.c) that the app copies into XRAM at runtime.
# clang (stage 2): flasher.c is compiled AND linked by clang/lld; COMPILER=sdcc
# keeps the legacy SDCC flasher chain as the rollback path.
FLASHER_SRC   := src/flasher/flasher.c
FLASHER_IHX   := $(BUILD)/flasher.ihx
FLASHER_BLOB  := $(BUILD)/flasher_blob.c
FLASHER_FLAGS := -mmcs51 --model-small --opt-code-size --iram-size 256 --xram-size 0x01F0 \
                 --code-loc 0x8000 --code-size 2048 --xram-loc 0x0600 -I. -Iinclude

OUT := $(BUILD)/$(PROJ)
COMMON_BIN := ../bin

ifeq ($(COMPILER),clang)

# --- mcs51-llvm (clang) path -------------------------------------------------
CLANG     := clang
LLD       := ld.lld
OBJCOPY   := llvm-objcopy
LLVM_MCS51 := $(patsubst C:%,/c%,$(LLVM_TC))/../lib/clang/23/mcs51

# SFR/XSFR headers + linker symbols generated from the SDCC headers (single
# source of truth). build/gen/include is on the path BEFORE include/, so the
# generated headers shadow "include/ca51f2sfr.h" / "include/ca51f2xsfr.h".
GEN_DIR   := $(BUILD)/gen
GEN_STAMP := $(GEN_DIR)/.stamp
GEN_OUT   := $(GEN_DIR)/include/ca51f2sfr.h $(GEN_DIR)/include/ca51f2xsfr.h \
             $(GEN_DIR)/sfr_symbols.ld
GEN_SRC   := tools/gen_clang_sfr.py include/ca51f2sfr.h include/ca51f2xsfr.h

# -Wno-gcc-compat: the SDCC-style trailing __interrupt(n) on a definition.
# -Wno-unknown-pragmas: SDCC pragmas (#pragma disable_warning) stay in sources.
# -Wno-unused-but-set-*: debug-gated statics (RTC_EN off) look unused to clang.
CLANG_FLAGS := --target=mcs51 -Oz -Wall -Wno-gcc-compat -Wno-unknown-pragmas \
               -Wno-unused-but-set-variable -Wno-unused-but-set-global \
               -ffunction-sections -fdata-sections \
               -I$(GEN_DIR) -I. -Iinclude -I.. -I../include_common \
               -include include/mcs51_compat.h
ifeq ($(LTO),1)
CLANG_FLAGS += -flto
endif

LD_SCRIPTS := ld/ca51f253l3.ld $(GEN_DIR)/sfr_symbols.ld
RUNTIME_LIB ?= $(LLVM_MCS51)/libmcs51builtins.a
OBJS := $(patsubst src/%.c,$(BUILD)/%.o,$(SRCS)) \
        $(BUILD)/flasher_blob.o \
        $(BUILD)/startup_clang.o

all: $(OUT).bin

$(BUILD):
	@mkdir -p $(BUILD)

$(GEN_STAMP): $(GEN_SRC) | $(BUILD)
	$(PYTHON) ./tools/gen_clang_sfr.py --sfr include/ca51f2sfr.h \
	  --xsfr include/ca51f2xsfr.h --outdir $(GEN_DIR)
	@touch $@

$(GEN_OUT): $(GEN_STAMP)

$(BUILD)/%.o: src/%.c $(HDRS) $(GEN_STAMP) | $(BUILD)
	$(CLANG) $(CLANG_FLAGS) -c $< -o $@

$(BUILD)/startup_clang.o: src/startup_clang.s | $(BUILD)
	$(CLANG) --target=mcs51 -c $< -o $@

# OTA flasher via clang (stage 2): own ld profile + startup mirroring the SDCC
# flasher flags (--code-loc 0x8000 / --xram-loc 0x0600); the blob chain below
# is shared with the SDCC path, so the clang build invokes NO SDCC at all.
$(BUILD)/flasher.o: $(FLASHER_SRC) $(HDRS) $(GEN_STAMP) | $(BUILD)
	$(CLANG) $(CLANG_FLAGS) -c $< -o $@

$(BUILD)/startup_flasher_clang.o: src/startup_flasher_clang.s | $(BUILD)
	$(CLANG) --target=mcs51 -c $< -o $@

$(BUILD)/flasher.elf: $(BUILD)/flasher.o $(BUILD)/startup_flasher_clang.o \
                     ld/ca51f253l3-flasher.ld $(GEN_OUT) $(RUNTIME_LIB)
	$(LLD) -m elf32-mcs51 -static --gc-sections --no-check-sections \
	  $(if $(filter 1,$(LTO)),$(LTO_O)) \
	  -T ld/ca51f253l3-flasher.ld --Map=$(BUILD)/flasher.map \
	  -o $@ $(BUILD)/flasher.o $(BUILD)/startup_flasher_clang.o $(RUNTIME_LIB)

$(FLASHER_IHX): $(BUILD)/flasher.elf
	$(OBJCOPY) -O ihex $< $@

$(FLASHER_BLOB): $(FLASHER_IHX) ./tools/make_flasher_blob.py
	$(PYTHON) ./tools/make_flasher_blob.py $< $@

$(BUILD)/flasher_blob.o: $(FLASHER_BLOB) $(HDRS) | $(BUILD)
	$(CLANG) $(CLANG_FLAGS) -c $< -o $@

$(OUT).elf: $(OBJS) $(LD_SCRIPTS)
	$(LLD) -m elf32-mcs51 -static --gc-sections --no-check-sections \
	  $(if $(filter 1,$(LTO)),$(LTO_O)) \
	  -T ld/ca51f253l3.ld \
	  --Map=$(OUT).map \
	  -o $@ $(OBJS) $(RUNTIME_LIB)

$(OUT).hex: $(OUT).elf
	$(OBJCOPY) -O ihex $< $@

$(OUT).bin: $(OUT).elf
	$(OBJCOPY) -O binary $< $@
	@echo "bin size: `wc -c < $@` bytes"
	-$(CP) $@ $(COMMON_BIN)/
	$(PYTHON) $(MAKE_OTA) $@
	-$(CP) $(BUILD)/*.zigbee $(COMMON_BIN)/

else ifeq ($(COMPILER),sdcc)

# --- legacy SDCC path (unchanged behavior) -----------------------------------
SDCCFLAGS := -mmcs51 --model-large --opt-code-size --iram-size 256 --xram-size 2048 --code-size $(FLASH_CODE_SIZE) -I. -Iinclude -I.. -I../include_common

RELS  := $(patsubst src/%.c,$(BUILD)/%.rel,$(SRCS)) $(BUILD)/flasher_blob.rel

all: $(OUT).bin

$(BUILD):
	@mkdir -p $(BUILD)

$(BUILD)/%.rel: src/%.c $(HDRS) | $(BUILD)
	$(SDCC) $(SDCCFLAGS) -c $< -o $@

$(FLASHER_IHX): $(FLASHER_SRC) $(HDRS) | $(BUILD)
	@rm -f $(FLASHER_IHX:.ihx=.mem)
	$(SDCC) $(FLASHER_FLAGS) $< -o $@ || { $(PYTHON) $(MEM_REPORT) $(FLASHER_IHX:.ihx=.mem); exit 1; }

$(FLASHER_BLOB): $(FLASHER_IHX) ./tools/make_flasher_blob.py
	$(PYTHON) ./tools/make_flasher_blob.py $< $@

$(BUILD)/flasher_blob.rel: $(FLASHER_BLOB) $(HDRS) | $(BUILD)
	$(SDCC) $(SDCCFLAGS) -c $< -o $@

$(OUT).ihx: $(RELS)
	@rm -f $(OUT).mem
	$(SDCC) $(SDCCFLAGS) $(RELS) -o $@ || { $(PYTHON) $(MEM_REPORT) $(OUT).mem; exit 1; }

$(OUT).hex: $(OUT).ihx
	$(PACKIHX) $(OUT).ihx > $(OUT).hex

$(OUT).bin: $(OUT).hex
	$(MAKEBIN) -p $(OUT).hex $(OUT).bin
	@echo "bin size: `wc -c < $(OUT).bin` bytes"
	-$(CP) $(OUT).bin $(COMMON_BIN)/
	$(PYTHON) $(MAKE_OTA) $(OUT).bin
	-$(CP) $(BUILD)/*.zigbee $(COMMON_BIN)/

else
$(error COMPILER must be 'clang' or 'sdcc' (got '$(COMPILER)'))
endif

.PHONY: clean
clean:
	-$(RM) -r $(BUILD)
	-$(RM) $(COMMON_BIN)/$(PROJ).bin
	-$(RM) $(COMMON_BIN)/6565-*-$(PROJ).zigbee

.PHONY: flash-cachip
flash-cachip: $(OUT).bin
	$(PYTHON) $(CACHIP_ISP) $(PORT) upload $(OUT).bin

flash-orig-write-cachip:
	$(PYTHON) $(CACHIP_ISP) $(PORT) upload $(ORIG_FW)

flash-orig-read-cachip:
	$(PYTHON) $(CACHIP_ISP) $(PORT) download ca51f223l3_TZE204_edl8pz1k.bin
	
flash-erase-cachip:
	$(PYTHON) $(CACHIP_ISP) $(PORT) erase
	
ota-cachip:
	$(PYTHON) $(MAKE_OTA) $(OUT).bin
	-$(CP) $(BUILD)/*.zigbee $(COMMON_BIN)/
