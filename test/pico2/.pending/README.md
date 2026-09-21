# pico2 tests waiting on a capability

## `smp-test0`

Carried over byte for byte from
`micro-os-plus-iii-smp-old/pico2/micro-os-plus-iii-pico2/smp-test0/`. It is
here rather than one directory up because it is **not an RTOS application**,
and `test/*` is globbed -- a directory that cannot link would break the
board's build for everyone.

Its Makefile compiles exactly nine files and not one of them is the kernel:

```
boot.S  interrupt-vectors.S  clocks.cpp  uart.cpp  led.cpp
multicore.cpp  init-fini-stubs.c  system_rp2350.c  main.cpp
```

No `micro-os-plus-iii`, no port `os-core.cpp`. It is the bare-metal dual-core
bring-up test: it proves the RP2350's second core starts and that the two can
talk, *before* any scheduler exists. That is why it also defines its own
`HardFault_Handler` and takes none of the board's RTOS glue.

`uos_add_app()` always links `micro-os-plus::iii`, so there is no way to
express this today. Making one is a change to the shared helper that every
architecture project uses, for one test -- so it is a decision to take
deliberately, not a detail to slip in.

It is **not** to be rewritten as an RTOS test. Its value is precisely that it
runs without one.
