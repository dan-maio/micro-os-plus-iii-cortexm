# -----------------------------------------------------------------------------
# What this board's tests need.
#
# The board carries two source sets that are NOT compiled into every
# application -- see test/boards/pico2/board.cmake for why. This file says
# which test takes which, and it is a transcription of the predecessor's
# seven Makefiles, not a new arrangement:
#
#   every test         the board's uart.cpp and its RTOS glue
#   smp-test5          the glue only -- it defines its own uart:: in main.cpp
# -----------------------------------------------------------------------------

function (board_test_sources _app _out)
  if (_app STREQUAL "smp-test5")
    set (${_out} "${UOS_BOARD_RTOS_GLUE}" PARENT_SCOPE)
  else ()
    set (${_out} "${UOS_BOARD_UART};${UOS_BOARD_RTOS_GLUE}" PARENT_SCOPE)
  endif ()
endfunction ()
