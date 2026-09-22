# -----------------------------------------------------------------------------
# What this board's tests need.
#
# Twelve tests, and three of the board's source sets are per-test. All of it is
# a transcription of the predecessor's rp2350B and rp2350B_psram Makefiles:
#
#   every test        uart.cpp and the RTOS glue
#   smp-test5         the glue only -- it defines its own uart:: in main.cpp
#   the SMP tests     multicore.cpp, the bootrom core-1 launch
#   smp-test1         single-core bring-up: no multicore.cpp, OS_NCPU=1
#   sc-test-ko        single-core kernel-object test: likewise
#   the PSRAM set     psram.cpp, PICO2_PSRAM=1, PICO2_PSRAM_CS_PIN=0, and a
#                     DIFFERENT LINKER SCRIPT -- .data/.bss/heap move to the
#                     external PSRAM at 0x11000000 and internal SRAM keeps only
#                     the PSRAM init routine and the main stack
#   the nested-clock  adc.cpp, plus the clock knobs each of the three was built
#   set               with: _200 runs the from-scratch clock tree at its
#                     default, the other two enable 250 MHz
#
# smp-test-nested is the only test here that is NOT also on the other two
# boards: it exists in the predecessor for rp2350B alone.
# -----------------------------------------------------------------------------

set (_single_core smp-test1 sc-test-ko)

# The four applications the predecessor built against the PSRAM.
set (_psram_tests smp-mat-test
                  smp-test-nested-clock
                  smp-test-nested-clock_200
                  smp-test-nested-clock_250)

# The three that drive the clock tree themselves and read the on-die
# temperature sensor through the ADC.
set (_clock_tests smp-test-nested-clock
                  smp-test-nested-clock_200
                  smp-test-nested-clock_250)

function (board_test_ncpu _app _out)
  if ("${_app}" IN_LIST _single_core)
    set (${_out} 1 PARENT_SCOPE)
  else ()
    set (${_out} "${UOS_BOARD_NCPU}" PARENT_SCOPE)
  endif ()
endfunction ()

function (board_test_sources _app _out)
  if (_app STREQUAL "smp-test5")
    set (_srcs "${UOS_BOARD_RTOS_GLUE}")
  else ()
    set (_srcs "${UOS_BOARD_UART}" "${UOS_BOARD_RTOS_GLUE}")
  endif ()
  if (NOT "${_app}" IN_LIST _single_core)
    list (APPEND _srcs "${UOS_BOARD_MULTICORE}")
  endif ()
  if ("${_app}" IN_LIST _clock_tests)
    list (APPEND _srcs "${UOS_BOARD_ADC}")
  endif ()
  if ("${_app}" IN_LIST _psram_tests)
    list (APPEND _srcs "${UOS_BOARD_PSRAM}")
  endif ()
  set (${_out} "${_srcs}" PARENT_SCOPE)
endfunction ()

function (board_test_linker _app _out)
  if ("${_app}" IN_LIST _psram_tests)
    set (${_out} "${UOS_BOARD_LINKER_PSRAM}" PARENT_SCOPE)
  else ()
    set (${_out} "" PARENT_SCOPE)
  endif ()
endfunction ()

function (board_test_defines _app _out)
  set (_defs "")
  if ("${_app}" IN_LIST _psram_tests)
    list (APPEND _defs PICO2_PSRAM=1 PICO2_PSRAM_CS_PIN=0)
  endif ()
  if ("${_app}" IN_LIST _clock_tests)
    list (APPEND _defs PICO2_CLOCK_FROM_SCRATCH)
    # _200 is the one that does NOT ask for 250 MHz; the other two do.
    if (NOT _app STREQUAL "smp-test-nested-clock_200")
      list (APPEND _defs PICO2_ENABLE_250MHZ)
    endif ()
  endif ()
  # The predecessor's smp-test1 Makefile was the only one to pin the system
  # clock, on both 16 MB boards -- the bring-up test runs the part at 150 MHz.
  if (_app STREQUAL "smp-test1")
    list (APPEND _defs PICO2_SYS_CLK_KHZ=150000)
  endif ()
  set (${_out} "${_defs}" PARENT_SCOPE)
endfunction ()

# The four tests that place their interrupt handlers in .data so they run from
# RAM instead of XIP flash. Their Makefiles compiled main.cpp -- and only
# main.cpp -- with these two options; see board_test_options() in
# test/CMakeLists.txt for what each one is for.
set (_ram_func_tests smp-test-nested
                     smp-test-nested-clock
                     smp-test-nested-clock_200
                     smp-test-nested-clock_250)

function (board_test_options _app _out)
  if ("${_app}" IN_LIST _ram_func_tests)
    set (${_out} "-g0;-mlong-calls" PARENT_SCOPE)
  else ()
    set (${_out} "" PARENT_SCOPE)
  endif ()
endfunction ()
