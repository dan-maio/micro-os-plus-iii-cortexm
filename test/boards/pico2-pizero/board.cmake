# -----------------------------------------------------------------------------
# Pi-Zero RP2350B -- 2x Cortex-M33, 16 MB flash, Pi-Zero form factor.
#
# The third board on this silicon, and like pico2-rp2350b-psram it is the
# pico2 board plus a short list of differences. Two of them are wiring and one
# is not:
#
#   UART    GPIO0/1 (as the Pico 2), but the LED is GPIO5, not GPIO25
#   flash   16 MB, so the 16 MB linker script and FLASHSIZE 0x1000000
#   probe   a different CMSIS-DAP adapter -- 0x0416:0x5951 at 2000 kHz
#
#   ARCH    __ARM_ARCH_8M_MAIN__, where every other board on this silicon
#           forces __ARM_ARCH_7EM__. That is not cosmetic: it selects a
#           DIFFERENT branch of the port headers, and the predecessor's
#           Makefiles for this board did it deliberately -- the M33 IS
#           ARMv8-M Mainline. The two branches have never been compared on
#           the desk, which is worth knowing before reading a result from
#           this board as if it were the same build as pico2's.
#
# It has no PSRAM, so it carries neither the PSRAM tests nor the PSRAM
# linker script.
# -----------------------------------------------------------------------------

include ("${CMAKE_CURRENT_LIST_DIR}/../pico2/board.cmake")

set (UOS_BOARD_LINKER_HW "${UOS_BOARD_LINKER_16M}")

# No emulated image, and this is a board fact, not a missing script: QEMU's
# generic Cortex-M machine has no Cortex-M33, and this board is deliberately
# built ARMv8-M (__ARM_ARCH_8M_MAIN__ below), so the `qemu` variant the pico2
# board offers cannot run here. Clearing the linker removes it, and the port's
# builder emits `hwd` alone.
set (UOS_BOARD_LINKER_QEMU "")

set (UOS_BOARD_DEFINES
     OS_USE_OS_APP_CONFIG_H
     __ARM_ARCH_8M_MAIN__
     PICO2_UART_TX_PIN=0
     PICO2_UART_RX_PIN=1
     PICO2_LED_PIN=5
     "PORT_GREETING=\"µOS++ Pi-Zero RP2350B (16 MB flash, 2x Cortex-M33 SMP)\"")
