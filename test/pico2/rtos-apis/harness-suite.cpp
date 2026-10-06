// The harness suite "rtos-apis" on the Pico 2.
//
// Its sources are not here: they live in the harness,
// tests/sources/rtos-apis/, and are named by board_test_libs() in tests.cmake.
// This file exists because the port's builder builds one application per
// directory that has a *.cpp, and this directory is where the application's
// NAME comes from, so that the platform registers rtos-apis-hwd and
// rtos-apis-qemu like every other test.
//
// The verdict wrapper, __wrap_os_main(), is the board's one, shared by its
// three suites.

#include "../harness-suite.hpp"
