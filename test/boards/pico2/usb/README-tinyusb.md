# usb — RP2350 USB device drivers (TinyUSB): CDC-ACM and HID

The USB **device** support of the RP2350 boards (`pico2`, and `pico2-pizero`,
whose `board.cmake` includes pico2's), built on **vendored TinyUSB** and wired
into the µOS++ SMP kernel. Two gadget tests use it:

| Test | Class | Enumerates as | Host side |
|---|---|---|---|
| `smp-test-usb-cdc-acm` | CDC-ACM (virtual serial) | `Cafe:4013`, a new `/dev/ttyACM*` | a terminal |
| `smp-test-usb-hid` | generic in/out HID | `Cafe:4012` | `host/kbd_forward` forwards a real keyboard's keys as HID OUT reports |

For CDC, bytes the host types are received by the device, and the device can
write bytes, including its own **trace log**, back over the same port:

```
   host terminal ──USB CDC bulk OUT──►  Pico 2
   (screen/minicom)                      │
                     TinyUSB (device) ──► tud_cdc_rx_cb ──► rx ring ──► app
                                                                        │
   host terminal ◄──USB CDC bulk IN───  mu_usb_cdc_task ◄─ tx ring ◄────┘
                                              ▲
                       mu_usb_cdc_log()/logf() (driver self-trace: "[cdc] ...")
```

## Why a shim (pico-sdk-free)

TinyUSB's RP2040/RP2350 device driver (`dcd_rp2040.c`, `rp2040_usb.c`) is
tightly coupled to **pico-sdk**. This project is deliberately pico-sdk-free, so
instead of pulling all of pico-sdk we vendor:

- **TinyUSB portable core** (`tinyusb/`) — device stack (`usbd.c`), the CDC and
  HID classes, common/fifo, and the RP2040 DCD (which also serves the RP2350:
  same USB controller, `DPRAM 0x50100000`, `REGS 0x50110000`, IRQ 14).
- The authoritative **register/struct headers** copied from pico-sdk
  (`pico-shim/hardware/{structs,regs}/usb.h`), plus a thin **platform shim**
  (`pico-shim/`) that maps the ~10 pico-sdk functions the DCD calls onto the
  pico2 BSP:

  | pico-sdk symbol | shim maps to |
  |---|---|
  | `reset_block` / `unreset_block_wait` | RESETS block (`0x40020000`) |
  | `irq_set_enabled` / `irq_add_shared_handler` | NVIC (`USBCTRL_IRQ = 14`) |
  | `time_us_32` | RP2350 TIMER0 (1 MHz) |
  | `busy_wait_at_least_cycles` | nop loop |
  | `critical_section_*` | per-core PRIMASK |
  | atomic register aliases (`hw_set/clear/xor`) | `+0x2000/+0x3000/+0x1000` |

TinyUSB and the shim are carried once, here; only what differs per class is
split into `cdc/` and `hid/`.

## Files

| Path | Role |
|---|---|
| `tinyusb/` | vendored TinyUSB subset (device + CDC and HID classes + rp2040 DCD) |
| `pico-shim/` | pico-sdk hardware headers (copied) + `pico-shim.c` (platform functions, and the shared `USBCTRL_IRQ_Handler`) |
| `cdc/tusb_config.h` | device, full-speed, **CDC only**, RX/TX FIFO 256/512, EP buf 64 |
| `cdc/usb_descriptors.c` | device (IAD/misc) + config (`TUD_CDC_DESCRIPTOR`) + strings (VID `0xCafe`, PID `0x4013`) |
| `cdc/usb_cdc.c`, `cdc/usb_cdc.h` | RX ring (`tud_cdc_rx_cb`), TX ring + `mu_usb_cdc_task()` drain, the self-logging trace (`mu_usb_cdc_log`/`logf`, plus mount/DTR/rx lifecycle logging), and the `mu_usb_cdc_*` API |
| `hid/tusb_config.h` | device, full-speed, **HID only**, EP buf 64 |
| `hid/usb_descriptors.c` | a generic in/out HID report descriptor + config + strings (VID `0xCafe`, PID `0x4012`) |
| `hid/usb_hid.c` | receive glue: `tud_hid_set_report_cb()` pushes the OUT report's bytes into a ring the application drains |

TinyUSB includes `tusb_config.h` by plain name, so the class directory must
come **first** on a test's include path. `board.cmake` names the pieces
(`UOS_BOARD_USB_INCLUDE`, `UOS_BOARD_USB_CORE_SOURCES`, and
`UOS_BOARD_USB_{CDC,HID}_{INCLUDE,SOURCES}`); the board's `tests.cmake` adds
them to the two tests, the class directory first.

The USBCTRL IRQ (14) is wired in the board's `src/interrupt-vectors.S`; the
shim provides `USBCTRL_IRQ_Handler`, with a weak default in `src/boot.S` so
the tests that do not use USB still link.

## CDC API (`cdc/usb_cdc.h`)

| Function | Direction | Notes |
|---|---|---|
| `mu_usb_cdc_getchar()` | host → device | `-1` if empty, else next received byte |
| `mu_usb_cdc_write(buf,len)` / `_write_str(s)` | device → host | queues on the TX ring; safe from any core (PRIMASK-guarded) |
| `mu_usb_cdc_log(msg)` / `mu_usb_cdc_logf(fmt,…)` | device → host | trace line `"[cdc] …\r\n"` on the same port |
| `mu_usb_cdc_task()` | — | drains the TX ring onto the CDC endpoint; call each `tud_task()` loop, on the USB task's core |
| `mu_usb_cdc_mounted()` / `_connected()` | — | bus mounted / host opened port (DTR) |

### Trace logging through the driver

The CDC driver logs its own lifecycle over the port, so the host terminal
shows the trace interleaved with the data:

- `tud_mount_cb` / `tud_umount_cb` → `[cdc] mounted` / `[cdc] unmounted`
- `tud_cdc_line_state_cb` → `[cdc] host connected (DTR=1)` / `disconnected`
- `tud_cdc_rx_cb` → `[cdc] rx N byte(s)`

Because writes go through a ring drained only by `mu_usb_cdc_task()` on the USB
core, logging is safe to call from callbacks and from other threads.

## The USB thread

The device task must run on a µOS++ thread **pinned to core 0** (so the
USBCTRL_IRQ, the class callbacks, and the PRIMASK critical section are all on
one core): `clocks::usb_init(); tusb_init(); for(;;){ tud_task(); … yield(); }`.
Pin it **at construction** (the thread attributes' `th_cpu_affinity`), not by
`cpu_affinity()` afterwards: core 1 is already running and can pick the new
thread first, and `tusb_init()` then enables the IRQ in core 1's NVIC while
`tud_task()` runs on core 0. The device never answers setup (host: descriptor
read -110, address -71).

The RP2350 USB controller needs the **48 MHz USB clock** (`PLL_USB` +
`clk_usb`), which is not part of the default clock tree and not set up by
TinyUSB: hence `clocks::usb_init()` before `tusb_init()`. Without it the
device never enumerates.

## Run

Both tests run on the board only (`-hwd`), through the harness, from
`micro-os-plus-iii-smp.git/tests`:

```sh
xpm run test-smp-test-usb-cdc-acm-hwd --config cortexm-pico2-cmake-gcc-debug
xpm run test-smp-test-usb-hid-hwd     --config cortexm-pico2-cmake-gcc-debug
```

(`cortexm-pico2-pizero-cmake-gcc-debug` for the Pi-Zero board.) Enumeration
needs the board's **own USB port** cabled to the host; the CMSIS-DAP debug
probe used for flashing and the UART does not carry the device.

- **CDC:** open the gadget's new `/dev/ttyACM*` (not necessarily `ttyACM0`: a
  debug probe's UART bridge often is that), e.g. `screen /dev/ttyACM1 115200`.
  What you type is echoed back, with the driver trace, and mirrored on the
  board's UART console; what you type on the UART goes to the host. Sending
  **Escape** ends the test with `RESULT: PASS`.
- **HID:** build the forwarder with `make` in `test/pico2/smp-test-usb-hid/host/`,
  then `sudo ./kbd_forward` (or `sudo ./kbd_forward /dev/input/eventN`). The
  keystrokes appear on the board's UART; **Escape** ends the test with
  `RESULT: PASS`.

## Status

On the **pico2-pizero**, both tests passed on the board (cortexm `3dff821`:
HID before the pin-at-construction change, CDC with it). On the **pico2**,
both passed on the board on 2026-09-26 (CDC enumerated as `/dev/ttyACM1`
beside the Debug Probe's own port). The pico2 HID test still pinned its USB
thread **after** construction then, and a later run the same day failed to
enumerate exactly as described under *The USB thread* (config descriptor
-110, address -62). Since `1e97fbf` it pins at construction like the other
three, and it passed again. The CDC test drives only the MINI-A's
WS2812 on GPIO20, so on a plain Pico 2 (a single LED on GPIO25) nothing blinks;
its `[cdc] led ON …` lines report what it sent to the WS2812.

## The `pico` (RP2040) board

The RP2040's USB controller is the one this DCD was written for, so the
`pico` board uses this directory as it is: TinyUSB, the `cdc/` and `hid/`
files, and the SDK register headers. Only the shim files that carry an
address, a reset bit or an IRQ number are its own, in
`test/boards/pico/usb/pico-shim/`, searched **first**:

| File | RP2040 value |
|---|---|
| `pico.h` | no `PICO_RP2350`; `CFG_TUSB_RP2_ERRATA_E2 1` |
| `pico-shim.c` | TIMER at `0x40054000`; `rp2040_chip_version()` read from `SYSINFO_CHIP_ID` |
| `hardware/irq.h` | `USBCTRL_IRQ = 5` |
| `hardware/resets.h` | `RESET_USBCTRL` = bit 24 |
| `hardware/regs/addressmap.h` | RESETS at `0x4000c000` |

**E2** (B0/B1 silicon: the endpoint ABORT flag is unusable) is on, and
TinyUSB skips the abort when the chip reports revision 1. The board on the
desk reports "RP2040 rev 1" (B0/B1). Without E2, the version hard-coded to
2, one CDC run read the device descriptor, failed the config descriptor, and
the device stopped answering; with E2 and the real version, both tests pass
(one run each, so not proven to be the cause). **E5** (enumeration on a busy bus) is NOT applied:
an SDK-free port of the SDK's `pico_fix` was tried and made enumeration fail
at the first descriptor read (-71), so it was removed. **E15** matters only
behind some hubs.

On the `pico` the CDC test lights the onboard LED (GPIO25) instead of the
WS2812. The descriptors are pico2's, so the gadgets still name themselves
"Pico2 CDC-ACM" / serial `PICO2-0001`. A line containing Escape ends both
tests: in the CDC terminal, press Escape **then Return** (the driver hands
the test whole lines only).

Both tests passed on the `pico` on 2026-10-13 (CDC as `/dev/ttyACM1`, HID
with `kbd_forward`), through the JTAGprobe over SWD at 15 MHz:

```sh
xpm run test-smp-test-usb-cdc-acm-hwd --config cortexm-pico-cmake-gcc-debug
xpm run test-smp-test-usb-hid-hwd     --config cortexm-pico-cmake-gcc-debug
```
