# SDCC project for CA51F2 / CA51F253L3 (thermostat display MCU).
# Requires C:\SDCC\bin (sdcc, packihx, makebin) and GNU make in PATH.
#
# Usage:
#   make            build build/osrctherm_r02_v<ver>_CA51F253L3.{ihx,hex,bin}
#   make clean      remove build products
#   make flash-cachip [PORT=COMx]   build + upload via tools/cachip-isp-serial_new.py
# (target 'all' is the default; root-level makefile delegates here)

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

# Make the toolchain reachable even when the invoking IDE (e.g. Eclipse) does
# not include it in PATH. Recipes run under MSYS /bin/sh, so the prepended
# entries use POSIX form (':' separated); 'C:/x' maps to '/c/x'.
TC_SDCC   := C:/SDCC/bin
TC_TELINK := C:/TelinkIoTStudio/bin
export PATH := $(patsubst C:%,/c%,$(TC_SDCC)):$(patsubst C:%,/c%,$(TC_TELINK)):$(PATH)

# memory layout of the CA51F253L3
CONFIG_H  := include/config.h
FLASH_CODE_SIZE := $(shell sed -n '/define FLASH_CODE_SIZE /s/.*  *//p' $(CONFIG_H))
SDCCFLAGS := -mmcs51 --model-large --opt-code-size --iram-size 256 --xram-size 2048 --code-size $(FLASH_CODE_SIZE) -I. -Iinclude -I.. -I../include_common

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
FLASHER_SRC   := src/flasher/flasher.c
FLASHER_IHX   := $(BUILD)/flasher.ihx
FLASHER_BLOB  := $(BUILD)/flasher_blob.c
FLASHER_FLAGS := -mmcs51 --model-small --opt-code-size --iram-size 256 --xram-size 0x01F0 \
                 --code-loc 0x8000 --code-size 2048 --xram-loc 0x0600 -I. -Iinclude

RELS  := $(patsubst src/%.c,$(BUILD)/%.rel,$(SRCS)) $(BUILD)/flasher_blob.rel

OUT := $(BUILD)/$(PROJ)
COMMON_BIN := ../bin

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
	