SPI_FLASH_MODE		?= qio
IMAGE				?= ota
SDKROOT				?= /home/erik/src/esp/opensdk
ESPTOOL				?= ~/bin/esptool
ESPTOOL2			?= ./esptool2
RBOOT				?= ./rboot
HOSTCC				?= gcc
HOSTCPP				?= g++
OTA_HOST			?= esp1

# no user serviceable parts below

section_free	= $(Q) perl -e '\
						open($$fd, "xtensa-lx106-elf-size -A $(1) |"); \
						$$available = $(6) * 1024; \
						$$used = 0; \
						while(<$$fd>) \
						{ \
							chomp; \
							@_ = split; \
							if(($$_[0] eq "$(3)") || ($$_[0] eq "$(4)") || ($$_[0] eq "$(5)")) \
							{ \
								$$used += $$_[1]; \
							} \
						} \
						$$free = $$available - $$used; \
						printf("    %-8s available: %3u k, used: %6u, free: %6u, %2u %%\n", "$(2)" . ":", $$available / 1024, $$used, $$free, 100 * $$free / $$available); \
						close($$fd);'

# use this line if you only want to see your own symbols in the output
#if((hex($$_[2]) > 0) && !m/\.a\(/)

link_debug		= $(Q) perl -e '\
						open($$fd, "< $(1)"); \
						$$top = 0; \
						while(<$$fd>) \
						{ \
							chomp; \
							if(m/^\s+\.$(2)/) \
							{ \
								@_ = split; \
								$$top = hex($$_[1]) if(hex($$_[1]) > $$top); \
								if(hex($$_[2]) > 0) \
								{ \
									$$size = sprintf("%06x", hex($$_[2])); \
									$$file = $$_[3]; \
									$$file =~ s/.*\///g; \
									$$size{"$$size-$$file"} = { size => $$size, id => $$file}; \
								} \
							} \
						} \
						for $$size (sort(keys(%size))) \
						{ \
							printf("%4d: %s\n", \
									hex($$size{$$size}{"size"}), \
									$$size{$$size}{"id"}); \
						} \
						printf("size: %u, free: %u\n", $$top - hex('$(4)'), ($(3) * 1024) - ($$top - hex('$(4)'))); \
						close($$fd);'

CC							:= $(SDKROOT)/xtensa-lx106-elf/bin/xtensa-lx106-elf-gcc
OBJCOPY						:= $(SDKROOT)/xtensa-lx106-elf/bin/xtensa-lx106-elf-objcopy
USER_CONFIG_SECTOR_PLAIN	:= 0x7a
USER_CONFIG_SECTOR_OTA		:= 0xfa
SEQUENCER_FLASH_OFFSET_PLAIN:= 0x076000
SEQUENCER_FLASH_OFFSET_OTA_0:= 0x0f6000
SEQUENCER_FLASH_OFFSET_OTA_1:= 0x1f6000
RFCAL_OFFSET_PLAIN			:= 0x7b000
RFCAL_OFFSET_OTA			:= 0xfb000
RFCAL_FILE					:= $(SDKROOT)/sdk/bin/blank.bin
RF_OFFSET_PLAIN				:= 0x07c000
RF_OFFSET_OTA				:= 0x1fc000
RF_FILE						:= esp_init_data_default_v08.bin
SYSTEM_OFFSET_PLAIN			:= 0x07e000
SYSTEM_OFFSET_OTA			:= 0x1fe000
SYSTEM_FILE					:= $(SDKROOT)/sdk/bin/blank.bin
LDSCRIPT_TEMPLATE			:= loadscript-template
LDSCRIPT					:= loadscript
ELF_PLAIN					:= espiobridge-plain.o
ELF_OTA						:= espiobridge-rboot.o
OFFSET_IRAM_PLAIN			:= 0x000000
OFFSET_IROM_PLAIN			:= 0x010000
OFFSET_OTA_BOOT				:= 0x000000
OFFSET_OTA_RBOOT_CFG		:= 0x001000
OFFSET_OTA_IMG_0			:= 0x002000
OFFSET_OTA_IMG_1			:= 0x102000
FIRMWARE_PLAIN_IRAM			:= espiobridge-plain-iram-$(OFFSET_IRAM_PLAIN).bin
FIRMWARE_PLAIN_IROM			:= espiobridge-plain-irom-$(OFFSET_IROM_PLAIN).bin
FIRMWARE_OTA_RBOOT			:= espiobridge-rboot-boot.bin
FIRMWARE_OTA_IMG			:= espiobridge-rboot-image.bin
CONFIG_RBOOT_SRC			:= rboot-config.c
CONFIG_RBOOT_ELF			:= rboot-config.o
CONFIG_RBOOT_BIN			:= rboot-config.bin
CONFIG_DEFAULT_SRC			:= default-config.c
CONFIG_DEFAULT_ELF			:= default-config.o
CONFIG_DEFAULT_BIN			:= default-config.bin
CONFIG_BACKUP_BIN			:= backup-config.bin
LINKMAP						:= linkmap
SDKLIBDIR					:= $(SDKROOT)/sdk/lib
LIBMAIN_PLAIN				:= main
LIBMAIN_PLAIN_FILE			:= $(SDKROOT)/sdk/lib/lib$(LIBMAIN_PLAIN).a
LIBMAIN_RBB					:= main-rbb
LIBMAIN_RBB_FILE			:= lib$(LIBMAIN_RBB).a
ESPTOOL2_BIN				:= $(ESPTOOL2)/esptool2
RBOOT_BIN					:= $(RBOOT)/firmware/rboot.bin

