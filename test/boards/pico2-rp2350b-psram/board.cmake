# -----------------------------------------------------------------------------
# WeAct RP2350B -- 2x Cortex-M33, 16 MB flash and 8 MB external PSRAM.
#
# The same silicon and the same board code as pico2, which is why this file
# starts by taking that board whole and then states its four differences. The
# RP2350B is the bigger package of the same chip: more flash, more pins, and
# -- the reason this board is here at all -- a second QSPI chip select wired
# to PSRAM, so a test can put its data somewhere other than the 520 KiB of
# internal SRAM.
#
#   flash   16 MB, so a 16 MB linker script and FLASHSIZE 0x1000000 (openocd.cfg)
#   UART    GPIO12/13, not GPIO0/1
#   PSRAM   window 1 at 0x11000000, CS on GPIO0
#   tests   four of them link their DATA into the PSRAM and take psram.cpp;
#           the rest run exactly as they do on the plain Pico 2
#
# Everything else -- boot.S, the vectors, the clocks, the LED, the SIO
# spinlock kernel lock, the SIO-FIFO IPI, the bootrom core-1 launch -- is the
# pico2 board's, unchanged and not copied.
# -----------------------------------------------------------------------------

# Take the Pico 2's declaration whole. CMAKE_CURRENT_LIST_DIR is per FILE, so
# every path that file sets still resolves into ITS directory, not this one.
include ("${CMAKE_CURRENT_LIST_DIR}/../pico2/board.cmake")

# 16 MB of flash instead of 4.
set (UOS_BOARD_LINKER_HW "${UOS_BOARD_LINKER_16M}")

# The four differences that are compile-time. The list is re-stated rather
# than appended to, because two of its entries replace pico2's defaults.
set (UOS_BOARD_DEFINES
     OS_USE_OS_APP_CONFIG_H
     __ARM_ARCH_7EM__
     PICO2_UART_TX_PIN=12
     PICO2_UART_RX_PIN=13
     PICO2_LED_PIN=25
     "PORT_GREETING=\"µOS++ WeAct RP2350B (16 MB flash + 8 MB PSRAM, 2x Cortex-M33 SMP)\"")
