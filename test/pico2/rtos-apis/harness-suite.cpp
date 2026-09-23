// The harness suite "rtos-apis".
//
// Its sources are not here: they live in the harness,
// tests/sources/rtos-apis/, and are named by board_test_libs() in tests.cmake.
// This file exists because the port's builder builds one application per
// directory that has a *.cpp, and this directory is where the application's
// NAME comes from -- test/pico2/rtos-apis/ -- so that the platform registers
// rtos-apis-hwd and rtos-apis-qemu like every other test.