V ?= $(VERBOSE)
ifeq ($(V),1)
	Q :=
	VECHO := @true
	MAKEMINS :=
else
	Q := @
	VECHO := @echo
	MAKEMINS := -s
endif

ifeq ($(IMAGE),plain)
	IMAGE_OTA := 0
	FLASH_SIZE_ESPTOOL := 4m
	FLASH_SIZE_KBYTES := 512
	RBOOT_SPI_SIZE := 512K
	USER_CONFIG_SECTOR := $(USER_CONFIG_SECTOR_PLAIN)
	SEQUENCER_FLASH_OFFSET_0 := $(SEQUENCER_FLASH_OFFSET_PLAIN)
	SEQUENCER_FLASH_OFFSET_1 := 0x000000
	RFCAL_ADDRESS=$(RFCAL_OFFSET_PLAIN)
	LD_ADDRESS := 0x40210000
	LD_LENGTH := 0x79000
	ELF := $(ELF_PLAIN)
	ALL_TARGETS := $(FIRMWARE_PLAIN_IRAM) $(FIRMWARE_PLAIN_IROM)
	FLASH_TARGET := flash-plain
endif

ifeq ($(IMAGE),ota)
	IMAGE_OTA := 1
	FLASH_SIZE_ESPTOOL := 16m
	FLASH_SIZE_KBYTES := 2048
	RBOOT_SPI_SIZE := 2M
	USER_CONFIG_SECTOR := $(USER_CONFIG_SECTOR_OTA)
	SEQUENCER_FLASH_OFFSET_0 := $(SEQUENCER_FLASH_OFFSET_OTA_0)
	SEQUENCER_FLASH_OFFSET_1 := $(SEQUENCER_FLASH_OFFSET_OTA_1)
	RFCAL_ADDRESS=$(RFCAL_OFFSET_OTA)
	LD_ADDRESS := 0x40202010
	LD_LENGTH := 0xf7ff0
	ELF := $(ELF_OTA)
	ALL_TARGETS := $(FIRMWARE_OTA_RBOOT) $(CONFIG_RBOOT_BIN) $(FIRMWARE_OTA_IMG) otapush espflash resetserial
	FLASH_TARGET := flash-ota
endif

