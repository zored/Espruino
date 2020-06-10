# Linking the esp8266... The Espruino source files get compiled into the .text section. The
# Espressif SDK libraries have .text and .irom0 sections. We need to put the libraries' .text into
# .iram0 (32KB on chip instruction ram) and we need to put the Esprunio .text and the libraries'
# .irom0 into .irom0 (demand-cached from flash). We do this dance by pre-linking the Espruino
# objects, then renaming .text to .irom0, and then finally linking with the SDK libraries.
# Note that a previous method of renaming .text to .irom0 in each object file doesn't work when
# we enable the link-time optimizer for inlining because it generates fresh code that all ends
# up in .iram0.
# We generate two binaries in order to support over-the-air updates, one per
# OTA partition (Espressif calls these user1.bin and user2.bin). In the 512KB flash case, there
# is only space for the first binary and updates are not possible. So we're really abusing the
# flash layout in that case because we tell the SDK that we have two 256KB partitions when in
# reality we're using one 512KB partition. This works out because the SDK doesn't use the
# user setting area that sits between the two 256KB partitions, so we can merrily use it for
# code.
ESP_ZIP     = $(PROJ_NAME).tgz
USER1_BIN    = espruino_esp8266_user1.bin
USER2_BIN    = espruino_esp8266_user2.bin
USER1_ELF    = espruino_esp8266_user1.elf
USER2_ELF    = espruino_esp8266_user2.elf
PARTIAL      = espruino_esp8266_partial.o

ifdef FLASH_4MB
ESP_COMBINED_SIZE = 4096
ESP_FLASH_ADDONS  = $(ET_DEFAULTS) $(INIT_DATA) $(ET_BLANK) $(BLANK)
LD_SCRIPT1   = ./targets/esp8266/eagle.app.v6.new.2048.ld
LD_SCRIPT2   = ./targets/esp8266/eagle.app.v6.new.2048.ld
else
ESP_COMBINED_SIZE = 512
LD_SCRIPT1   = ./targets/esp8266/eagle.app.v6.new.1024.app1.ld
LD_SCRIPT2   = ./targets/esp8266/eagle.app.v6.new.1024.app2.ld
endif

ifdef FLASH_1MB
ESP_COMBINED_SIZE = 1024
endif

LD_RENAME    = --rename-section .text=.irom.text --rename-section .literal=.irom.literal
ESP_COMBINED = $(PROJ_NAME)_combined_$(ESP_COMBINED_SIZE).bin
APPGEN_TOOL  = $(ESP8266_SDK_ROOT)/tools/gen_appbin.py
BOOTLOADER   = $(ESP8266_SDK_ROOT)/bin/boot_v1.6.bin
BLANK        = $(ESP8266_SDK_ROOT)/bin/blank.bin
INIT_DATA    = $(ESP8266_SDK_ROOT)/bin/esp_init_data_default.bin

proj: $(USER1_BIN) $(USER2_BIN) $(ESP_ZIP)
combined: $(ESP_COMBINED)


# generate partially linked .o with all Esprunio source files linked
$(PARTIAL): $(OBJS) $(LINKER_FILE)
	@echo LD $@
ifdef USE_CRYPTO
	$(Q)$(OBJCOPY) --rename-section .rodata=.irom0.text libs/crypto/mbedtls/library/sha1.o
ifdef USE_SHA256
	$(Q)$(OBJCOPY) --rename-section .rodata=.irom0.text libs/crypto/mbedtls/library/sha256.o
endif
ifdef USE_SHA512
	$(Q)$(OBJCOPY) --rename-section .rodata=.irom0.text libs/crypto/mbedtls/library/sha512.o
endif
endif
	$(Q)$(LD) $(OPTIMIZEFLAGS) -nostdlib -Wl,--no-check-sections -Wl,-static -r -o $@ $(OBJS)
	$(Q)$(OBJCOPY) $(LD_RENAME) $@

# generate fully linked 'user1' .elf using linker script for first OTA partition
$(USER1_ELF): $(PARTIAL) $(LINKER_FILE)
	@echo LD $@
	$(Q)$(LD) $(LDFLAGS) -T$(LD_SCRIPT1) -o $@ $(PARTIAL) -Wl,--start-group $(LIBS) -Wl,--end-group
	$(Q)$(OBJDUMP) --headers -j .data -j .rodata -j .bss -j .irom0.text -j .text $@ | tail -n +4
	@echo To disassemble: $(OBJDUMP) -d -l -x $@
	$(OBJDUMP) -d -l -x $@ >espruino_esp8266_user1.lst

