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
HID before the pin-at-construction change, CDC with it). On the **pico2**, the
CDC test is built but has not been run on the board (same commit); for the HID
test this repository records no board run.
