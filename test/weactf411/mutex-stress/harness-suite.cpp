// The harness suite "mutex-stress" on the WeAct F411CE.
//
// Its sources are not here: they live in the harness,
// tests/sources/mutex-stress/, and are named by board_test_libs() in
// tests.cmake. This file exists because the port's builder builds one
// application per directory that has a *.cpp, and this directory is where the
// application's NAME comes from, so that the platform registers
// mutex-stress-hwd like every other test.
//
// The verdict wrapper, __wrap_os_main(), is the board's one, shared by its
// three suites.

#include "../harness-suite.hpp"
