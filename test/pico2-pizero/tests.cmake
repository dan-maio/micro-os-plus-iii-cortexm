# -----------------------------------------------------------------------------
# What this board's tests need.
#
# The same composition as the Pico 2's, because these are the same eight tests
# on the same silicon -- the board differs in its pins, its flash size and its
# probe, none of which is a source file. Transcribed from the predecessor's
# pizero_rp2350B Makefiles:
#
#   every test    the board's uart.cpp and its RTOS glue
#   smp-test5     the glue only -- it defines its own uart:: in main.cpp
#   the SMP tests multicore.cpp, the bootrom core-1 launch
#   smp-test1     no multicore.cpp: it is the single-core RTOS bring-up
#   sc-test-ko    no multicore.cpp: the single-core kernel-object test
# -----------------------------------------------------------------------------

set (_single_core smp-test1 sc-test-ko)

# The two applications that compile NO KERNEL. They are the bare-metal probes
# that run before a scheduler exists: smp-test0 starts the second core itself
# and checks the two can talk, exc-test asks whether exception ENTRY works at
# all. Their Makefiles list no kernel source and no port core -- nine files
# and seven files respectively -- so they link the port's bare target.
set (BOARD_TEST_NO_KERNEL smp-test0 exc-test psram-exec)

# The two USB gadget tests, carried from the pico2 board. The same TinyUSB
# stack and class drivers (test/boards/pico2/usb/), each with its own
# tusb_config.h -- which is why their include directory goes first. The
# board's differences need no source change: the LED is PICO2_LED_PIN (GPIO5),
# the UART GPIO0/1, and the 16 MB linker script is the board's.
set (_usb_cdc_tests smp-test-usb-cdc-acm)
set (_usb_hid_tests smp-test-usb-hid)

function (board_test_includes _app _out)
  if ("${_app}" IN_LIST _usb_cdc_tests)
    set (${_out} "${UOS_BOARD_USB_CDC_INCLUDE}" ${UOS_BOARD_USB_INCLUDE} PARENT_SCOPE)
  elseif ("${_app}" IN_LIST _usb_hid_tests)
    set (${_out} "${UOS_BOARD_USB_HID_INCLUDE}" ${UOS_BOARD_USB_INCLUDE} PARENT_SCOPE)
  else ()
    set (${_out} "" PARENT_SCOPE)
  endif ()
endfunction ()

function (board_test_ncpu _app _out)
  if ("${_app}" IN_LIST _single_core)
    set (${_out} 1 PARENT_SCOPE)
  else ()
    set (${_out} "${UOS_BOARD_NCPU}" PARENT_SCOPE)
  endif ()
endfunction ()

function (board_test_sources _app _out)

  # The kernel-less pair take the board's start-up and nothing that needs a
  # scheduler: no rtos-glue.cpp, no trace, no syscalls. smp-test0 drives both
  # cores itself, so it does take multicore.cpp; exc-test does not.
  if ("${_app}" IN_LIST BOARD_TEST_NO_KERNEL)
    set (_srcs "${UOS_BOARD_UART}")
    if (_app STREQUAL "smp-test0")
      list (APPEND _srcs "${UOS_BOARD_MULTICORE}")
    endif ()
    if (_app STREQUAL "psram-exec")
      # The QSPI PSRAM bring-up runs from SRAM (see glue/psram.cpp). The board
      # has no multicore.cpp here: the supervisor is single-core.
      list (APPEND _srcs "${UOS_BOARD_PSRAM}")
    endif ()
    set (${_out} "${_srcs}" PARENT_SCOPE)
    return ()
  endif ()
  if (_app STREQUAL "smp-test5")
    set (_srcs "${UOS_BOARD_RTOS_GLUE}")
  else ()
    set (_srcs "${UOS_BOARD_UART}" "${UOS_BOARD_RTOS_GLUE}")
  endif ()
  if (NOT "${_app}" IN_LIST _single_core)
    list (APPEND _srcs "${UOS_BOARD_MULTICORE}")
  endif ()
  if (_app STREQUAL "psram-mat-test-250")
    # The matrix solver keeps the kernel in SRAM but its big arrays in PSRAM;
    # the QSPI bring-up routine comes from the PSRAM glue.
    list (APPEND _srcs "${UOS_BOARD_PSRAM}")
  endif ()
  # The USB pair: TinyUSB, the core and one class driver. Unlike the pico2's
  # CDC test, no ws2812.cpp -- this board has no RGB LED; its CDC test blinks
  # the plain LED.
  if ("${_app}" IN_LIST _usb_cdc_tests)
    list (APPEND _srcs ${UOS_BOARD_USB_CORE_SOURCES} ${UOS_BOARD_USB_CDC_SOURCES})
  elseif ("${_app}" IN_LIST _usb_hid_tests)
    list (APPEND _srcs ${UOS_BOARD_USB_CORE_SOURCES} ${UOS_BOARD_USB_HID_SOURCES})
  endif ()
  set (${_out} "${_srcs}" PARENT_SCOPE)
endfunction ()

function (board_test_defines _app _out)
  # The PSRAM dual-app supervisor runs the whole image from RAM (the board's
  # flash XIP is marginal for the dense PSRAM fetches), brings up the QSPI PSRAM
  # on CS GPIO47, and needs no kernel.
  if (_app STREQUAL "psram-exec")
    set (${_out} PICO2_RAM_EXEC=1 PICO2_PSRAM_CS_PIN=47 PARENT_SCOPE)
    return ()
  endif ()
  # The SRAM-boot PSRAM solver: the kernel stays in SRAM, only the big matrix
  # arrays live in PSRAM. boot.S copies (but does not call) the QMI routine
  # (PICO2_PSRAM_COPY); the app brings PSRAM up itself, at 250 MHz.
  if (_app STREQUAL "psram-mat-test-250")
    set (${_out}
         PICO2_PSRAM_COPY=1 PICO2_PSRAM_CS_PIN=47 PICO2_SYS_CLK_KHZ=250000
         PARENT_SCOPE)
    return ()
  endif ()
  # The predecessor's smp-test1 Makefile was the only one here to pin the
  # system clock: the bring-up test runs the part at 150 MHz.
  if (_app STREQUAL "smp-test1")
    set (${_out} PICO2_SYS_CLK_KHZ=150000 PARENT_SCOPE)
  else ()
    set (${_out} "" PARENT_SCOPE)
  endif ()
endfunction ()

function (board_test_linker _app _out)
  # psram-exec carries its own RAM-exec linker: a flash stub, the SRAM
  # supervisor, and the two PSRAM app images at 0x11000000 / 0x11400000.
  if (_app STREQUAL "psram-exec")
    set (${_out}
         "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/psram-exec/psram-exec.ld"
         PARENT_SCOPE)
  elseif (_app STREQUAL "psram-mat-test-250")
    set (${_out}
         "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/psram-mat-test-250/psram-sram-boot.ld"
         PARENT_SCOPE)
  else ()
    set (${_out} "" PARENT_SCOPE)
  endif ()
endfunction ()
