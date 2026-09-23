# -----------------------------------------------------------------------------
# Test builds for the Cortex-M port.
#
# The same file as the aarch32 and aarch64 ports carry, and deliberately so:
# every board owns its tests, they live in test/<board>/, one directory per
# application, and this file builds whatever is there.
#
# How many images per test is the board's decision, not this file's. A board
# that sets UOS_BOARD_LINKER_QEMU gets two -- `qemu` for the emulated suite,
# `hwd` for real silicon over OpenOCD. A board that does not gets `hwd`
# alone. All three STM32 boards here are the second case: they are programmed
# over an ST-Link and run on the desk.
#
# There is no shared test list and no capability filter, because there is
# nothing left to filter: a board builds the directories it has.
#
# Each board's test/ also carries the support its tests share -- include/ and
# src/ -- and a tests.cmake naming the few knobs a directory listing cannot
# express.
#
# BOARD selects the board.
# -----------------------------------------------------------------------------

option (UOS_DEBUG_BOOT "Compile the hwd builds with the early-boot markers" OFF)

# UOS_BOARD_SRC_DIR is where the board's CODE is. No two boards of this port
# share it: the two F411 boards are the same silicon but different hardware,
# and they have no source file in common -- only the SoC support, which is a
# devices/ target.
#
# The TESTS are never shared. Each board has its own directory here --
# test/<board>/ -- so changing or adding a test reaches one board and no other.
set (_boards_dir "${UOS_BOARD_SRC_DIR}")
set (_test_dir   "${CMAKE_CURRENT_SOURCE_DIR}/${BOARD}")

if (NOT IS_DIRECTORY "${_test_dir}")
  message (FATAL_ERROR
           "test/${BOARD} does not exist. Every board has a directory here -- "
           "it is where that board's test applications live, even when the "
           "board shares its src/ and include/ with another.")
endif ()

set (_linker_hwd  "${UOS_BOARD_LINKER_HW}")
set (_linker_qemu "${UOS_BOARD_LINKER_QEMU}")

# One image per test, or two.
#
# Most boards get both: `qemu` for the emulated suite and `hwd` for the probe.
# A board whose board.cmake sets no UOS_BOARD_LINKER_QEMU gets only `hwd`,
# because there is nowhere for the other image to run.
#
# That is not a limitation of QEMU's ARM support -- it is what the board IS.
# The Luckfox Lyra is the case: QEMU models none of the RK3506's own blocks,
# so an emulated Lyra would be a generic Cortex-A7 wearing the name, and
# every test that makes the board worth having -- the DWC2 gadget, the SD
# host, the Cortex-M0 mailbox -- would be exactly the part not emulated. The
# board is tested on hardware, and says so by staying silent here rather than
# by carrying a second linker script and a suite that cannot pass.
if (_linker_qemu)
  set (_variants qemu hwd)
else ()
  set (_variants hwd)
endif ()

# What every application of this port is compiled with. The predecessor's
# Makefiles carried exactly -DTRACE here; the part number, the app-config
# switch and the crystal are board facts and come from board.cmake.
set (_common_defines TRACE SEMIHOST)

# The knobs a directory listing cannot express. Optional: a board whose tests
# need nothing beyond the defaults has no tests.cmake.
set (BOARD_TEST_NEED_DEVICES "")
# Tests that bring their own start-up code and must NOT also get test/src/.
set (BOARD_TEST_SELF_CONTAINED "")
# Tests that compile NO KERNEL at all -- the bare-metal probes that run before
# a scheduler exists. They link the port's bare target instead of its full one
# and are declared NO_KERNEL, so no kernel, no port scheduler and no OS_NCPU
# reach them. A port with no such test never sets UOS_PORT_BARE_LIB.
set (BOARD_TEST_NO_KERNEL "")
# Tests that only make sense on the board and get NO `qemu` image, even when
# the board has a QEMU linker. A board whose silicon QEMU models only partly
# -- the RP2350's USB and PSRAM are the case -- still runs its core tests
# emulated and says so per test here, rather than dropping the whole board
# from the emulator.
set (BOARD_TEST_HWD_ONLY "")
function (board_test_defines _app _out)
  set (${_out} "" PARENT_SCOPE)
endfunction ()
# Sources and include directories ONE test needs beyond its own directory.
#
# A test carried over from the predecessor may bring a subdirectory of its own
# -- usb/, sd/, m0/ -- and may take part of the code the board carries for its
# tests. It cannot be a glob: several of those tests ship their own copy of a
# file the board also has, so which copy a test compiles is a decision, not a
# directory listing. The board's tests.cmake makes it, one test at a time,
# exactly as that test's Makefile did.
function (board_test_sources _app _out)
  set (${_out} "" PARENT_SCOPE)
