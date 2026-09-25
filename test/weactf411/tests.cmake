# -----------------------------------------------------------------------------
# What this board's tests need.
#
# mos-test1 and spi-pipeline are the board's own tests and keep the defaults:
# one CPU, the kernel's non-SMP branch.
#
# The three harness suites run here under the kernel's SMP scheduler at
# OS_NCPU=1: the SMP code paths (per-CPU current thread and lock state, the
# affinity-aware picker, the stack_ptr liveness rule) on one Cortex-M4F, at
# the board's 100 MHz (HSE 25 MHz -> PLL, os-hooks.c). The port's upstream
# core carries that branch (src/rtos/os-core.cpp, gated on
# OS_USE_SMP_SCHEDULER); its kernel lock is interrupt masking with an
# owner/depth record, since one CPU has no other core to exclude. The suites
# use neither the LED (PC13) nor the SPI NOR flash; their verdict goes out on
# semihosting and on USART1 (PA9/PA10).
# -----------------------------------------------------------------------------

# The harness suites.
#
# They are not board tests: their sources live in the harness,
# tests/sources/<suite>/, and the directory under test/weactf411/ carries only
# the name. The harness's platform add_subdirectory()s those sources into
# test::<suite>, so naming that library here brings its sources AND its include
# directory -- including the suite's own cmsis-plus/os-app-config.h, which the
# board's os-app-config.h hands over to (UOS_HARNESS_SUITE).
set (_harness_suites rtos-apis mutex-stress cmsis-os-validator)

# What a harness platform gives a suite and a board app does not, gathered in
# one interface so each suite names it once (the same set as pico2's):
#
#   OS_USE_OS_APP_CONFIG_H  the kernel headers read the suite's own
#                           cmsis-plus/os-app-config.h only under it;
#   the platform include    that os-app-config.h includes cmsis-plus/platform.h,
#                           which is the harness platform's;
#   the syscall groups      the suites call gettimeofday() and the POSIX I/O
#                           layer; the hwd image otherwise links only the SoC;
#   --wrap=os_main          the verdict: harness-suite.cpp defines
#                           __wrap_os_main(), which calls the suite and prints
#                           the RESULT line hw.sh reads.
#
# A suite exists only inside the harness -- test::<suite> is the harness's --
# so PLATFORM_NAME and the harness's platforms/ are always there when this is.
if (NOT TARGET weactf411-harness-suite)
  add_library (weactf411-harness-suite INTERFACE)
  target_compile_definitions (weactf411-harness-suite INTERFACE
                              OS_USE_OS_APP_CONFIG_H UOS_HARNESS_SUITE
                              _POSIX_C_SOURCE=200809L _GNU_SOURCE)
  target_include_directories (weactf411-harness-suite INTERFACE
                              "${CMAKE_SOURCE_DIR}/platforms/${PLATFORM_NAME}/include")
  target_link_libraries (weactf411-harness-suite INTERFACE
                         micro-os-plus::iii-semihosting
                         micro-os-plus::iii-newlib-reent
                         micro-os-plus::iii-posix-io)
  target_link_options (weactf411-harness-suite INTERFACE "-Wl,--wrap=os_main")
endif ()

function (board_test_defines _app _out)
  # The suites build the kernel's SMP branch. NCPU stays the board's 1, so
  # uos_add_app() defines OS_NCPU=1 and, on its own, no SMP scheduler: the
  # define is added here, for these three applications only.
  if ("${_app}" IN_LIST _harness_suites)
    set (${_out} OS_USE_SMP_SCHEDULER=1 PARENT_SCOPE)
  else ()
    set (${_out} "" PARENT_SCOPE)
  endif ()
endfunction ()

function (board_test_libs _app _out)
  if (_app STREQUAL "rtos-apis")
    set (${_out} test::rtos-apis xpacks::chan-fatfs weactf411-harness-suite
         PARENT_SCOPE)
  elseif (_app STREQUAL "mutex-stress")
    set (${_out} test::mutex-stress weactf411-harness-suite PARENT_SCOPE)
  elseif (_app STREQUAL "cmsis-os-validator")
    set (${_out} test::cmsis-os-validator xpacks::arm-cmsis-os-validator
         weactf411-harness-suite PARENT_SCOPE)
  else ()
    set (${_out} "" PARENT_SCOPE)
  endif ()
endfunction ()
