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
  set (${_out} "${_srcs}" PARENT_SCOPE)
endfunction ()

function (board_test_defines _app _out)
  # The predecessor's smp-test1 Makefile was the only one here to pin the
  # system clock: the bring-up test runs the part at 150 MHz.
  if (_app STREQUAL "smp-test1")
    set (${_out} PICO2_SYS_CLK_KHZ=150000 PARENT_SCOPE)
  else ()
    set (${_out} "" PARENT_SCOPE)
  endif ()
endfunction ()
