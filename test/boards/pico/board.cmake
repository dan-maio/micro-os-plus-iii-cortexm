# -----------------------------------------------------------------------------
# Raspberry Pi Pico -- RP2040, 2x Cortex-M0+.
#
# The Pico 2's sibling, one generation earlier, and built the same way: no
# Pico SDK, the board's own boot, vectors, clocks and UART, and the RP2040
# flavour of the port core. The RP2040 port the JTAGprobe_smp probe firmware
# runs on (micro-os-plus-iii-rp2040, on top of the Pico SDK) is where the
# ARMv6-M parts come from; here they sit on bare registers.
#
# WHAT MAKES IT SMP is the same three pieces of silicon as on the RP2350,
# at the RP2040's addresses and IRQs:
#
#   kernel lock   SIO hardware spinlock 0. The M0+ has no LDREX/STREX at all.
#   IPI           the SIO inter-core FIFO; each core has its own IRQ for it,
#                 SIO_IRQ_PROC0 (15) and SIO_IRQ_PROC1 (16).
#   CPU index     SIO_CPUID.
#
# Core 1 is launched through the bootrom FIFO handshake, as on the RP2350.
#
# What the RP2040 does NOT have, and what that changes:
#
#   no FPU        float is soft-float; fp-switch still checks that each
#                 thread's values survive switches, they just live in core
#                 registers.
#   no BASEPRI    critical sections are PRIMASK (DI/EI).
#   264 KB SRAM   half the RP2350's: the heap is what .data/.bss leave.
#   boot2         the bootrom runs a 256-byte flash second stage before the
#                 image (src/boot2.S); the vector table follows it.
#
# The probe is the JTAGprobe (CMSIS-DAP v2), over SWD -- the RP2040 has no
# JTAG -- at 15 MHz by default (openocd.cfg, hw.sh).
# -----------------------------------------------------------------------------

set (UOS_BOARD_SRC_DIR "${CMAKE_CURRENT_LIST_DIR}")

set (UOS_BOARD_FLAGS -mcpu=cortex-m0plus -mthumb -mfloat-abi=soft)
set (UOS_BOARD_NCPU  2)

set (UOS_BOARD_CAPS  smp led uart)

# The RP2040 flavour of the port core: the RP2350 one with the ARMv6-M
# context switch and the RP2040's per-core FIFO IRQs. include-rp2040/ holds
# the headers that differ (os-c-decls.h, os-inlines.h); os-decls.h comes from
# include/, as for the Pico 2.
set (UOS_BOARD_PORT_CORE    "src/rtos/os-core-rp2040.cpp")
set (UOS_BOARD_PORT_INCLUDE "include-rp2040;include")

# The emulated image links against QEMU's generic Cortex-M machine
# (mps2-an385, as the harness's qemu-cortex-m0 platform), whose memory map
# this script describes. QEMU has no RP2040: the SIO block, the USB and the
# second core are not modelled, so those tests are HWD_ONLY in tests.cmake.
set (UOS_BOARD_LINKER_QEMU "${UOS_BOARD_SRC_DIR}/linker-qemu.ld")

set (UOS_BOARD_LINKER_HW "${UOS_BOARD_SRC_DIR}/linker.ld")

# The board's own boot.S and vectors, and its UART trace, as on the Pico 2.
set (UOS_BOARD_LIBS  micro-os-plus::soc-rp2040)

set (UOS_BOARD_DEFINES
     OS_USE_OS_APP_CONFIG_H
     "PORT_GREETING=\"µOS++ Raspberry Pi Pico (RP2040, 2x Cortex-M0+ SMP)\"")

# src/ is compiled into every application; glue/ is composed per test by
# test/pico/tests.cmake, exactly as the Pico 2's (see its board.cmake).
set (UOS_BOARD_UART      "${CMAKE_CURRENT_LIST_DIR}/glue/uart.cpp")
set (UOS_BOARD_MULTICORE "${CMAKE_CURRENT_LIST_DIR}/glue/multicore.cpp")
set (UOS_BOARD_RTOS_GLUE
     "${CMAKE_CURRENT_LIST_DIR}/glue/rtos-glue.cpp"
     "${CMAKE_CURRENT_LIST_DIR}/glue/trace-uart.cpp"
     # One copy for the whole port; see test/boards/shared/syscalls.c.
     "${CMAKE_CURRENT_LIST_DIR}/../shared/syscalls.c")

# -----------------------------------------------------------------------------
# The USB device stack, for the two USB gadget tests.
#
# The Pico 2's vendored TinyUSB is used as it is: its device driver is
# TinyUSB's portable/raspberrypi/rp2040, written for this very controller (the
# RP2350 kept it), and its per-class files (usb/cdc, usb/hid) hold nothing
# RP2350-specific. Only the pico-shim files that carry an address, a reset bit
# or an IRQ number are the RP2040's own, in usb/pico-shim/ here; that
# directory comes FIRST, so it shadows the Pico 2's and the rest of the shim
# (the SDK's USB register headers) is the Pico 2's.
set (_pico2_usb "${CMAKE_CURRENT_LIST_DIR}/../pico2/usb")
set (UOS_BOARD_USB_INCLUDE
     "${CMAKE_CURRENT_LIST_DIR}/usb/pico-shim"
     "${_pico2_usb}/pico-shim"
     "${_pico2_usb}/tinyusb")
set (UOS_BOARD_USB_CORE_SOURCES
     "${_pico2_usb}/tinyusb/tusb.c"
     "${_pico2_usb}/tinyusb/common/tusb_fifo.c"
     "${_pico2_usb}/tinyusb/device/usbd.c"
     "${_pico2_usb}/tinyusb/portable/raspberrypi/rp2040/dcd_rp2040.c"
     "${_pico2_usb}/tinyusb/portable/raspberrypi/rp2040/rp2040_usb.c"
     "${CMAKE_CURRENT_LIST_DIR}/usb/pico-shim/pico-shim.c")

set (UOS_BOARD_USB_CDC_INCLUDE "${_pico2_usb}/cdc")
set (UOS_BOARD_USB_CDC_SOURCES
     "${_pico2_usb}/tinyusb/class/cdc/cdc_device.c"
     "${_pico2_usb}/cdc/usb_descriptors.c"
     "${_pico2_usb}/cdc/usb_cdc.c")

set (UOS_BOARD_USB_HID_INCLUDE "${_pico2_usb}/hid")
set (UOS_BOARD_USB_HID_SOURCES
     "${_pico2_usb}/tinyusb/class/hid/hid_device.c"
     "${_pico2_usb}/hid/usb_descriptors.c"
     "${_pico2_usb}/hid/usb_hid.c")
