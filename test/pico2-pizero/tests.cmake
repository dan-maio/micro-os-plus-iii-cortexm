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
set (BOARD_TEST_NO_KERNEL smp-test0 exc-test)

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
