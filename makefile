# GNU make on Windows/MSYS: $(MAKE) expands to an absolute Windows path
# ("C:/TelinkSDK/bin/make.exe") which the MSYS /bin/sh used in recipes cannot
# execute. Recurse by the bare 'make' name so the shell resolves it via PATH.
MAKE := make

subdir_ca51f2 := CA51F253L3
subdir_tlsr := TLSR8258

.PHONY: all subdir_build clean flash-cachip flash-orig-read-cachip flash-orig-write-cachip
all: subdir_build
subdir_build:
	$(MAKE) -C $(subdir_ca51f2) all
	$(MAKE) -C $(subdir_tlsr) all

bootloader:
	$(MAKE) -C $(subdir_tlsr) -f makefile.bootloader

bootloader-flash:
	$(MAKE) -C $(subdir_tlsr) -f makefile.bootloader bootloader-flash

clean:
	$(MAKE) -C $(subdir_ca51f2) clean
	$(MAKE) -C $(subdir_tlsr) clean
	
flash-cachip:
	$(MAKE) -C $(subdir_ca51f2) flash-cachip
	
flash-orig-read-cachip:
	$(MAKE) -C $(subdir_ca51f2) flash-orig-read-cachip

flash-orig-write-cachip:
	$(MAKE) -C $(subdir_ca51f2) flash-orig-write-cachip

flash-erase-cachip:
	$(MAKE) -C $(subdir_ca51f2) flash-erase-cachip
