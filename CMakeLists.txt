# -----------------------------------------------------------------------------
# micro-os-plus-iii-cortexm -- the Cortex-M port of µOS++ III.
#
# The third architecture project, beside -aarch32 and -aarch64 and built the
# same way. It consumes the kernel and the device drivers; neither of them
# knows this project exists, so the dependency runs one way and the kernel
# tree stays merge-clean against upstream.
#
# It holds no copy of them. Clone the siblings beside this one:
#
#   git clone .../micro-os-plus-iii-smp.git
#   git clone .../micro-os-plus-iii-devices.git
#
# and they are found automatically.
#
# WHAT THIS PORT IS. src/rtos/os-core.cpp is the upstream Cortex-M port,
# carried over unmodified: the PendSV context switch, the SysTick tick, and
# the critical sections. It is single-core, and on the three STM32F4 boards
# here that is what runs -- OS_NCPU=1, no kernel lock, no IPI. The kernel's
# SMP branches compile out; no kernel patch is involved, which is exactly how
# the predecessor's boards were proven.
#
# WHAT SMP WILL LOOK LIKE HERE, so the next reader does not assume otherwise.
# The predecessor also has a dual-core Cortex-M SMP core, in its pico2 tree.
# That core is NOT portable Cortex-M SMP and cannot simply replace this file:
# its kernel lock is the RP2350's SIO hardware spinlock 0, its IPI is the SIO
# inter-core FIFO on IRQ 25, and its CPU id is SIO_CPUID. All three are
# Rockchip-of-the-RP2350 -- silicon, not ISA.
#
# In this layout that code belongs to the BOARD, the way the Lyra's GIC-400
# SGI and the Pi's do: test/boards/<id>/src/rtos/. So cortexm gains SMP by a
# board arriving that supplies a lock and an IPI, not by this file changing.
# That is why the STM32 boards come first -- they are the OS_NCPU=1 witness
# that the split is real.
#
# Boards are build configurations of this one port. Pick one with
# -DBOARD=nucleof411 (default), weactf411 or weactf412.
#
# Exports: micro-os-plus::cortexm
# -----------------------------------------------------------------------------
cmake_minimum_required (VERSION 3.20)

project (micro-os-plus-iii-cortexm LANGUAGES C CXX ASM)

set (UOS_CORTEXM_ROOT "${CMAKE_CURRENT_SOURCE_DIR}" CACHE INTERNAL "")

