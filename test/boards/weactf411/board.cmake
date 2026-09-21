# -----------------------------------------------------------------------------
# WeAct Studio F411CE -- STM32F411CE, one Cortex-M4F.
#
# Same silicon as the Nucleo above, a different board: its own pinout, its own
# console pins, and a SPI NOR flash the Nucleo does not have. It shares no
# source file with the Nucleo -- only the SoC support, which is why that is a
# devices/ target and not board code.
# -----------------------------------------------------------------------------

set (UOS_BOARD_SRC_DIR "${CMAKE_CURRENT_LIST_DIR}")

set (UOS_BOARD_FLAGS -mcpu=cortex-m4 -mthumb -mfloat-abi=hard -mfpu=fpv4-sp-d16)
set (UOS_BOARD_NCPU  1)

# `spi-flash` and `flatfs` are this board's own: bsp/spi.cpp drives the SPI
# NOR part, and bsp/flatfs.cpp is a flat filesystem over it. Both are board
# code here rather than devices/ drivers, because they were written against
# this board's wiring and have never been compiled for another.
set (UOS_BOARD_CAPS  led uart spi-flash flatfs)

set (UOS_BOARD_LINKER_HW "${UOS_BOARD_SRC_DIR}/linker.ld")

set (UOS_BOARD_LIBS  micro-os-plus::soc-stm32f411xe)

set (UOS_BOARD_DEFINES
     STM32F411xE
     OS_USE_OS_APP_CONFIG_H
     "PORT_GREETING=\"µOS++ WeAct F411CE (STM32F411CE, Cortex-M4F)\"")
