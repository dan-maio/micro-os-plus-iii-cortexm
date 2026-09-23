// The harness suite "mutex-stress".
//
// Its sources are not here: they live in the harness,
// tests/sources/mutex-stress/, and are named by board_test_libs() in tests.cmake.
// This file exists because the port's builder builds one application per
// directory that has a *.cpp, and this directory is where the application's
// NAME comes from -- test/pico2/mutex-stress/ -- so that the platform registers
// mutex-stress-hwd and mutex-stress-qemu like every other test.
