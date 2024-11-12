# Name: Makefile
# Project: Smart-Response-PE/examples/hello-world-receiver
# Author: Stephen Erisman <github@serisman.com>
# Creation Date: 2018-09-21
# License: MIT

# ---------------------------------------------------------------------

DEVICE = CC2533
F_CPU = 32000000

# ---------------------------------------------------------------------
BUILD		?= Opticon_2.9_BWRY


OUTPUT_NAME = $(BUILD)


XRAM_LOC = 0x0000
XRAM_SIZE = 0x1700 #0x1800 (removed last 256 bytes! Last 0x100 is idata mapped into xmem, or something)
HEAP_SIZE = 5275 # maximize me!

ROOT_DIR = .
BUILD_DIR = .build
OUTPUT_DIR = .output


include tagtypes/$(BUILD)/make.mk

FLAGS += -Ilib
FLAGS += -Itagtypes/$(BUILD)

OUTPUT = $(OUTPUT_DIR)/$(OUTPUT_NAME)

LIB_SOURCES = $(wildcard $(ROOT_DIR)/lib/*.c) $(wildcard $(ROOT_DIR)/lib/**/*.c)
LIB_OBJECTS = $(patsubst $(ROOT_DIR)/lib/%.c,$(BUILD_DIR)/lib/%.rel,$(LIB_SOURCES))

#SOURCES = $(wildcard $(ROOT_DIR)/lib/*.c) $(wildcard $(ROOT_DIR)/lib/**/*.c)
#OBJECTS = $(patsubst $(ROOT_DIR)/lib/%.c,$(BUILD_DIR)/lib/%.rel,$(LIB_SOURCES))


#SOURCES = _heap.c
SOURCES = main.c
#SOURCES = _heap.c

SOURCES += tagtypes/$(BUILD)/screen.c
SOURCES += tagtypes/$(BUILD)/userinterface.c


OBJECTS = $(patsubst %.c,$(BUILD_DIR)/%.rel,$(SOURCES))

# http://sdcc.sourceforge.net/doc/sdccman.pdf
COMPILE = sdcc -mmcs51 -c --std-sdcc11 --opt-code-size -D$(DEVICE) -DHEAP_SIZE=$(HEAP_SIZE) -DF_CPU=$(F_CPU) -I. -I$(ROOT_DIR)/lib --peep-file lib/cc253x/peep.def --fomit-frame-pointer --code-loc 0x0000 --code-size 0x18000 --model-medium $(FLAGS)
BUILD_LIB = sdar -rc
LINK = sdcc --debug -mmcs51 --xram-loc $(XRAM_LOC) --xram-size $(XRAM_SIZE) --code-loc 0x0000 --code-size 0x18000 --model-medium -Wl-r #-Wl-bBANK0=0x00000 -Wl-r #-Wl-bBANK2=0x28000 -Wl-r --model-medium #-Wl-bBANK0=0x00000 -Wl-r

# symbolic targets:
all: size

print-%: ; @echo $* = $($*)

$(BUILD_DIR)/%.rel: %.c
	@mkdir -p $(dir $@)
	$(COMPILE) -o $@ $<

$(BUILD_DIR)/lib/%.rel: $(ROOT_DIR)/lib/%.c
	@mkdir -p $(dir $@)
	$(COMPILE) -o $@ $<

$(BUILD_DIR)/lib.lib: $(LIB_OBJECTS)
	$(BUILD_LIB) $@ $(LIB_OBJECTS)

$(OUTPUT).ihx: $(OBJECTS) $(BUILD_DIR)/lib.lib
	@mkdir -p $(dir $@)
	$(LINK) --out-fmt-ihx -o $(OUTPUT).ihx $(OBJECTS) $(BUILD_DIR)/lib.lib

$(OUTPUT).hex: $(OUTPUT).ihx
	packihx $(OUTPUT).ihx > $(OUTPUT).hex

$(OUTPUT).bin: $(OUTPUT).ihx
#	makebin $(OUTPUT).ihx > $(OUTPUT).bin

size: $(OUTPUT).bin
	@echo '---------- Segments ----------'
	@egrep '(ABS,CON)|(REL,CON)' $(OUTPUT).map | gawk --non-decimal-data '{dec = sprintf("%d","0x" $$2); print dec " " $$0}' | /usr/bin/sort -n -k1 | cut -f2- -d' ' | uniq
	@echo '---------- Memory ----------'
	@egrep 'available|EXTERNAL|FLASH' $(OUTPUT).mem
	@cp ${OUTPUT}.ihx out-unpaged.hex
	@srec_cat -disable-sequence-warning out-unpaged.hex -Intel -crop 0x28000 0x30000 -offset -0x18000 out-unpaged.hex -Intel -crop 0x0000 0x10000  -o bin/fw-$(BUILD).hex -Intel
	@objcopy -Iihex -Obinary bin/fw-$(BUILD).hex bin/ota-$(BUILD).bin
	@python3 scanfilesram.py
	@rm -rf out-unpaged.hex

clean:
	rm -r -f $(BUILD_DIR) $(OUTPUT_DIR)
