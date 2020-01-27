include ../../py/mkenv.mk

PORT ?= /dev/ttyACM0

# qstr definitions (must come before including py.mk)
QSTR_DEFS = qstrdefsport.h

# include py core make definitions
include $(TOP)/py/py.mk

CROSS_COMPILE = avr-

INC += -I.
INC += -I$(TOP)
INC += -I$(BUILD)

CFLAGS = $(INC) -Wall -Os -g -flto -fuse-linker-plugin -ffunction-sections -fdata-sections -Wl,--gc-sections,--relax -mmcu=atmega2560 -DNDEBUG
CFLAGS += -DF_CPU=16000000
# those are actually passed to GCC, hence the -Wl
LDFLAGS = -Wl,--gc-sections,--relax,--emit-relocs

LIBS =

SRC_C = \
	main.c \
	uart_core.c \
	lib/utils/stdout_helpers.c \
	lib/utils/pyexec.c \
	lib/libc/string0.c \
	lib/mp-readline/readline.c

OBJ = $(PY_O) $(addprefix $(BUILD)/, $(SRC_C:.c=.o))

all: $(BUILD)/firmware.hex

$(BUILD)/firmware.elf: $(OBJ)
	$(ECHO) "LINK $@"
	$(Q)$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^
	$(Q)$(SIZE) $@

$(BUILD)/firmware.hex: $(BUILD)/firmware.elf
	sed -i 's/!!!/@@@/g' $^
	$(Q)$(OBJCOPY) -j .text -j .data -O ihex $^ $@

flash: $(BUILD)/firmware.hex
	avrdude -p atmega2560 -P $(PORT) -b 115200 -c wiring -D -U flash:w:$^

include $(TOP)/py/mkrules.mk
