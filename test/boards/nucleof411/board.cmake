# -----------------------------------------------------------------------------
# ST Nucleo-F411RE -- STM32F411RE, one Cortex-M4F.
#
# One CPU. That is not a limitation being worked around: it is what makes
# this board the witness that the SMP port contract is optional. OS_NCPU=1
# means the kernel's SMP branches compile out, no kernel patch is applied,
# and the upstream Cortex-M port runs exactly as upstream wrote it.
# -----------------------------------------------------------------------------

set (UOS_BOARD_SRC_DIR "${CMAKE_CURRENT_LIST_DIR}")

set (UOS_BOARD_FLAGS -mcpu=cortex-m4 -mthumb -mfloat-abi=hard -mfpu=fpv4-sp-d16)
set (UOS_BOARD_NCPU  1)

# What THIS PORT CAN DRIVE on this board: the user LED and the ST-Link's
# virtual COM port on USART2. No SD card, no USB device, and one CPU -- so
# no `smp` either.
set (UOS_BOARD_CAPS  led uart)

# Hardware only. There is no UOS_BOARD_LINKER_QEMU: this board is programmed
# over its on-board ST-Link and run on the desk.
set (UOS_BOARD_LINKER_HW "${UOS_BOARD_SRC_DIR}/linker.ld")

set (UOS_BOARD_LIBS  micro-os-plus::soc-stm32f411xe)

set (UOS_BOARD_DEFINES
     STM32F411xE
     OS_USE_OS_APP_CONFIG_H
     "PORT_GREETING=\"µOS++ ST Nucleo-F411RE (STM32F411RE, Cortex-M4F)\"")
