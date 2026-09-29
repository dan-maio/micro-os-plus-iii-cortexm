// The harness suite "fp-switch" on the WeAct RP2350B.
//
// Its sources are not here: they live in the harness, tests/sources/fp-switch/,
// and are named by board_test_libs() in tests.cmake. This file exists because
// the port's builder builds one application per directory that has a *.cpp,
// and this directory is where the application's NAME comes from, so that the
// platform registers fp-switch-hwd and fp-switch-qemu like every other test.
//
// The verdict wrapper, __wrap_os_main(), is the Pico 2's: same silicon, same
// UART and semihosting, so the one copy under test/pico2/ is used as is.

#include "../../pico2/harness-suite.hpp"