WARNINGS		:= -Wall -Wextra -Werror -Wno-unused-parameter -Wformat=2 -Wuninitialized \
					-Wno-pointer-sign -Wno-div-by-zero -Wfloat-equal -Wno-declaration-after-statement \
					-Wundef -Wshadow -Wpointer-arith -Wbad-function-cast -Wcast-qual -Wwrite-strings \
					-Wsequence-point -Wclobbered -Wlogical-op -Wmissing-field-initializers -Wpacked \
					-Wredundant-decls -Wnested-externs -Wvla -Wdisabled-optimization \
					-Wunreachable-code -Wtrigraphs -Wreturn-type -Wmissing-braces -Wparentheses \
					-Wimplicit -Winit-self -Wformat-nonliteral -Wcomment -Wno-packed \
					-Wstrict-prototypes -Wmissing-prototypes -Wold-style-definition -Wcast-align \
					-Wno-format-security -Wno-format-nonliteral \
					-Wno-error=suggest-attribute=const -Wno-error=suggest-attribute=pure \
					-Wsuggest-attribute=const -Wsuggest-attribute=pure

CFLAGS			:=  -Os -std=gnu11 -mlongcalls -fno-builtin -freorder-blocks -mno-serialize-volatile \
						-D__ets__ -DICACHE_FLASH \
						-DIMAGE_TYPE=$(IMAGE) -DIMAGE_OTA=$(IMAGE_OTA) -DUSER_CONFIG_SECTOR=$(USER_CONFIG_SECTOR) \
						-DRFCAL_ADDRESS=$(RFCAL_ADDRESS) \
						-DSEQUENCER_FLASH_OFFSET=$(SEQUENCER_FLASH_OFFSET_0) \
						-DSEQUENCER_FLASH_OFFSET_0=$(SEQUENCER_FLASH_OFFSET_0) -DSEQUENCER_FLASH_OFFSET_1=$(SEQUENCER_FLASH_OFFSET_1) \
						-DBOOT_BIG_FLASH=1 -DBOOT_RTC_ENABLED=1
HOSTCFLAGS		:= -O3 -lssl -lcrypto
CINC			:= -I$(SDKROOT)/lx106-hal/include -I$(SDKROOT)/xtensa-lx106-elf/xtensa-lx106-elf/include \
					-I$(SDKROOT)/xtensa-lx106-elf/xtensa-lx106-elf/sysroot/usr/include \
					-isystem$(SDKROOT)/sdk/include -I$(RBOOT)/appcode -I$(RBOOT) -I.
LDFLAGS			:= -L . -L$(SDKLIBDIR) -Wl,--gc-sections -Wl,-Map=$(LINKMAP) -nostdlib -u call_user_start -Wl,-static
SDKLIBS			:= -lhal -lpp -lphy -lnet80211 -llwip -lwpa -lcrypto -lm

OBJS			:= application.o config.o display.o display_cfa634.o display_lcd.o display_orbital.o display_saa.o \
						http.o i2c.o i2c_sensor.o io.o io_gpio.o io_aux.o io_mcp.o io_ledpixel.o io_pcf.o ota.o queue.o \
						socket.o stats.o time.o uart.o dispatch.o util.o sequencer.o init.o
OTA_OBJ			:= rboot-bigflash.o rboot-api.o
HEADERS			:= application.h config.h display.h display_cfa634.h display_lcd.h display_orbital.h display_saa.h \
						esp-uart-register.h http.h i2c.h i2c_sensor.h io.h io_gpio.h \
						io_aux.h io_mcp.h io_ledpixel.h io_pcf.h ota.h queue.h stats.h uart.h user_config.h \
						socket.h dispatch.h util.h sequencer.h init.h

.PRECIOUS:		*.c *.h
.PHONY:			all flash flash-plain flash-ota clean free linkdebug always ota

all:			$(ALL_TARGETS) free
				$(VECHO) "DONE $(IMAGE) TARGETS $(ALL_TARGETS) CONFIG SECTOR $(USER_CONFIG_SECTOR)"

