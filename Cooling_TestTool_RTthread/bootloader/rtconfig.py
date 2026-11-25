import os

ARCH = 'arm'
CPU = 'cortex-m4'
CROSS_TOOL = 'gcc'
MCU = 'STM32F302xC'
BSP_LIBRARY_TYPE = None

if os.getenv('RTT_EXEC_PATH'):
    EXEC_PATH = os.getenv('RTT_EXEC_PATH')
else:
    EXEC_PATH = r'C:\Users\XXYYZZ'

PLATFORM = 'gcc'
PREFIX = 'arm-none-eabi-'
CC = PREFIX + 'gcc'
AS = PREFIX + 'gcc'
AR = PREFIX + 'ar'
CXX = PREFIX + 'g++'
LINK = PREFIX + 'gcc'
TARGET_EXT = 'elf'
SIZE = PREFIX + 'size'
OBJCPY = PREFIX + 'objcopy'

DEVICE = ' -mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard -ffunction-sections -fdata-sections'
CFLAGS = DEVICE + ' -Wall -Os -gdwarf-2 -g -Dgcc -D__RT_KERNEL_SOURCE__ -DCOOLING_BOOTLOADER_BUILD'
CXXFLAGS = CFLAGS
AFLAGS = ' -c' + DEVICE + ' -x assembler-with-cpp -Wa,-mimplicit-it=thumb -gdwarf-2 -DCOOLING_BOOTLOADER_BUILD'
LFLAGS = DEVICE + ' -Wl,--gc-sections,-Map=cooling_bootloader.map,-cref,-u,Reset_Handler -T linker_bootloader.lds'
CPATH = ''
LPATH = ''
POST_ACTION = OBJCPY + ' -O binary --gap-fill 0xFF $TARGET cooling_bootloader.bin\n' + SIZE + ' $TARGET\n'
