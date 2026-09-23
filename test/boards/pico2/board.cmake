# -----------------------------------------------------------------------------
# Raspberry Pi Pico 2 -- RP2350, 2x Cortex-M33.
#
# The SMP board of this port, and the reason the port exists in this shape.
# Two identical, cache-coherent Cortex-M33 in one coherency domain: unlike the
# RK3506's Cortex-M0 companion, these two ARE peers, so the scheduler places
# threads on both and either can take an IPI from the other.
#
# WHAT MAKES IT SMP is three pieces of RP2350 silicon, none of them portable
# Cortex-M and all of them therefore here rather than in the port:
#
#   kernel lock   SIO hardware spinlock 0. Not LDREX/STREX: the RP2350 has no
#                 global exclusive monitor, so the architectural route does
#                 not work across the two cores.
#   IPI           the SIO inter-core FIFO, delivered on SIO_IRQ_FIFO
#                 (external IRQ 25); the handler drains the FIFO and pends
#                 its own core's PendSV.
#   CPU index     SIO_CPUID.
#
# Core 1 is launched through the bootrom FIFO handshake with its own MSP; the
# scheduler's per-core SysTick means only core 0 advances the RTOS clock,
# while core 1's SysTick only reschedules core 1.
# -----------------------------------------------------------------------------

set (UOS_BOARD_SRC_DIR "${CMAKE_CURRENT_LIST_DIR}")

set (UOS_BOARD_FLAGS -mcpu=cortex-m33 -mthumb -mfloat-abi=hard -mfpu=fpv5-sp-d16)
set (UOS_BOARD_NCPU  2)

set (UOS_BOARD_CAPS  smp led uart)

# THE PORT CORE THIS BOARD WAS PROVEN WITH. Upstream's Cortex-M core is
# single-core and has no SMP branch at all, so this board names the RP2350
# flavour -- the same file plus an OS_USE_SMP_SCHEDULER branch. Its port
# headers come with it: they carry the SIO spinlock and SIO_CPUID inlines.
#
# See the port's CMakeLists.txt for why the two cores are not merged yet.
set (UOS_BOARD_PORT_CORE    "src/rtos/os-core-rp2350.cpp")
set (UOS_BOARD_PORT_INCLUDE "include-rp2350")

# Hardware only. QEMU has no RP2350 machine, and the parts that would matter
# -- the SIO block the whole SMP design rests on -- are exactly what a
# generic Cortex-M33 machine does not have.
set (UOS_BOARD_LINKER_HW "${UOS_BOARD_SRC_DIR}/linker.ld")

# No micro-os-plus::iii-startup: this board has its own boot.S and its own
# interrupt-vectors.S. No ::iii-trace-semihosting either -- it traces over
# the UART, through the board's own trace-uart.cpp, which is why the
# predecessor's Makefiles listed that file in place of the kernel's backend.
set (UOS_BOARD_LIBS  micro-os-plus::soc-rp2350)

# __ARM_ARCH_7EM__ is forced, as the predecessor's Makefiles forced it. The
# M33 is ARMv8-M, but the port headers' v7E-M path is the one this board was
# brought up and proven on; letting the compiler's own __ARM_ARCH_8M_MAIN__
# select a different branch would change the scheduler under a working board.
# OS_USE_SMP_SCHEDULER is NOT here. It is derived per application from NCPU by
# uos_add_app(), because on this board it is not a board-wide fact: smp-test1
# (the single-core RTOS bring-up) and sc-test-ko (the single-core kernel-object
# test) define it in none of the predecessor's Makefiles. test/<board>/
# tests.cmake names them through board_test_ncpu().
set (UOS_BOARD_DEFINES
     OS_USE_OS_APP_CONFIG_H
     __ARM_ARCH_7EM__
     "PORT_GREETING=\"µOS++ Raspberry Pi Pico 2 (RP2350, 2x Cortex-M33 SMP)\"")

