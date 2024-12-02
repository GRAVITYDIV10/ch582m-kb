SRCS += \
app/main.c \

SRCS += \
ble/ble.c \
ble/ble_sysinfo_svc.c \
ble/ble_console_svc.c \

SRCS += \
lib/fifo8.c \

SRCS += \
forth/stepforth.c \

INCS += \
-I app/ \
-I lib/ \
-I ble/ \
-I forth/ \

CH58X_SDK ?= ./EVT/EXAM
TOOLCHAIN ?= ./MRS_Toolchain_Linux_x64_V1.92/RISC-V_Embedded_GCC12/bin/

CROSS_COMPILE ?= $(TOOLCHAIN)/riscv-none-elf-
CC = $(CROSS_COMPILE)gcc
LD = $(CROSS_COMPILE)ld
OD = $(CROSS_COMPILE)objdump
OC = $(CROSS_COMPILE)objcopy
SZ = $(CROSS_COMPILE)size
NM = $(CROSS_COMPILE)nm
GDB = $(CROSS_COMPILE)gdb

SRCS += \
$(CH58X_SDK)/SRC/Startup/startup_CH583.S \
#$(CH58X_SDK)/FreeRTOS/FreeRTOS/portable/GCC/RISC-V/portASM.S \
#$(CH58X_SDK)/FreeRTOS/Startup/startup_CH583.S \

SRCS += \
$(CH58X_SDK)/SRC/StdPeriphDriver/CH58x_sys.c \
$(CH58X_SDK)/SRC/StdPeriphDriver/CH58x_uart0.c \
$(CH58X_SDK)/SRC/StdPeriphDriver/CH58x_pwm.c \
$(CH58X_SDK)/SRC/StdPeriphDriver/CH58x_timer0.c \
$(CH58X_SDK)/SRC/StdPeriphDriver/CH58x_timer1.c \
$(CH58X_SDK)/SRC/StdPeriphDriver/CH58x_pwr.c \
$(CH58X_SDK)/SRC/StdPeriphDriver/CH58x_spi0.c \
$(CH58X_SDK)/SRC/StdPeriphDriver/CH58x_clk.c \
$(CH58X_SDK)/SRC/StdPeriphDriver/CH58x_i2c.c \
$(CH58X_SDK)/SRC/StdPeriphDriver/CH58x_uart3.c \
$(CH58X_SDK)/SRC/StdPeriphDriver/CH58x_spi1.c \
$(CH58X_SDK)/SRC/StdPeriphDriver/CH58x_adc.c \
$(CH58X_SDK)/SRC/StdPeriphDriver/CH58x_gpio.c \
$(CH58X_SDK)/SRC/StdPeriphDriver/CH58x_timer3.c \
$(CH58X_SDK)/SRC/StdPeriphDriver/CH58x_uart1.c \
$(CH58X_SDK)/SRC/StdPeriphDriver/CH58x_timer2.c \
$(CH58X_SDK)/SRC/StdPeriphDriver/CH58x_flash.c \
$(CH58X_SDK)/SRC/StdPeriphDriver/CH58x_uart2.c \
$(CH58X_SDK)/SRC/RVMSIS/core_riscv.c \
$(CH58X_SDK)/BLE/HAL/KEY.c \
$(CH58X_SDK)/BLE/HAL/LED.c \
$(CH58X_SDK)/BLE/HAL/MCU.c \
$(CH58X_SDK)/BLE/HAL/RTC.c \
$(CH58X_SDK)/BLE/HAL/SLEEP.c \

SRCS += \
#$(CH58X_SDK)/SRC/StdPeriphDriver/CH58x_usbhostClass.c \
#$(CH58X_SDK)/SRC/StdPeriphDriver/CH58x_usb2hostBase.c \
#$(CH58X_SDK)/SRC/StdPeriphDriver/CH58x_usbdev.c \
#$(CH58X_SDK)/SRC/StdPeriphDriver/CH58x_usb2dev.c \
#$(CH58X_SDK)/SRC/StdPeriphDriver/CH58x_usbhostBase.c \
#$(CH58X_SDK)/SRC/StdPeriphDriver/CH58x_usb2hostClass.c \