clean:
				$(VECHO) "CLEAN"
				$(Q) $(MAKE) $(MAKEMINS) -C $(ESPTOOL2) clean
				$(Q) $(MAKE) $(MAKEMINS) -C $(RBOOT) clean
				$(Q) rm -f $(OBJS) $(OTA_OBJ) \
						$(ELF_PLAIN) $(ELF_OTA) \
						$(FIRMWARE_PLAIN_IRAM) $(FIRMWARE_PLAIN_IROM) \
						$(FIRMWARE_OTA_RBOOT) $(FIRMWARE_OTA_IMG) \
						$(LDSCRIPT) \
						$(CONFIG_RBOOT_ELF) $(CONFIG_RBOOT_BIN) \
						$(CONFIG_DEFAULT_ELF) \
						$(LIBMAIN_RBB_FILE) $(ZIP) $(LINKMAP) otapush espflash resetserial

free:			$(ELF)
				$(VECHO) "MEMORY USAGE"
				$(call section_free,$(ELF),iram,.text,,,32)
				$(call section_free,$(ELF),dram,.bss,.data,.rodata,77)
				$(call section_free,$(ELF),irom,.irom0.text,,,408)

linkdebug:		$(LINKMAP)
				$(Q) echo "IROM:"
				$(call link_debug,$<,irom0.text,424,40210000)
				$(Q) echo "IRAM:"
				$(call link_debug,$<,text,32,40100000)

application.o:		$(HEADERS)
config.o:			$(HEADERS)
display.o:			$(HEADERS)
display_cfa634.o:	$(HEADERS)
display_lcd.o:		$(HEADERS)
display_orbital.o:	$(HEADERS)
display_saa.o:		$(HEADERS)
http.o:				$(HEADERS)
i2c.o:				$(HEADERS)
i2c_sensor.o:		$(HEADERS)
io_aux.o:			$(HEADERS)
io.o:				$(HEADERS)
io_gpio.o:			$(HEADERS)
io_mcp.o:			$(HEADERS)
io_ledpixel.o:		$(HEADERS)
io_pcf.o:			$(HEADERS)
ota.o:				$(HEADERS)
queue.o:			queue.h
stats.o:			$(HEADERS) always
time.o:				$(HEADERS)
uart.o:				$(HEADERS)
dispatch.o:			$(HEADERS)
util.o:				$(HEADERS)
sequencer.o:		$(HEADERS)
$(LINKMAP):			$(ELF_OTA)

$(ESPTOOL2_BIN):
						$(VECHO) "MAKE ESPTOOL2"
						$(Q) $(MAKE) $(MAKEMINS) -C $(ESPTOOL2)

$(RBOOT_BIN):			$(ESPTOOL2_BIN)
						$(VECHO) "MAKE RBOOT"
						$(Q) $(MAKE) $(MAKEMINS) -C $(RBOOT) RBOOT_BIG_FLASH=1 RBOOT_RTC_ENABLED=1 SPI_SIZE=$(RBOOT_SPI_SIZE) SPI_MODE=$(SPI_FLASH_MODE)

$(LDSCRIPT):			$(LDSCRIPT_TEMPLATE)
						$(VECHO) "LINKER SCRIPT $(LD_ADDRESS) $(LD_LENGTH) $@"
						$(Q) sed -e 's/@IROM0_SEG_ADDRESS@/$(LD_ADDRESS)/' -e 's/@IROM_SEG_LENGTH@/$(LD_LENGTH)/' < $< > $@

$(ELF_PLAIN):			$(OBJS) $(LDSCRIPT)
						$(VECHO) "LD PLAIN"
						$(Q) $(CC) -T./$(LDSCRIPT) $(CFLAGS) $(LDFLAGS) $(OBJS) -Wl,--start-group -l$(LIBMAIN_PLAIN) $(SDKLIBS) -lgcc -Wl,--end-group -o $@

$(LIBMAIN_RBB_FILE):	$(LIBMAIN_PLAIN_FILE)
						$(VECHO) "TWEAK LIBMAIN $@"
						$(Q) $(OBJCOPY) -W Cache_Read_Enable_New $< $@

