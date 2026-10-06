# -----------------------------------------------------------------------------
#
# This file is part of the µOS++ project (https://micro-os-plus.github.io/).
# Copyright (c) 2021-2025 Liviu Ionescu. All rights reserved.
#
# Permission to use, copy, modify, and/or distribute this software
# for any purpose is hereby granted, under the terms of the MIT license.
#
# If a copy of the license was not distributed with this file, it can
# be obtained from https://opensource.org/licenses/mit/.
#
# -----------------------------------------------------------------------------

# This file is intended to be consumed by applications with:
#
# `add_subdirectory("xpacks/@micro-os-plus/micro-os-plus-iii-cortexm")`
#
# The result is an interface library that can be added to the linker with:
#
# `target_link_libraries(your-target PUBLIC micro-os-plus::iii-cortexm)`

# -----------------------------------------------------------------------------
## Preamble ##

# https://cmake.org/cmake/help/v3.20/
cmake_minimum_required(VERSION 3.20)

project(micro-os-plus-iii-cortexm
  DESCRIPTION "µOS++ Cortex-M port"
)

if(COMMAND xpack_get_package_name_and_version)
  xpack_get_package_name_and_version("${CMAKE_CURRENT_SOURCE_DIR}/package.json")
  message(VERBOSE "Processing xPack ${PACKAGE_JSON_NAME}@${PACKAGE_JSON_VERSION}...")
endif()

# -----------------------------------------------------------------------------
## The project library definitions ##

add_library(micro-os-plus-iii-cortexm-interface INTERFACE EXCLUDE_FROM_ALL)

# -----------------------------------------------------------------------------
# Target settings.

target_include_directories(micro-os-plus-iii-cortexm-interface INTERFACE
  "include"
)

target_sources(micro-os-plus-iii-cortexm-interface INTERFACE
  src/rtos/os-core.cpp
)

target_compile_definitions(micro-os-plus-iii-cortexm-interface INTERFACE
  # None.
)

target_compile_options(micro-os-plus-iii-cortexm-interface INTERFACE
  # None.
)

target_link_libraries(micro-os-plus-iii-cortexm-interface INTERFACE
  # None.
)

if (COMMAND xpack_display_target_lists)
  xpack_display_target_lists(micro-os-plus-iii-cortexm-interface)
endif()

# -----------------------------------------------------------------------------
# Aliases.

# https://cmake.org/cmake/help/v3.20/command/add_library.html#alias-libraries
add_library(micro-os-plus::iii-cortexm ALIAS micro-os-plus-iii-cortexm-interface)
message(VERBOSE "> micro-os-plus::iii-cortexm -> micro-os-plus-iii-cortexm-interface")


# Dissolved SoC targets: STM32F4xx and RP2350
# The devices repository was dissolved into the architecture ports.
if (NOT TARGET micro-os-plus::soc-stm32f411xe)
  add_library (micro-os-plus-iii-soc-stm32f4xx-common-interface INTERFACE)
  target_include_directories (
    micro-os-plus-iii-soc-stm32f4xx-common-interface
    INTERFACE
      "${CMAKE_CURRENT_SOURCE_DIR}/soc/stm32f4xx/include"
      "${CMAKE_CURRENT_SOURCE_DIR}/soc/stm32f4xx/include/cmsis"
  )
  target_sources (
    micro-os-plus-iii-soc-stm32f4xx-common-interface
    INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/soc/stm32f4xx/src/system_stm32f4xx.c"
  )

  add_library (micro-os-plus-iii-soc-stm32f411xe-interface INTERFACE)
  target_link_libraries (
    micro-os-plus-iii-soc-stm32f411xe-interface
    INTERFACE micro-os-plus-iii-soc-stm32f4xx-common-interface
  )
  target_sources (
    micro-os-plus-iii-soc-stm32f411xe-interface
    INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/soc/stm32f4xx/src/vectors_stm32f411xe.c"
  )
  add_library (micro-os-plus::soc-stm32f411xe
               ALIAS micro-os-plus-iii-soc-stm32f411xe-interface)

  add_library (micro-os-plus-iii-soc-stm32f412rx-interface INTERFACE)
  target_link_libraries (
    micro-os-plus-iii-soc-stm32f412rx-interface
    INTERFACE micro-os-plus-iii-soc-stm32f4xx-common-interface
  )
  target_sources (
    micro-os-plus-iii-soc-stm32f412rx-interface
    INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/soc/stm32f4xx/src/vectors_stm32f412rx.c"
  )
  add_library (micro-os-plus::soc-stm32f412rx
               ALIAS micro-os-plus-iii-soc-stm32f412rx-interface)
endif ()

if (NOT TARGET micro-os-plus::soc-rp2350)
  add_library (micro-os-plus-iii-soc-rp2350-interface INTERFACE)
  target_include_directories (
    micro-os-plus-iii-soc-rp2350-interface
    INTERFACE
      "${CMAKE_CURRENT_SOURCE_DIR}/soc/rp2350/include"
      "${CMAKE_CURRENT_SOURCE_DIR}/soc/rp2350/include/cmsis"
  )
  target_sources (
    micro-os-plus-iii-soc-rp2350-interface
    INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/soc/rp2350/src/system_rp2350.c"
  )
  add_library (micro-os-plus::soc-rp2350
               ALIAS micro-os-plus-iii-soc-rp2350-interface)
endif ()

# -----------------------------------------------------------------------------