INCS += \
-I $(CH58X_SDK)/SRC/RVMSIS \
-I $(CH58X_SDK)/SRC/StdPeriphDriver/inc \
-I $(CH58X_SDK)/BLE/HAL/include \
-I $(CH58X_SDK)/BLE/LIB \
-I $(CH58X_SDK)/BLE/MESH/MESH_LIB \

LIBS += \
-L $(CH58X_SDK)/SRC/StdPeriphDriver -lISP583 \
-L $(CH58X_SDK)/BLE/LIB  -l:LIBCH58xBLE.a \
-L $(CH58X_SDK)/BLE/MESH/MESH_LIB -l:LIBCH58xMESHROM.a -l:LIBMESH.a \
-lprintf -lprintfloat

LINK_SCRIPT ?= $(CH58X_SDK)/SRC/Ld/Link.ld

CFLAGS += \
-Wall -mno-save-restore \
--specs=nano.specs --specs=nosys.specs \
-msmall-data-limit=8 -fmessage-length=0 \
-fsigned-char -Wno-unused-parameter \
-march=rv32imac_zicsr -mabi=ilp32 -Os -ggdb \
-fdata-sections -ffunction-sections -Wl,--gc-sections \
-nostartfiles \
-Wl,-Map fw.map \
-Wl,--print-memory-usage \
-T $(LINK_SCRIPT)

# DEBUG:
# 0: UART0
# 1: UART1
# 2: UART2
CFLAGS += \
	-DDEBUG=1 \

reflash: clean all flash info

all: clean bin dis

info:
	$(SZ) fw.elf

elf:
	$(CC) $(CFLAGS) $(INCS) $(SRCS) $(LIBS) -o fw.elf

bin: elf
	$(OC) -O binary fw.elf fw.bin

dis: elf
	$(OD) -S -d fw.elf > fw.dis

clean:
	rm -fv fw.bin fw.elf fw.dis fw.map

patch:
	sed -i -e 's/void FLASH_ROM_READ(UINT32 StartAddr, PVOID Buffer, UINT32 len);//g' \
		$(CH58X_SDK)/SRC/StdPeriphDriver/inc/CH58x_flash.h


OPENOCD ?= ./MRS_Toolchain_Linux_x64_V1.92/OpenOCD/bin/openocd -f MRS_Toolchain_Linux_x64_V1.92/OpenOCD/bin/wch-riscv.cfg

ocd-flash: erase bin
	$(OPENOCD) -c init -c halt -c 'program fw.elf' -c exit
	$(OPENOCD) -c init -c halt -c 'wlink_reset_resume' -c exit

ocd-verify:
	$(OPENOCD) -c init -c halt -c 'verify_image fw.elf' -c exit

ocd-erase:
	$(OPENOCD) -c init -c halt -c 'flash erase_sector wch_riscv 0 last' -c exit

ocd-reset:
	$(OPENOCD) -c init -c halt -c 'wlink_reset_resume' -c exit

ocd-dbgserver:
	$(OPENOCD)

WCHISP ?= wchisp

dbgen:
	$(WCHISP) config enable-debug

WLINK ?= wlink --chip CH582 --speed high

erase:
	$(WLINK) erase

rstpwr:
	$(WLINK) set-power disable3v3
	sleep 1
	$(WLINK) set-power enable3v3

flash: bin
	$(WLINK) flash fw.bin

reset:
	$(WLINK) reset

dbgclient: elf
	$(GDB) fw.elf --init-eval-command="target remote localhost:3333"

dbgcon:
	telnet 127.0.0.1 4444

WCHISP ?= ./wchisp-linux-x64/wchisp

usbflash: bin
	$(WCHISP) flash fw.bin