$(ELF_OTA):				$(OBJS) $(OTA_OBJ) $(LIBMAIN_RBB_FILE) $(LDSCRIPT)
						$(VECHO) "LD OTA"
						$(Q) $(CC) -T./$(LDSCRIPT) $(CFLAGS) $(LDFLAGS) $(OBJS) $(OTA_OBJ) -Wl,--start-group -l$(LIBMAIN_RBB) $(SDKLIBS) -lgcc -Wl,--end-group -o $@

$(FIRMWARE_PLAIN_IRAM):	$(ELF_PLAIN) $(ESPTOOL2_BIN)
						$(VECHO) "PLAIN FIRMWARE IRAM $@"
						$(Q) $(ESPTOOL2_BIN) -quiet -bin -$(FLASH_SIZE_KBYTES) -$(SPI_FLASH_MODE) -boot0 $< $@ .text .data .rodata

$(FIRMWARE_PLAIN_IROM):	$(ELF_PLAIN) $(ESPTOOL2_BIN)
						$(VECHO) "PLAIN FIRMWARE IROM $@"
						$(Q) $(ESPTOOL2_BIN) -quiet -lib -$(FLASH_SIZE_KBYTES) -$(SPI_FLASH_MODE) $< $@

$(FIRMWARE_OTA_RBOOT):	$(RBOOT_BIN)
						cp $< $@

$(FIRMWARE_OTA_IMG):	$(ELF_OTA) $(ESPTOOL2_BIN)
						$(VECHO) "RBOOT FIRMWARE $@"
						$(Q) $(ESPTOOL2_BIN) -quiet -bin -$(FLASH_SIZE_KBYTES) -$(SPI_FLASH_MODE) -boot2 $< $@ .text .data .rodata

$(CONFIG_RBOOT_ELF):	$(CONFIG_RBOOT_SRC)

$(CONFIG_DEFAULT_ELF):	always

$(CONFIG_RBOOT_BIN):	$(CONFIG_RBOOT_ELF)
						$(VECHO) "RBOOT CONFIG $@"
						$(Q) $(OBJCOPY) --output-target binary $< $@

$(CONFIG_DEFAULT_BIN):	$(CONFIG_DEFAULT_ELF)
						$(VECHO) "DEFAULT CONFIG $@"
						$(Q) $(OBJCOPY) --output-target binary $< $@

flash:					$(FLASH_TARGET)

flash-plain:			$(FIRMWARE_PLAIN_IRAM) $(FIRMWARE_PLAIN_IROM) free resetserial
						$(VECHO) "FLASH PLAIN"
						$(Q) $(ESPTOOL) write_flash --flash_size $(FLASH_SIZE_ESPTOOL) --flash_mode $(SPI_FLASH_MODE) \
							$(OFFSET_IRAM_PLAIN) $(FIRMWARE_PLAIN_IRAM) \
							$(OFFSET_IROM_PLAIN) $(FIRMWARE_PLAIN_IROM) \
							$(RF_OFFSET_PLAIN) $(RF_FILE) \
							$(SYSTEM_OFFSET_PLAIN) $(SYSTEM_FILE) \
							$(RFCAL_OFFSET_PLAIN) $(RFCAL_FILE) \

flash-ota:				$(FIRMWARE_OTA_RBOOT) $(CONFIG_RBOOT_BIN) $(FIRMWARE_OTA_IMG) free resetserial
						$(VECHO) "FLASH RBOOT"
						$(Q) $(ESPTOOL) write_flash --flash_size $(FLASH_SIZE_ESPTOOL) --flash_mode $(SPI_FLASH_MODE) \
							$(OFFSET_OTA_BOOT) $(FIRMWARE_OTA_RBOOT) \
							$(OFFSET_OTA_RBOOT_CFG) $(CONFIG_RBOOT_BIN) \
							$(OFFSET_OTA_IMG_0) $(FIRMWARE_OTA_IMG) \
							$(RF_OFFSET_OTA) $(RF_FILE) \
							$(SYSTEM_OFFSET_OTA) $(SYSTEM_FILE) \
							$(RFCAL_OFFSET_OTA) $(RFCAL_FILE)

