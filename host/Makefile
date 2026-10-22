# Desktop preview harness. Examples:
#   make -C host preview SCENES=7          -> host/out/s07.gif
#   make -C host preview SCENES="0 16" P=1 -> 1-bit dither previews
#   make -C host preview SCENES=all S=10
#   make -C host preview SCENES=9 KEEP_RAW=1 && python3 host/sheet.py host/out/s09.raw 25 x.png 0.2 1.0

ROOT := ..
BUILD := build
OUT := out
SCENES ?= all
P ?= 4
FPS ?= 25
S ?= 20
KEEP_RAW ?= 0

SRC := $(wildcard $(ROOT)/src/gfx/*.c) $(wildcard $(ROOT)/src/scenes/*.c) \
       $(ROOT)/src/display/dither.c main.c plat_host.c
CFLAGS ?= -O2 -g -Wall -Wextra -std=gnu11 -fsanitize=address,undefined
CFLAGS += -I$(BUILD) -Wno-missing-field-initializers

$(BUILD)/sr_host: $(SRC) $(BUILD)/font.h $(wildcard $(ROOT)/src/*/*.h) $(ROOT)/src/plat.h
	$(CC) $(CFLAGS) -o $@ $(SRC) -lm

$(BUILD)/font.h: gen_font.py
	@mkdir -p $(BUILD)
	python3 -I gen_font.py $@

preview: $(BUILD)/sr_host
	@mkdir -p $(OUT)
	$(BUILD)/sr_host -p $(P) -f $(FPS) -s $(S) -o $(OUT) -a $(ROOT)/assets $(SCENES) | \
	while read raw frames fps name; do \
		python3 -I to_gif.py $$raw $${raw%.raw}.gif $$fps && \
		{ [ "$(KEEP_RAW)" = 1 ] || rm $$raw; }; \
	done

clean:
	rm -rf $(BUILD) $(OUT)

.PHONY: preview clean
