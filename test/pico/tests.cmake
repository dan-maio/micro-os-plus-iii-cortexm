# -----------------------------------------------------------------------------
# What this board's tests need.
#
# The board carries source sets that are NOT compiled into every application --
# see test/boards/pico/board.cmake for why. This file says which test takes
# which. It is the Pico 2's (test/pico2/tests.cmake), test for test: every
# Pico 2 test has its RP2040 twin here, composed the same way, except:
#
#   every test    the board's uart.cpp and its RTOS glue
#   smp-test5     the glue only -- it defines its own uart:: in main.cpp
#   the SMP tests multicore.cpp, the bootrom core-1 launch
#   smp-test1     no multicore.cpp: it is the single-core RTOS bring-up
#   sc-test-ko    no multicore.cpp: the single-core kernel-object test
#   the USB pair  TinyUSB -- the core, one class driver, and their own
#                 tusb_config.h, which is why their include directory goes
#                 first
#   the CDC test  no ws2812.cpp: the Pico has no RGB LED, so the test
#                 reports with the onboard LED
#   the qemu images  the generic Cortex-M0 core (cortexm-qemu-m0), run on
#                 QEMU's mps2-an385, as the harness's qemu-cortex-m0 platform
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

# Tests that get NO `qemu` image. The USB pair needs the RP2040's USB
# peripheral, the multicore ones need the SIO block the SMP design rests on,
# and QEMU's generic Cortex-M machine models neither. What is left --
# exc-test, smp-test1 and sc-test-ko, the single-core bring-up and the
# kernel-object test -- builds and runs emulated.
set (BOARD_TEST_HWD_ONLY
     cmsis-os-validator-ram
     rtos-apis-ram
     mutex-stress-ram
     smp-mat-test-ram
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
  # The harness suites are single-core here too. On the RP2040, launching core
  # 1 is a board act the test performs (multicore::launch_core1), and the
  # harness suites never do it: their mains are the kernel's portable
  # tests/sources/<suite>/, and this board has no platform-support.cpp to
  # install the secondary idle and release it (the AArch harness platforms
  # have one). Building them at OS_NCPU=2 made them link the SMP kernel while
  # CPU 1 never ran -- `os_idle_thread_core[1]` stayed nullptr and the
  # fp-switch suite's whole cross-core claim was vacuous (migrated_rounds
  # never incremented, and the verdict does not require it). Until the board
  # supplies the launch, they run single-core, which is what they actually do.
  # The qemu variant already forces NCPU=1 in test/CMakeLists.txt.
  if ("${_app}" IN_LIST _single_core OR "${_app}" IN_LIST _harness_suites)
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
    list (APPEND _srcs ${UOS_BOARD_USB_CORE_SOURCES} ${UOS_BOARD_USB_CDC_SOURCES})
  elseif ("${_app}" IN_LIST _usb_hid_tests)
    list (APPEND _srcs ${UOS_BOARD_USB_CORE_SOURCES} ${UOS_BOARD_USB_HID_SOURCES})
  endif ()

  set (${_out} "${_srcs}" PARENT_SCOPE)
endfunction ()

# The two USB gadget tests, the Pico 2's, on the RP2040's USB controller (the
# same controller: TinyUSB's rp2040 driver serves both chips).
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
  set (${_out} "" PARENT_SCOPE)
endfunction ()

# The `qemu` image does not link the board at all. The RP2040's boot.S, its
# interrupt vectors and its UART are what run on real silicon; on QEMU's
# generic Cortex-M machine (mps2-an385) the image is the kernel plus a generic
# device
# (micro-os-plus::device, whose vectors-cortexm.c owns the vector table) plus
# the board's semihosting console glue under qemu/. The test's own sources are
# unchanged.
function (board_test_qemu_libs _app _out)
  set (
    ${_out}
    # The generic ARMv6-M core: the image is built for the Cortex-M0 and runs
    # on QEMU's Cortex-M3 machine, as the harness's qemu-cortex-m0 platform.
    micro-os-plus::cortexm-qemu-m0
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
# tests/sources/<suite>/, and the directory under test/pico/ carries only the
# name. The harness's platform add_subdirectory()s those sources into
# test::<suite>, so naming that library here brings its sources AND its include
# directory -- including the suite's own cmsis-plus/os-app-config.h, which is
# why the directory must stay off the board's include path.
#
# Everything else is a port test's: the board's glue and trace come from
# board_test_sources below, and the variant logic (hwd against the board, qemu
# against the generic device) is the builder's, unchanged.
# -----------------------------------------------------------------------------
set (_harness_suites rtos-apis rtos-apis-ram mutex-stress mutex-stress-ram cmsis-os-validator cmsis-os-validator-ram fp-switch)

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
if (NOT TARGET pico-harness-suite)
  add_library (pico-harness-suite INTERFACE)
  # UOS_HARNESS_SUITE hands the board's os-app-config.h over to the suite's
  # (test/boards/pico/include/cmsis-plus/os-app-config.h). The POSIX pair is
  # what the kernel's semihosting syscalls need (S_IREAD), exactly as the
  # pico2-1cpu platform defines them.
  target_compile_definitions (pico-harness-suite INTERFACE
                              OS_USE_OS_APP_CONFIG_H UOS_HARNESS_SUITE
                              _POSIX_C_SOURCE=200809L _GNU_SOURCE)
  target_include_directories (pico-harness-suite INTERFACE
                              "${CMAKE_SOURCE_DIR}/platforms/${PLATFORM_NAME}/include")
  target_link_libraries (pico-harness-suite INTERFACE
                         micro-os-plus::iii-semihosting
                         micro-os-plus::iii-newlib-reent
                         micro-os-plus::iii-posix-io)
  target_link_options (pico-harness-suite INTERFACE "-Wl,--wrap=os_main")
endif ()

function (board_test_libs _app _out)
  if (_app STREQUAL "rtos-apis" OR _app STREQUAL "rtos-apis-ram")
    set (${_out} test::rtos-apis xpacks::chan-fatfs pico-harness-suite
         PARENT_SCOPE)
  elseif (_app STREQUAL "mutex-stress" OR _app STREQUAL "mutex-stress-ram")
    set (${_out} test::mutex-stress pico-harness-suite PARENT_SCOPE)
  elseif (_app STREQUAL "fp-switch")
    set (${_out} test::fp-switch pico-harness-suite PARENT_SCOPE)
  elseif (_app STREQUAL "cmsis-os-validator" OR _app STREQUAL "cmsis-os-validator-ram")
    set (${_out} test::cmsis-os-validator xpacks::arm-cmsis-os-validator
         pico-harness-suite PARENT_SCOPE)
  else ()
    set (${_out} "" PARENT_SCOPE)
  endif ()
endfunction ()

function (board_test_linker _app _out)
  if (_app STREQUAL "cmsis-os-validator-ram")
    set (${_out} "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/cmsis-os-validator-ram/linker-ram.ld" PARENT_SCOPE)
  elseif (_app STREQUAL "rtos-apis-ram")
    set (${_out} "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/rtos-apis-ram/linker-ram.ld" PARENT_SCOPE)
  elseif (_app STREQUAL "mutex-stress-ram")
    set (${_out} "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/mutex-stress-ram/linker-ram.ld" PARENT_SCOPE)
  elseif (_app STREQUAL "smp-mat-test-ram")
    set (${_out} "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/smp-mat-test-ram/linker-ram.ld" PARENT_SCOPE)
  else ()
    set (${_out} "" PARENT_SCOPE)
  endif ()
endfunction ()