ota-compat:				$(FIRMWARE_OTA_IMG) free otapush espflash
						$(VECHO) "FLASH OTA LEGACY INTERFACE"
						$(Q) otapush -u write $(OTA_HOST) $(FIRMWARE_OTA_IMG)

ota:					$(FIRMWARE_OTA_IMG) free espflash
						$(VECHO) "FLASH OTA"
						espflash -h $(OTA_HOST) -f $(FIRMWARE_OTA_IMG) -W

ota-dummy:				$(FIRMWARE_OTA_IMG) free espflash
						$(VECHO) "FLASH OTA DUMMY"
						$(Q) espflash -h $(OTA_HOST) -f $(FIRMWARE_OTA_IMG) -S

ota-default:			$(RF_FILE) $(SYSTEM_FILE) $(RFCAL_FILE)
						$(VECHO) "FLASH OTA DEFAULTS"
						$(VECHO) "* rf config"
						$(Q)espflash -n -N -h $(OTA_HOST) -f $(RF_FILE) -s $(RF_OFFSET_OTA) -W
						$(VECHO) "* system_config"
						$(Q)espflash -n -N -h $(OTA_HOST) -f $(SYSTEM_FILE) -s $(SYSTEM_OFFSET_OTA) -W
						$(VECHO) "* rf calibiration"
						$(Q)espflash -n -N -h $(OTA_HOST) -f $(RFCAL_FILE) -s $(RFCAL_OFFSET_OTA) -W

ota-rboot-update:		$(FIRMWARE_OTA_RBOOT)
						$(VECHO) "FLASH RBOOT"
						$(Q) espflash -n -N -h $(OTA_HOST) -f $(FIRMWARE_OTA_RBOOT) -s $(OFFSET_OTA_BOOT) -W

backup-config:
						$(VECHO) "BACKUP CONFIG"
						$(Q) $(ESPTOOL) read_flash $(USER_CONFIG_SECTOR)000 0x1000 $(CONFIG_BACKUP_BIN)

restore-config:
						$(VECHO) "RESTORE CONFIG"
						$(Q) $(ESPTOOL) write_flash --flash_size $(FLASH_SIZE_ESPTOOL) --flash_mode $(SPI_FLASH_MODE) \
							$(USER_CONFIG_SECTOR)000 $(CONFIG_BACKUP_BIN)

wipe-config:
						$(VECHO) "WIPE CONFIG"
						dd if=/dev/zero of=wipe-config.bin bs=4096 count=1
						$(Q) $(ESPTOOL) write_flash --flash_size $(FLASH_SIZE_ESPTOOL) --flash_mode $(SPI_FLASH_MODE) \
							$(USER_CONFIG_SECTOR)000 wipe-config.bin
						rm wipe-config.bin

%.o:					%.c
						$(VECHO) "CC $<"
						$(Q) $(CC) $(WARNINGS) $(CFLAGS) $(CINC) -c $< -o $@

%.i:					%.c
						$(VECHO) "CC -E $<"
						$(Q) $(CC) -E $(WARNINGS) $(CFLAGS) $(CINC) -c $< -o $@

%.s:					%.c
						$(VECHO) "CC -S $<"
						$(Q) $(CC) -S $(WARNINGS) $(CFLAGS) $(CINC) -c $< -o $@

%.o:					%.ci
						$(VECHO) "CCI $<"
						$(Q) $(CC) -x c $(WARNINGS) $(CFLAGS) -I$(RBOOT) $(CINC) -c $< -o $@

otapush:				otapush.c
						$(VECHO) "HOST CC $<"
						$(Q) $(HOSTCC) $(HOSTCFLAGS) $(WARNINGS) $< -o $@

espflash:				espflash.cpp
						$(VECHO) "HOST CPP $<"
						$(Q) $(HOSTCPP) $(HOSTCFLAGS) -Wall -Wextra -Werror $< -lpthread -lboost_system -lboost_program_options -lboost_regex -lboost_thread -o $@

resetserial:			resetserial.c
						$(VECHO) "HOST CC $<"
						$(Q) $(HOSTCC) $(HOSTCFLAGS) $(WARNINGS) $< -o $@
