DIR = build
PROG = tn84-lp-ht

CC = avr-gcc
OBJCOPY = avr-objcopy
OBJDUMP = avr-objdump
OBJSIZE = avr-size
CPPFILT = avr-c++filt

CFLAGS = -I. -Itn84-nrf24l01
CFLAGS += -Os -Wall -MMD -mrelax -mmcu=attiny84 -DF_CPU=1000000L -D__PROG_TYPES_COMPAT__

LDFLAGS = -mmcu=attiny84 -Wl,--gc-sections,-Map=$(DIR)/$(PROG).map

.PHONY: size clean

all: $(DIR) $(DIR)/$(PROG).elf $(DIR)/$(PROG).hex $(DIR)/$(PROG).lst size

$(DIR):
	mkdir -p $(DIR)

$(DIR)/main.o: main.c *.h
	$(CC) $(CFLAGS) -c main.c -o $(DIR)/main.o

$(DIR)/$(PROG).elf: $(DIR)/main.o
	$(CC) $(LDFLAGS) $(DIR)/*.o $(LDLIBS) -o $(DIR)/$(PROG).elf

$(DIR)/$(PROG).hex: $(DIR)/$(PROG).elf
	$(OBJCOPY) -R .eeprom -O ihex $(DIR)/$(PROG).elf $(DIR)/$(PROG).hex

$(DIR)/$(PROG).lst: $(DIR)/$(PROG).elf
	$(OBJDUMP) -xDS $(DIR)/$(PROG).elf | $(CPPFILT) > $(DIR)/$(PROG).lst

size:
	@echo "\nFiles: \n\t$(DIR)/$(PROG).elf\t$(DIR)/$(PROG).map\n\t$(DIR)/$(PROG).hex\t$(DIR)/$(PROG).lst\n"
	$(OBJSIZE) --mcu=attiny84 -C --format=avr $(DIR)/$(PROG).elf

clean:
	rm -rf $(DIR)