endfunction ()
function (board_test_includes _app _out)
  set (${_out} "" PARENT_SCOPE)
endfunction ()
# The LINKER SCRIPT one test needs instead of the board's.
#
# A board is one piece of hardware, so one memory map -- until a test changes
# where it runs. On the RP2350B the PSRAM tests link their data into the
# external PSRAM at 0x11000000 while every other test on the same board keeps
# its data in internal SRAM, and the predecessor said so with one LDSCRIPT
# line per Makefile. Returning nothing keeps the board's own script, which is
# what every test of every other board does.
function (board_test_linker _app _out)
  set (${_out} "" PARENT_SCOPE)
endfunction ()
# COMPILE OPTIONS one test needs on ITS OWN sources.
#
# Not on the kernel it links: these are options that belong to the way a
# particular application is written. The RP2350's nested-interrupt tests are
# the case -- they place their handlers in .data so the code runs from RAM
# rather than XIP flash, and the predecessor's Makefiles gave exactly those
# main.cpp files "-g0 -mlong-calls", with the comment "disabling debug symbols
# and enabling long-calls for RAM function support". Both halves are load
# bearing: a call from flash to a .data function is out of a Thumb BL's reach,
# and GCC's location views cannot describe a function whose section moved
# ("Error: leb128 operand is an undefined symbol").
function (board_test_options _app _out)
  set (${_out} "" PARENT_SCOPE)
endfunction ()
# How many CPUs ONE test runs on, when that is not the whole board.
#
# NCPU is a board fact for every board here but one: the RP2350 has two cores,
# and two of its tests are deliberately single-core -- smp-test1 is the
# single-core RTOS bring-up and sc-test-ko is the single-core twin of
# smp-test-ko, the only test on this silicon that exercises the kernel's
# NON-SMP branch. Their Makefiles simply did not define OS_USE_SMP_SCHEDULER.
# uos_add_app() derives that define from NCPU, so returning 1 here is the
# whole of it.
function (board_test_ncpu _app _out)
  set (${_out} "${UOS_BOARD_NCPU}" PARENT_SCOPE)
endfunction ()
# What the QEMU variant needs instead of the board.
#
# A board whose silicon QEMU does not model cannot run its board tests
# emulated by linking the board: the pico2 is the case. These three hooks let
# the board say what the `qemu` image links instead -- the kernel, a generic
# device with its own vector table, and a semihosting console -- while the
# test's own sources are unchanged. Returning nothing keeps the board's.
function (board_test_qemu_libs _app _out)
  set (${_out} "" PARENT_SCOPE)
endfunction ()
function (board_test_qemu_sources _app _out)
  set (${_out} "" PARENT_SCOPE)
endfunction ()
function (board_test_qemu_includes _app _out)
  set (${_out} "" PARENT_SCOPE)
endfunction ()
if (EXISTS "${_test_dir}/tests.cmake")
  include ("${_test_dir}/tests.cmake")
endif ()

# The support this board's tests share: one copy per board, not per test.
file (GLOB _test_support CONFIGURE_DEPENDS "${_test_dir}/src/*.cpp")
# `boards/shared` is the one include directory every board's tests get: it
# holds the cross-board helpers, hw_result.hpp among them, which would
# otherwise be copied into each board's own include/.
set (_test_includes "${_test_dir}/include"
                    "${CMAKE_CURRENT_SOURCE_DIR}/boards/shared")

# One application per directory. include/ and src/ are the support above, not
# applications, so they are skipped.
file (GLOB _test_dirs LIST_DIRECTORIES true CONFIGURE_DEPENDS "${_test_dir}/*")

