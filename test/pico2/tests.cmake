# -----------------------------------------------------------------------------
# What this board's tests need.
#
# The board carries source sets that are NOT compiled into every application --
# see test/boards/pico2/board.cmake for why. This file says which test takes
# which, and it is a transcription of the predecessor's Makefiles, not a new
# arrangement:
#
#   every test    the board's uart.cpp and its RTOS glue
#   smp-test5     the glue only -- it defines its own uart:: in main.cpp
#   the SMP tests multicore.cpp, the bootrom core-1 launch
#   smp-test1     no multicore.cpp: it is the single-core RTOS bring-up
#   sc-test-ko    no multicore.cpp: the single-core kernel-object test
#   the USB pair  TinyUSB -- the core, one class driver, and their own
#                 tusb_config.h, which is why their include directory goes
#                 first
#   the CDC test  ws2812.cpp as well: its console is the USB, so it reports
#                 with the RGB LED
#
# smp-test1 and sc-test-ko are also the two tests that run at OS_NCPU=1 on a
# two-core board. That is not a simplification: sc-test-ko is the only test on
# this silicon that exercises the kernel's non-SMP branch, which is what makes
# it worth keeping beside its SMP twin.
# -----------------------------------------------------------------------------

set (_single_core smp-test1 sc-test-ko)

# The two applications that compile NO KERNEL. They are the bare-metal probes
# that run before a scheduler exists: smp-test0 starts the second core itself
# and checks the two can talk, exc-test asks whether exception ENTRY works at
# all. Their Makefiles list no kernel source and no port core -- nine files
# and seven files respectively -- so they link the port's bare target.
set (BOARD_TEST_NO_KERNEL smp-test0 exc-test)

# Tests that get NO `qemu` image. The USB pair needs the RP2350's USB
# peripheral, the multicore ones need the SIO block the SMP design rests on,
# and QEMU's generic Cortex-M33 machine models neither. What is left --
# exc-test, smp-test1 and sc-test-ko, the single-core bring-up and the
# kernel-object test -- builds and runs emulated.
set (BOARD_TEST_HWD_ONLY
     exc-test
     smp-mat-test
     smp-test0
     smp-test2
     smp-test3
     smp-test4
     smp-test5
     smp-test-ko
     smp-test-usb-cdc-acm
     smp-test-usb-hid)

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
    set (${_out} "${_srcs}" PARENT_SCOPE)
    return ()
  endif ()
  # smp-test5 brings its own uart::; every other test takes the board's.
  if (_app STREQUAL "smp-test5")
    set (_srcs "${UOS_BOARD_RTOS_GLUE}")
  else ()
    set (_srcs "${UOS_BOARD_UART}" "${UOS_BOARD_RTOS_GLUE}")
  endif ()

  if (NOT "${_app}" IN_LIST _single_core)
    list (APPEND _srcs "${UOS_BOARD_MULTICORE}")
  endif ()

  if ("${_app}" IN_LIST _usb_cdc_tests)
    list (APPEND _srcs ${UOS_BOARD_USB_CORE_SOURCES} ${UOS_BOARD_USB_CDC_SOURCES}
                       "${UOS_BOARD_WS2812}")
  elseif ("${_app}" IN_LIST _usb_hid_tests)
    list (APPEND _srcs ${UOS_BOARD_USB_CORE_SOURCES} ${UOS_BOARD_USB_HID_SOURCES})
  endif ()

  set (${_out} "${_srcs}" PARENT_SCOPE)
endfunction ()

# The two USB gadget tests. Both link the 4 MB script, which is why they are on
# THIS board: the predecessor built them against ld/pico2-rp2350.ld, not
# against either 16 MB map.
#
# The CDC one it called smp-test-mini-a-usb-cdc-acm, after the RP2350B-MINI-A
# it was brought up on -- the WS2812 defines below are that board's wiring
# (an RGB LED on GPIO20, where a plain Pico 2 has a single LED on GPIO25).
# Nothing else about the test is board-specific, so it is carried under the
# name of what it tests. Its `_agy` sibling is not carried: main.cpp and
# Makefile are byte-identical to this one.
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

function (board_test_defines _app _out)
  if ("${_app}" IN_LIST _usb_cdc_tests)
    # The MINI-A's RGB LED, exactly as that Makefile declared it.
    set (${_out} PICO2_HAS_WS2812 PICO2_WS2812_PIN=20 WS2812_CLK_KHZ=150000
         PARENT_SCOPE)
  else ()
    set (${_out} "" PARENT_SCOPE)
  endif ()
endfunction ()

