// The harness suite "cmsis-os-validator".
//
// Its sources are not here: they live in the harness,
// tests/sources/cmsis-os-validator/, and are named by board_test_libs() in tests.cmake.
// This file exists because the port's builder builds one application per
// directory that has a *.cpp, and this directory is where the application's
// NAME comes from -- test/pico2/cmsis-os-validator/ -- so that the platform registers
// cmsis-os-validator-hwd and cmsis-os-validator-qemu like every other test.
