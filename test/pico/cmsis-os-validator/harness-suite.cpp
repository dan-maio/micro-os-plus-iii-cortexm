// The harness suite "cmsis-os-validator" on the Pico.
//
// Its sources are not here: they live in the harness,
// tests/sources/cmsis-os-validator/, and are named by board_test_libs() in
// tests.cmake. This file exists because the port's builder builds one
// application per directory that has a *.cpp, and this directory is where the
// application's NAME comes from, so that the platform registers
// cmsis-os-validator-hwd and cmsis-os-validator-qemu like every other test.
//
// The verdict wrapper, __wrap_os_main(), is the board's one, shared by its
// three suites.

#include "../harness-suite.hpp"