# The `qemu` image does not link the board at all. The RP2350's boot.S, its
# interrupt vectors and its UART are what run on real silicon; on QEMU's
# generic Cortex-M33 machine the image is the kernel plus a generic device
# (micro-os-plus::device, whose vectors-cortexm.c owns the vector table) plus
# the board's semihosting console glue under qemu/. The test's own sources are
# unchanged.
function (board_test_qemu_libs _app _out)
  set (
    ${_out}
    micro-os-plus::cortexm-qemu
    micro-os-plus::iii-startup
    micro-os-plus::iii-trace-semihosting
    micro-os-plus::iii-semihosting
    micro-os-plus::iii-newlib-reent
    micro-os-plus::iii-posix-io
    micro-os-plus::device
    PARENT_SCOPE
  )
endfunction ()

function (board_test_qemu_sources _app _out)
  set (${_out} "${UOS_BOARD_SRC_DIR}/qemu/src/uart.cpp"
               "${UOS_BOARD_SRC_DIR}/qemu/src/led.cpp"
               "${UOS_BOARD_SRC_DIR}/qemu/src/clocks.cpp"
       PARENT_SCOPE)
endfunction ()

function (board_test_qemu_includes _app _out)
  set (${_out} "${UOS_BOARD_SRC_DIR}/qemu/include" PARENT_SCOPE)
endfunction ()

# -----------------------------------------------------------------------------
# The harness suites.
#
# They are not board tests: their sources live in the harness,
# tests/sources/<suite>/, and the directory under test/pico2/ carries only the
# name. The harness's platform add_subdirectory()s those sources into
# test::<suite>, so naming that library here brings its sources AND its include
# directory -- including the suite's own cmsis-plus/os-app-config.h, which is
# why the directory must stay off the board's include path.
#
# Everything else is a port test's: the board's glue and trace come from
# board_test_sources below, and the variant logic (hwd against the board, qemu
# against the generic device) is the builder's, unchanged.
# -----------------------------------------------------------------------------
set (_harness_suites rtos-apis mutex-stress cmsis-os-validator fp-switch)

# What a harness platform gives a suite and a board app does not, gathered in
# one interface so each suite names it once:
#
#   OS_USE_OS_APP_CONFIG_H  the kernel headers read the suite's own
#                           cmsis-plus/os-app-config.h only under it (the
#                           harness platforms get it from
#                           micro-os-plus::common-options);
#   the platform include    that os-app-config.h includes cmsis-plus/platform.h,
#                           which is the harness platform's -- the one this
#                           port's builder is add_subdirectory()'d from;
#   the syscall groups      the suites call gettimeofday() and the POSIX I/O
#                           layer. The qemu image already links all three; the
#                           hwd image links only the SoC, so it needs them here;
#   --wrap=os_main          the verdict. A suite RETURNS its code from os_main();
#                           hw.sh reads a RESULT line. Each placeholder
#                           harness-suite.cpp defines __wrap_os_main(), which
#                           calls the suite and prints that line; the code then
#                           goes back to the kernel's std::exit(), which is the
#                           same semihosting SYS_EXIT hw_result::ok()/fail() use.
#
# A suite exists only inside the harness -- test::<suite> is the harness's --
# so PLATFORM_NAME and the harness's platforms/ are always there when this is.
if (NOT TARGET pico2-harness-suite)
  add_library (pico2-harness-suite INTERFACE)
  # UOS_HARNESS_SUITE hands the board's os-app-config.h over to the suite's
  # (test/boards/pico2/include/cmsis-plus/os-app-config.h). The POSIX pair is
  # what the kernel's semihosting syscalls need (S_IREAD), exactly as the
  # pico2-1cpu platform defines them.
  target_compile_definitions (pico2-harness-suite INTERFACE
                              OS_USE_OS_APP_CONFIG_H UOS_HARNESS_SUITE
                              _POSIX_C_SOURCE=200809L _GNU_SOURCE)
  target_include_directories (pico2-harness-suite INTERFACE
                              "${CMAKE_SOURCE_DIR}/platforms/${PLATFORM_NAME}/include")
  target_link_libraries (pico2-harness-suite INTERFACE
                         micro-os-plus::iii-semihosting
                         micro-os-plus::iii-newlib-reent
                         micro-os-plus::iii-posix-io)
  target_link_options (pico2-harness-suite INTERFACE "-Wl,--wrap=os_main")
endif ()

function (board_test_libs _app _out)
  if (_app STREQUAL "rtos-apis")
    set (${_out} test::rtos-apis xpacks::chan-fatfs pico2-harness-suite
         PARENT_SCOPE)
  elseif (_app STREQUAL "mutex-stress")
    set (${_out} test::mutex-stress pico2-harness-suite PARENT_SCOPE)
  elseif (_app STREQUAL "fp-switch")
    set (${_out} test::fp-switch pico2-harness-suite PARENT_SCOPE)
  elseif (_app STREQUAL "cmsis-os-validator")
    set (${_out} test::cmsis-os-validator xpacks::arm-cmsis-os-validator
         pico2-harness-suite PARENT_SCOPE)
  else ()
    set (${_out} "" PARENT_SCOPE)
  endif ()
endfunction ()