# generate fully linked 'user2' .elf using linker script for second OTA partition
$(USER2_ELF): $(PARTIAL) $(LINKER_FILE)
	@echo LD $@
	$(Q)$(LD) $(LDFLAGS) -T$(LD_SCRIPT2) -o $@ $(PARTIAL) -Wl,--start-group $(LIBS) -Wl,--end-group
	@echo To disassemble: $(OBJDUMP) -d -l -x $@

# generate binary image for user1, i.e. first OTA partition
$(USER1_BIN): $(USER1_ELF)
	$(Q)$(OBJCOPY) --only-section .text -O binary $(USER1_ELF) eagle.app.v6.text.bin
	$(Q)$(OBJCOPY) --only-section .data -O binary $(USER1_ELF) eagle.app.v6.data.bin
	$(Q)$(OBJCOPY) --only-section .rodata -O binary $(USER1_ELF) eagle.app.v6.rodata.bin
	$(Q)$(OBJCOPY) --only-section .irom0.text -O binary $(USER1_ELF) eagle.app.v6.irom0text.bin
	@ls -ls eagle*bin
	$(Q)COMPILE=gcc python $(APPGEN_TOOL) $(USER1_ELF) 2 $(ESP_FLASH_MODE) $(ESP_FLASH_FREQ_DIV) $(ESP_FLASH_SIZE) 0 >/dev/null
	$(Q) rm -f eagle.app.v6.*.bin
	$(Q) mv eagle.app.flash.bin $@
	@echo "** user1.bin uses $$( stat $(STAT_FLAGS) $@) bytes of" $(ESP_FLASH_MAX) "available"
	@if [ $$( stat $(STAT_FLAGS) $@) -gt $$(( $(ESP_FLASH_MAX) )) ]; then echo "$@ too big!"; false; fi

# generate binary image for user2, i.e. second OTA partition
# we make this rule dependent on user1.bin in order to serialize the two rules because they use
# stupid static filenames (go blame the Espressif tool)
$(USER2_BIN): $(USER2_ELF) $(USER1_BIN)
	$(Q)$(OBJCOPY) --only-section .text -O binary $(USER2_ELF) eagle.app.v6.text.bin
	$(Q)$(OBJCOPY) --only-section .data -O binary $(USER2_ELF) eagle.app.v6.data.bin
	$(Q)$(OBJCOPY) --only-section .rodata -O binary $(USER2_ELF) eagle.app.v6.rodata.bin
	$(Q)$(OBJCOPY) --only-section .irom0.text -O binary $(USER2_ELF) eagle.app.v6.irom0text.bin
	$(Q)COMPILE=gcc python $(APPGEN_TOOL) $(USER2_ELF) 2 $(ESP_FLASH_MODE) $(ESP_FLASH_FREQ_DIV) $(ESP_FLASH_SIZE) 1 >/dev/null
	$(Q) rm -f eagle.app.v6.*.bin
	$(Q) mv eagle.app.flash.bin $@

$(ESP_ZIP): $(USER1_BIN) $(USER2_BIN)
	$(Q)rm -rf build/$(basename $(ESP_ZIP))
	$(Q)mkdir -p build/$(basename $(ESP_ZIP))
	$(Q)cp $(USER1_BIN) $(USER2_BIN) scripts/wiflash.sh $(BLANK) \
	  $(INIT_DATA) $(BOOTLOADER) \
	  targets/esp8266/README_flash.txt \
	  targets/esp8266/Makefile \
	  build/$(basename $(ESP_ZIP))
	$(Q)tar -C build -zcf $(ESP_ZIP) ./$(basename $(ESP_ZIP))

# Combined 512k/4096k binary that includes everything that's needed and can be
# flashed to 0 in 512k/4096k parts