# -----------------------------------------------------------------------------
# What src/ holds, and what glue/ holds, is not a tidy-up: it is the split the
# predecessor's per-test Makefiles already made, read off all seven of them.
#
#   src/    boot.S, interrupt-vectors.S, init-fini-stubs.c, clocks.cpp,
#           led.cpp -- compiled for every application, because every Makefile
#           listed them.
#
#   glue/   uart.cpp, rtos-glue.cpp, trace-uart.cpp, syscalls.c,
#           multicore.cpp, psram.cpp, adc.cpp -- NOT compiled for every
#           application, because the Makefiles disagree, one test at a time:
#             smp-test5     defines its own uart:: in main.cpp
#             smp-test1     single-core: no multicore.cpp
#             sc-test-ko    single-core: no multicore.cpp
#             the PSRAM set  adds psram.cpp
#             the nested-clock set  adds adc.cpp
#             smp-test0     has no RTOS at all, so no glue at all
#           Linking the board's copy as well is a duplicate definition, which
#           is exactly what a glob over src/ produced before this split.
#
# Each board's test/<board>/tests.cmake composes them per test.
set (UOS_BOARD_UART      "${CMAKE_CURRENT_LIST_DIR}/glue/uart.cpp")
set (UOS_BOARD_MULTICORE "${CMAKE_CURRENT_LIST_DIR}/glue/multicore.cpp")
set (UOS_BOARD_PSRAM     "${CMAKE_CURRENT_LIST_DIR}/glue/psram.cpp")
set (UOS_BOARD_ADC       "${CMAKE_CURRENT_LIST_DIR}/glue/adc.cpp")
set (UOS_BOARD_RTOS_GLUE
     "${CMAKE_CURRENT_LIST_DIR}/glue/rtos-glue.cpp"
     "${CMAKE_CURRENT_LIST_DIR}/glue/trace-uart.cpp"
     # One copy for the whole port; see test/boards/shared/syscalls.c.
     "${CMAKE_CURRENT_LIST_DIR}/../shared/syscalls.c")

# The other two memory maps this silicon is built against. They belong to the
# 16 MB RP2350B boards rather than to this 4 MB one, and they live here
# because the board CODE lives here: pico2-rp2350b-psram/ and pico2-pizero/
# are board.cmake, a probe config and a test directory, nothing more.
set (UOS_BOARD_LINKER_16M   "${CMAKE_CURRENT_LIST_DIR}/pico2-rp2350b.ld")
set (UOS_BOARD_LINKER_PSRAM "${CMAKE_CURRENT_LIST_DIR}/pico2-rp2350b-psram.ld")

# -----------------------------------------------------------------------------
# The USB device stack, for the two tests that are a USB gadget.
#
# TinyUSB, vendored by the predecessor. It carried TWO copies of it, one under
# src/usb-cdc/ and one under src/usb-hid/, and they are byte-identical apart
# from the one class driver each keeps (class/cdc against class/hid). Here it
# is ONE tree carrying both classes, and what genuinely differs per test --
# tusb_config.h, usb_descriptors.c and the class glue -- stays split, in
# usb/cdc/ and usb/hid/.
#
# tusb_config.h is why the per-test include directory has to come FIRST:
# tinyusb includes it by plain name, and each test configures a different
# device class.
set (UOS_BOARD_USB_INCLUDE
     "${CMAKE_CURRENT_LIST_DIR}/usb/tinyusb"
     "${CMAKE_CURRENT_LIST_DIR}/usb/pico-shim")
set (UOS_BOARD_USB_CORE_SOURCES
     "${CMAKE_CURRENT_LIST_DIR}/usb/tinyusb/tusb.c"
     "${CMAKE_CURRENT_LIST_DIR}/usb/tinyusb/common/tusb_fifo.c"
     "${CMAKE_CURRENT_LIST_DIR}/usb/tinyusb/device/usbd.c"
     "${CMAKE_CURRENT_LIST_DIR}/usb/tinyusb/portable/raspberrypi/rp2040/dcd_rp2040.c"
     "${CMAKE_CURRENT_LIST_DIR}/usb/tinyusb/portable/raspberrypi/rp2040/rp2040_usb.c"
     "${CMAKE_CURRENT_LIST_DIR}/usb/pico-shim/pico-shim.c")

set (UOS_BOARD_USB_CDC_INCLUDE "${CMAKE_CURRENT_LIST_DIR}/usb/cdc")
set (UOS_BOARD_USB_CDC_SOURCES
     "${CMAKE_CURRENT_LIST_DIR}/usb/tinyusb/class/cdc/cdc_device.c"
     "${CMAKE_CURRENT_LIST_DIR}/usb/cdc/usb_descriptors.c"
     "${CMAKE_CURRENT_LIST_DIR}/usb/cdc/usb_cdc.c")

set (UOS_BOARD_USB_HID_INCLUDE "${CMAKE_CURRENT_LIST_DIR}/usb/hid")
set (UOS_BOARD_USB_HID_SOURCES
     "${CMAKE_CURRENT_LIST_DIR}/usb/tinyusb/class/hid/hid_device.c"
     "${CMAKE_CURRENT_LIST_DIR}/usb/hid/usb_descriptors.c"
     "${CMAKE_CURRENT_LIST_DIR}/usb/hid/usb_hid.c")

# The WS2812 RGB LED driver. One test uses it, so it is glue, not src/.
set (UOS_BOARD_WS2812 "${CMAKE_CURRENT_LIST_DIR}/glue/ws2812.cpp")