set (_apps "")
foreach (_dir IN LISTS _test_dirs)
  if (NOT IS_DIRECTORY "${_dir}")
    continue ()
  endif ()
  get_filename_component (_app "${_dir}" NAME)
  if (_app STREQUAL "include" OR _app STREQUAL "src")
    continue ()
  endif ()
  file (GLOB _srcs CONFIGURE_DEPENDS "${_dir}/*.cpp")
  if (NOT _srcs)
    continue ()
  endif ()
  list (APPEND _apps "${_app}")

  board_test_defines  ("${_app}" _app_defines)
  board_test_sources  ("${_app}" _app_sources)
  board_test_includes ("${_app}" _app_includes)
  board_test_linker   ("${_app}" _app_linker)
  board_test_options  ("${_app}" _app_options)
  board_test_ncpu     ("${_app}" _app_ncpu)

  # A test carried over whole from the predecessor defines its own
  # smp_install_boot_threads(); adding the board's shared copy would be a
  # duplicate definition, and picking one would mean editing a test that
  # already passed on hardware.
  set (_app_support ${_test_support})
  if ("${_app}" IN_LIST BOARD_TEST_SELF_CONTAINED)
    set (_app_support "")
  endif ()

  # The application's own sources carry its own options, if it has any --
  # every other source of the target keeps the build's.
  if (_app_options)
    set_source_files_properties (${_srcs} PROPERTIES COMPILE_OPTIONS "${_app_options}")
  endif ()

  set (_libs micro-os-plus::cortexm)
  set (_no_kernel "")
  if ("${_app}" IN_LIST BOARD_TEST_NO_KERNEL)
    if (NOT UOS_PORT_BARE_LIB)
      message (FATAL_ERROR
               "${_app} is listed in BOARD_TEST_NO_KERNEL, but this port "
               "declares no bare target (UOS_PORT_BARE_LIB).")
    endif ()
    set (_libs ${UOS_PORT_BARE_LIB})
    set (_no_kernel NO_KERNEL)
  endif ()
  if ("${_app}" IN_LIST BOARD_TEST_NEED_DEVICES)
    list (APPEND _libs ${UOS_BOARD_DEVICES})
  endif ()

  # A test listed HWD_ONLY gets the hardware image even on a board that has a
  # QEMU linker; every other test gets every variant the board has.
  set (_app_variants ${_variants})
  if ("${_app}" IN_LIST BOARD_TEST_HWD_ONLY)
    set (_app_variants hwd)
  endif ()

  foreach (_variant IN LISTS _app_variants)
    set (_variant_libs ${_libs})
    set (_variant_sources ${_app_sources})
    set (_variant_includes ${_app_includes})
    if (_variant STREQUAL "hwd")
      # DEBUG_BOOT is opt-in: every early-boot marker is a semihosting trap,
      # and under a JTAG probe each one costs real time. Turn it on only when
      # bringing a board up.
      set (_variant_defines HW_BUILD)
      if (UOS_DEBUG_BOOT)
        list (APPEND _variant_defines DEBUG_BOOT)
      endif ()
      set (_variant_linker "${_linker_hwd}")
      # A test that names its own script takes it. Only the hardware image:
      # an emulated machine has the memory map QEMU gives it, not the one a
      # board's external memory happens to have.
      if (_app_linker)
        set (_variant_linker "${_app_linker}")
      endif ()
    else ()
      # QEMU_BUILD says "this image runs under the emulator", which is not the
      # same as "not hardware": a board whose silicon QEMU does not model
      # builds its emulated image against the `virt` machine instead.
      set (_variant_defines QEMU_BUILD)
      set (_variant_linker "${_linker_qemu}")
      # The emulated image may replace the board entirely (pico2): the kernel
      # plus a generic device with its own vectors, and a semihosting console.
      board_test_qemu_libs ("${_app}" _qemu_libs)
      if (_qemu_libs)
        # The board is replaced, so the board's linker script is too: the
        # device that replaces it carries its own (-T from its CMakeLists).
        set (_variant_libs ${_qemu_libs})
        set (_variant_linker "")
      endif ()
      board_test_qemu_sources ("${_app}" _qemu_sources)
      if (_qemu_sources)
        # The board's own sources are what run on silicon; the emulated image
        # replaces them with its own (the pico2's qemu glue).
        set (_variant_sources ${_qemu_sources})
      endif ()
      board_test_qemu_includes ("${_app}" _qemu_includes)
      list (APPEND _variant_includes ${_qemu_includes})
    endif ()

    uos_add_app (
      "${_app}-${_variant}"
      ${_no_kernel}
      SOURCES ${_srcs} ${_app_support} ${_variant_sources}
      NCPU ${_app_ncpu}
      LINKER_SCRIPT "${_variant_linker}"
      DEFINES ${_common_defines} ${_app_defines} ${_variant_defines}
      # ${_dir} is the application's own directory, which the predecessor's
      # Makefiles all put on the include path as -I. -- an application whose
      # headers sit beside its sources and includes them as <foo.hpp> needs
      # it, and one that writes "foo.hpp" never notices. Each application is
      # its own target, so this reaches no other.
      INCLUDES ${_variant_includes} ${_dir} ${_test_includes}
      LIBRARIES ${_variant_libs}
      OUTPUT_NAME "${_app}-${_variant}"
    )
  endforeach ()
endforeach ()

list (LENGTH _apps _n)
list (LENGTH _variants _nv)
math (EXPR _n "${_n} * ${_nv}")
message (STATUS "µOS++ III Cortex-M tests: ${_n} targets, board=${BOARD}, from ${_test_dir}")