$(ESP_COMBINED): $(USER1_BIN) $(USER2_BIN)
	dd if=/dev/zero ibs=1k count=$(ESP_COMBINED_SIZE) | tr "\0x00" "\0xFF" > $@
	dd if=$(BOOTLOADER) of=$@ bs=1 conv=notrunc
	dd bs=1 seek=4096 if=$(USER1_BIN) of=$@ conv=notrunc
	dd bs=1 seek=$(shell printf "%d" $(ET_BLANK)) if=$(BLANK) of=$@ conv=notrunc
	dd bs=1 seek=$(shell printf "%d" $(ET_DEFAULTS)) if=$(INIT_DATA) of=$@ conv=notrunc

# Analyze all the .o files and rank them by the amount of static string area used, useful to figure
# out where to optimize and move strings to flash
# IMPORTANT: this only works if DISABLE_LTO is defined, e.g. `DISABLE_LTO=1 make`
topstrings: $(PARTIAL)
	$(Q)for f in `find . -name \*.o`; do \
	  str=$$($(OBJDUMP) -j .rodata.str1.1 -j .rodata.str1.4 -h $$f 2>/dev/null | \
	    egrep -o 'rodata.str1.. [0-9a-f]+' | \
	    awk $$(expr "$$(awk --version)" : "GNU.*" >/dev/null && echo --non-decimal-data) \
	      -e '{printf "%d\n", ("0x" $$2);}'); \
	  [ "$$str" ] && echo "$$str $$f"; \
	done | \
	sort -rn >topstrings
	$(Q)echo "Top 20 from ./topstrings:"
	$(Q)head -20 topstrings
	$(Q)echo "To get details: $(OBJDUMP) -j .rodata.str1.1 -j .rodata.str1.4 -s src/FILENAME.o"

# Same as topstrings but consider all read-only data
topreadonly: $(PARTIAL)
	$(Q)for f in `find . -name \*.o`; do \
	  str=$$($(OBJDUMP) -j .rodata -h $$f 2>/dev/null | \
	    egrep -o 'rodata +[0-9a-f]+' | \
	    awk $$(expr "$$(awk --version)" : "GNU.*" >/dev/null && echo --non-decimal-data) \
	      -e '{printf "%d\n", ("0x" $$2);}'); \
	  [ "$$str" ] && echo "$$str $$f"; \
	done | \
	sort -rn >topreadonly
	$(Q)echo "Top 20 from ./topreadonly:"
	$(Q)head -20 topreadonly
	$(Q)echo "To get details: $(OBJDUMP) -j .rodata -s src/FILENAME.o"


flash: all $(USER1_BIN) $(USER2_BIN)
ifndef COMPORT
	$(error "In order to flash, we need to have the COMPORT variable defined")
endif
	-$(ESPTOOL) --port $(COMPORT) --baud $(FLASH_BAUD) write_flash --flash_freq $(ET_FF) --flash_mode $(ET_FM) --flash_size $(ET_FS) 0x0000 $(BOOTLOADER) 0x1000 $(USER1_BIN) $(ET_DEFAULTS) $(INIT_DATA) $(ET_BLANK) $(BLANK)


flash_combined: $(ESP_COMBINED)
ifndef COMPORT
	$(error "In order to flash, we need to have the COMPORT variable defined")
endif
	-$(ESPTOOL) --port $(COMPORT) --baud $(FLASH_BAUD) write_flash --flash_freq $(ET_FF) --flash_mode $(ET_FM) --flash_size $(ET_FS) 0x0000 $(ESP_COMBINED) 

# erase flash
flash_erase: .
ifndef COMPORT
	$(error "In order to flash, we need to have the COMPORT variable defined")
endif
	-$(ESPTOOL) --port $(COMPORT) --baud $(FLASH_BAUD) erase_flash

# just flash user1 and don't mess with bootloader or wifi settings
quickflash: all $(USER1_BIN) $(USER2_BIN)
ifndef COMPORT
	$(error "In order to flash, we need to have the COMPORT variable defined")
endif
	-$(ESPTOOL) --port $(COMPORT) --baud $(FLASH_BAUD) write_flash 0x1000 $(USER1_BIN)

wiflash: all $(USER1_BIN) $(USER2_BIN)
ifndef ESPHOSTNAME
	$(error "In order to flash over wifi, we need to have the ESPHOSTNAME variable defined")
endif
	./scripts/wiflash.sh $(ESPHOSTNAME):88 $(USER1_BIN) $(USER2_BIN)