# -----------------------------------------------------------------------------
# Dependencies: sibling clones by default.
get_filename_component (_uos_siblings "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)

set (UOS_SMP_DIR "${_uos_siblings}/micro-os-plus-iii-smp"
     CACHE PATH "µOS++ III SMP kernel working copy")
set (UOS_DEVICES_DIR "${_uos_siblings}/micro-os-plus-iii-devices"
     CACHE PATH "µOS++ III devices working copy")

foreach (_dep_var IN ITEMS UOS_SMP_DIR UOS_DEVICES_DIR)
  # A workspace may keep the siblings as `*.git` working copies (the xPack
  # development convention); fall back to that name before giving up.
  if (NOT EXISTS "${${_dep_var}}/CMakeLists.txt"
      AND EXISTS "${${_dep_var}}.git/CMakeLists.txt")
    set (${_dep_var} "${${_dep_var}}.git" CACHE PATH
         "µOS++ III sibling working copy" FORCE)
  endif ()

  if (NOT EXISTS "${${_dep_var}}/CMakeLists.txt")
    string (REPLACE "UOS_" "" _repo "${_dep_var}")
    string (REPLACE "_DIR" "" _repo "${_repo}")
    string (TOLOWER "${_repo}" _repo)
    message (FATAL_ERROR
      "Cannot find the ${_repo} repository at ${${_dep_var}}.\n"
      "This project keeps no copy of it. Clone it beside this one:\n"
      "    git clone <remote>/micro-os-plus-iii-${_repo}.git\n"
      "or point at an existing working copy:\n"
      "    cmake -D${_dep_var}=/path/to/it ...")
  endif ()
endforeach ()

add_subdirectory ("${UOS_SMP_DIR}" micro-os-plus-iii-smp)
add_subdirectory ("${UOS_DEVICES_DIR}" micro-os-plus-iii-devices)

# -----------------------------------------------------------------------------
# Which board. Boards are DISCOVERED, not listed: every directory under
# test/boards/ holding a board.cmake is a board, and that file is the only
# place its facts are written.
file (GLOB _board_decls CONFIGURE_DEPENDS
      "${CMAKE_CURRENT_SOURCE_DIR}/test/boards/*/board.cmake")
set (_board_ids "")
foreach (_decl IN LISTS _board_decls)
  get_filename_component (_dir "${_decl}" DIRECTORY)
  get_filename_component (_id "${_dir}" NAME)
  list (APPEND _board_ids "${_id}")
endforeach ()
list (SORT _board_ids)

set (BOARD "nucleof411" CACHE STRING "Target board (one of: ${_board_ids})")
set_property (CACHE BOARD PROPERTY STRINGS ${_board_ids})

if (NOT BOARD IN_LIST _board_ids)
  message (FATAL_ERROR
    "BOARD=${BOARD} is not a board of this port.\n"
    "Available: ${_board_ids}\n"
    "A board is a directory under test/boards/ containing a board.cmake.")
endif ()

include ("${CMAKE_CURRENT_SOURCE_DIR}/test/boards/${BOARD}/board.cmake")

# UOS_BOARD_LINKER_QEMU is deliberately not required: a board that QEMU
# cannot usefully model leaves it unset and is built for hardware only. All
# three STM32 boards are that case.
foreach (_required IN ITEMS UOS_BOARD_SRC_DIR UOS_BOARD_FLAGS UOS_BOARD_NCPU
                            UOS_BOARD_CAPS UOS_BOARD_LINKER_HW
                            UOS_BOARD_DEFINES)
  if (NOT DEFINED ${_required})
    message (FATAL_ERROR
      "test/boards/${BOARD}/board.cmake does not set ${_required}.")
  endif ()
endforeach ()

get_filename_component (UOS_BOARD_SRC_DIR "${UOS_BOARD_SRC_DIR}" ABSOLUTE)
if (NOT IS_DIRECTORY "${UOS_BOARD_SRC_DIR}")
  message (FATAL_ERROR
    "test/boards/${BOARD}/board.cmake points at ${UOS_BOARD_SRC_DIR}, which does not exist.")
endif ()

foreach (_v IN ITEMS UOS_BOARD_SRC_DIR UOS_BOARD_NCPU UOS_BOARD_CAPS
                     UOS_BOARD_LINKER_HW UOS_BOARD_LINKER_QEMU)
  # LINKER_QEMU may legitimately be unset -- a hardware-only board.
  set (${_v} "${${_v}}" CACHE INTERNAL "board fact")
endforeach ()

set (_board_flags  ${UOS_BOARD_FLAGS})
set (_board_define ${UOS_BOARD_DEFINES})
set (_board_libs   ${UOS_BOARD_LIBS})

file (GLOB _board_sources CONFIGURE_DEPENDS
      "${UOS_BOARD_SRC_DIR}/src/*.c" "${UOS_BOARD_SRC_DIR}/src/*.cpp"
      "${UOS_BOARD_SRC_DIR}/src/*.S" "${UOS_BOARD_SRC_DIR}/src/rtos/*.cpp")

# The newlib/OS glue is identical on every board of this port, so there is one
# copy of it, in test/boards/shared/. It is inserted where the glob used to
# find each board's own src/syscalls.c -- the glob is sorted, so the position
# is reproducible and link order does not move. Boards that compose their glue
# PER TEST name it themselves (the RP2350 ones, whose two kernel-less probes
# must not link it at all) and are recognised by having set
# UOS_BOARD_RTOS_GLUE.
if (NOT DEFINED UOS_BOARD_RTOS_GLUE)
  set (_uos_syscalls "${CMAKE_CURRENT_LIST_DIR}/test/boards/shared/syscalls.c")
  set (_uos_at -1)
  set (_i 0)
  foreach (_f IN LISTS _board_sources)
    get_filename_component (_n "${_f}" NAME)
    if (_n STRGREATER "syscalls.c" AND _uos_at EQUAL -1)
      set (_uos_at ${_i})
    endif ()
    math (EXPR _i "${_i} + 1")
  endforeach ()
  if (_uos_at EQUAL -1)
    list (APPEND _board_sources "${_uos_syscalls}")
  else ()
    list (INSERT _board_sources ${_uos_at} "${_uos_syscalls}")
  endif ()
endif ()

# -----------------------------------------------------------------------------
# The port itself, in two halves.
#
#   -board-interface   the BOARD: its flags, its include path, its start-up
#                      and its silicon support. True before any scheduler
#                      exists, and therefore linkable on its own.
#   -interface         that, plus the kernel and the ISA scheduler.
#
# The split is not tidiness. Two of the RP2350 tests compile no kernel at all
# -- they are the bare-metal probes that run before one exists -- and until
# this split there was no way to express them at all.
add_library (micro-os-plus-iii-cortexm-board-interface INTERFACE)
add_library (micro-os-plus-iii-cortexm-interface INTERFACE)

# WHICH PORT CORE, and why this is a board choice at all.
#
# Cortex-M is one ISA family but not one scheduler. Upstream's core (the
# default below) is single-core: PendSV, SysTick, PRIMASK critical sections.
# The RP2350 flavour is the predecessor's pico2 core, which is that plus an
# SMP branch gated on OS_USE_SMP_SCHEDULER -- and the SMP branch is RP2350
# silicon: SIO hardware spinlock 0 for the kernel lock, the SIO inter-core
# FIFO for the IPI, SIO_CPUID for the core index.
#
# The two are 91% the same file, and one day they should be one. They are not
# merged today for a stated reason: the three STM32 boards are hardware-proven
# on upstream's core, and moving them onto a different 748-line scheduler is a
# change that has to be re-proven on the desk, not assumed. A board therefore
# names the core it was proven with, and adding pico2 reaches no STM32 board.
#
# Defaults are upstream's, so a board that says nothing gets what the STM32
# boards get.
if (NOT DEFINED UOS_BOARD_PORT_CORE)
  set (UOS_BOARD_PORT_CORE "src/rtos/os-core.cpp")
endif ()
if (NOT DEFINED UOS_BOARD_PORT_INCLUDE)
  set (UOS_BOARD_PORT_INCLUDE "include")
endif ()

target_include_directories (
  micro-os-plus-iii-cortexm-board-interface
  INTERFACE "${UOS_BOARD_SRC_DIR}/include"
)
target_include_directories (
  micro-os-plus-iii-cortexm-interface
  INTERFACE "${UOS_BOARD_PORT_INCLUDE}"
)

target_sources (
  micro-os-plus-iii-cortexm-board-interface
  INTERFACE
        # Per-board: the start-up, the vectors, the clocks and the LED --
        # everything that is true of the silicon before a scheduler exists.
        ${_board_sources}
)

target_sources (
  micro-os-plus-iii-cortexm-interface
  INTERFACE
        # The ISA half of this port: the PendSV context switch, the SysTick
        # tick and the critical sections. Which one, see above.
        "${UOS_BOARD_PORT_CORE}"
        # Strong _Exit(): terminate through semihosting SYS_EXIT, so
        # std::exit() ends the run with a real exit code instead of idling.
        # The AArch32 and AArch64 ports carry the same file.
        src/semihosting-exit.cpp
)

target_compile_definitions (
  micro-os-plus-iii-cortexm-board-interface INTERFACE ${_board_define}
)

target_compile_options (
  micro-os-plus-iii-cortexm-board-interface
  INTERFACE ${_board_flags}
            # The predecessor's Makefiles carried this on every Cortex-M
            # build and none of the Cortex-A ones: without it every static
            # object with a destructor registers through __cxa_atexit and
            # drags in the exit machinery, which a 512 KiB part does not
            # want.
            $<$<COMPILE_LANGUAGE:CXX>:-fno-use-cxa-atexit>
)

target_link_options (
  micro-os-plus-iii-cortexm-board-interface
  INTERFACE ${_board_flags}
            # newlib-nano. The three boards' Makefiles all linked with it,
            # and the linker scripts are sized for it.
            --specs=nano.specs
)

target_link_libraries (
  micro-os-plus-iii-cortexm-board-interface
  INTERFACE
        # Upstream's generic startup and the trace backend are NOT here.
        # They are board choices, and the boards of this port disagree: the
        # STM32 boards take micro-os-plus::iii-startup and
        # ::iii-trace-semihosting, while the RP2350 has its own boot.S and
        # its own interrupt vectors and traces over the UART. A board names
        # both in UOS_BOARD_LIBS.
        ${_board_libs}
)

target_link_libraries (
  micro-os-plus-iii-cortexm-interface
  INTERFACE micro-os-plus-iii-cortexm-board-interface micro-os-plus::iii
)

add_library (micro-os-plus::cortexm ALIAS micro-os-plus-iii-cortexm-interface)
add_library (micro-os-plus::cortexm-bare
             ALIAS micro-os-plus-iii-cortexm-board-interface)

# The GENERIC port, for the `qemu` image of a board QEMU does not model.
#
# A board like the pico2 overrides the port core (os-core-rp2350.cpp) and the
# include directory (include-rp2350/) with RP2350-specific ones. Its emulated
# image must not: it links the generic core and the generic include, the same
# way the upstream Cortex-M port does, plus whatever device provides the
# vector table (the harness's device-qemu-cortexm). The board's tests are
# unchanged; only what runs under them differs.
add_library (micro-os-plus-iii-cortexm-qemu-interface INTERFACE
             EXCLUDE_FROM_ALL)
target_include_directories (micro-os-plus-iii-cortexm-qemu-interface
                            INTERFACE "include")
target_sources (
  micro-os-plus-iii-cortexm-qemu-interface
  INTERFACE "src/rtos/os-core.cpp" src/semihosting-exit.cpp
)
target_link_libraries (micro-os-plus-iii-cortexm-qemu-interface
                       INTERFACE micro-os-plus::iii)
# The console is the semihosting channel; the kernel's trace-semihosting
# backend only compiles when one of these is defined, otherwise the weak
# no-op trace::write wins and nothing is printed.
target_compile_definitions (
  micro-os-plus-iii-cortexm-qemu-interface
  INTERFACE OS_USE_TRACE_SEMIHOSTING_STDOUT
)
# The emulated image runs on QEMU's generic Cortex-M7 machine (mps2-an500),
# which is what the harness's device-qemu-cortexm supports -- its CMSIS core
# has no Cortex-M33. The board's own tests keep the board's flags; only the
# qemu image is built for the generic machine.
target_compile_options (
  micro-os-plus-iii-cortexm-qemu-interface
  INTERFACE -mcpu=cortex-m7 -mthumb -mfloat-abi=hard -mfpu=fpv5-d16
)
target_link_options (
  micro-os-plus-iii-cortexm-qemu-interface
  INTERFACE -mcpu=cortex-m7 -mthumb -mfloat-abi=hard -mfpu=fpv5-d16
            --specs=nano.specs
)
add_library (micro-os-plus::cortexm-qemu
             ALIAS micro-os-plus-iii-cortexm-qemu-interface)
message (
  VERBOSE
  "> micro-os-plus::cortexm-qemu -> micro-os-plus-iii-cortexm-qemu-interface"
)

# Which target a test links. The bare one exists for applications that run
# BEFORE a scheduler does -- see NO_KERNEL in uos-app.cmake. A port with no
# such test leaves UOS_PORT_BARE_LIB unset and the test loop rejects the
# request rather than quietly linking a kernel.
set (UOS_PORT_LIB      micro-os-plus::cortexm      CACHE INTERNAL "")
set (UOS_PORT_BARE_LIB micro-os-plus::cortexm-bare CACHE INTERNAL "")

# -----------------------------------------------------------------------------
if (PROJECT_IS_TOP_LEVEL AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test/CMakeLists.txt")
  add_subdirectory (test)
endif ()

message (STATUS "µOS++ III Cortex-M port: micro-os-plus::cortexm, board ${BOARD}")
