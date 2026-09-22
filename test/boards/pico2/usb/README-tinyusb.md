# usb-cdc — RP2350 USB CDC-ACM (virtual serial) device driver (TinyUSB)

A USB **CDC-ACM** device driver for the Raspberry Pi Pico 2 (RP2350), built on
**vendored TinyUSB** and wired into the µOS++ SMP kernel. The board enumerates
as a virtual serial port (`/dev/ttyACM0`, `COMx`): bytes the host types are
received by the device, and the device can write bytes — including its own
**trace log** — back over the same port.

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

- **TinyUSB portable core** (`tinyusb/`) — device stack (`usbd.c`), CDC class,
  common/fifo, and the RP2040 DCD (which also serves the RP2350: same USB
  controller, `DPRAM 0x50100000`, `REGS 0x50110000`, IRQ 14).
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

This tree is a sibling of `../usb-hid/`; only the class (CDC vs HID), the
descriptors, and the top-level glue driver differ — the TinyUSB core and the
pico-shim are the same.

## Files

| Path | Role |
|---|---|
| `tinyusb/` | vendored TinyUSB subset (device + **CDC class** + rp2040 DCD) |
| `pico-shim/` | pico-sdk hardware headers (copied) + `pico-shim.c` (platform functions) |
| `tusb_config.h` | TinyUSB config: device, full-speed, **CDC only**, RX/TX FIFO 256/512, EP buf 64 |
| `usb_descriptors.c` | device (IAD/misc) + config (`TUD_CDC_DESCRIPTOR`) + strings (VID `0xCafe`, PID `0x4013`) |
| `usb_cdc.c` | RX ring (`tud_cdc_rx_cb`), TX ring + `mu_usb_cdc_task()` drain, and the self-logging trace (`mu_usb_cdc_log`/`logf`, plus mount/DTR/rx lifecycle logging) |
| `usb_cdc.h` | the `mu_usb_cdc_*` application API |

The USBCTRL IRQ (14) is wired in the port's `interrupt-vectors.S`; the shim
provides `USBCTRL_IRQ_Handler` (weak default in `boot.S` so non-USB tests still
link).

## API (`usb_cdc.h`)

| Function | Direction | Notes |
|---|---|---|
| `mu_usb_cdc_getchar()` | host → device | `-1` if empty, else next received byte |
| `mu_usb_cdc_write(buf,len)` / `_write_str(s)` | device → host | queues on the TX ring; safe from any core (PRIMASK-guarded) |
| `mu_usb_cdc_log(msg)` / `mu_usb_cdc_logf(fmt,…)` | device → host | trace line `"[cdc] …\r\n"` on the same port |
| `mu_usb_cdc_task()` | — | drains the TX ring onto the CDC endpoint; call each `tud_task()` loop, on the USB task's core |
| `mu_usb_cdc_mounted()` / `_connected()` | — | bus mounted / host opened port (DTR) |

## Trace logging through the driver

The driver logs its own lifecycle over the CDC port, so the host terminal shows
the trace interleaved with the data:

- `tud_mount_cb` / `tud_umount_cb` → `[cdc] mounted` / `[cdc] unmounted`
- `tud_cdc_line_state_cb` → `[cdc] host connected (DTR=1)` / `disconnected`
- `tud_cdc_rx_cb` → `[cdc] rx N byte(s)`

Because writes go through a ring drained only by `mu_usb_cdc_task()` on the USB
core, logging is safe to call from callbacks and from other threads.

## Integration (see the test's Makefile)

```make
USBCDC_DIR := ../../micro-os-plus-iii-pico2/src/usb-cdc
INCLUDES += -I$(USBCDC_DIR) -I$(USBCDC_DIR)/tinyusb -I$(USBCDC_DIR)/pico-shim
USB_SOURCES := $(USBCDC_DIR)/tinyusb/{tusb.c,common/tusb_fifo.c,device/usbd.c,\
    class/cdc/cdc_device.c,portable/raspberrypi/rp2040/{dcd_rp2040.c,rp2040_usb.c}} \
    $(USBCDC_DIR)/pico-shim/pico-shim.c $(USBCDC_DIR)/usb_descriptors.c $(USBCDC_DIR)/usb_cdc.c
```

The device task must run on a µOS++ thread **pinned to core 0** (so the
USBCTRL_IRQ, the CDC callbacks, and the PRIMASK critical section are all on one
core): `tusb_init(); for(;;){ tud_task(); mu_usb_cdc_task(); yield(); }`.

## Run

1. Build + flash the test: `cd smp-test-usb-cdc-acm && make && ./write_board.sh`
2. Plug the Pico 2's **own** USB port into the host; it enumerates as CDC-ACM
   (`lsusb` shows `Cafe:4013`, and a `/dev/ttyACM0` appears).
3. Open a terminal: `screen /dev/ttyACM0 115200` (or `minicom`/`picocom`/PuTTY).
4. Type — each byte is echoed back, and you see the driver trace (`[cdc] rx 1
   byte(s)`, `[cdc] host connected (DTR=1)`). Each byte is also mirrored on
   UART0 @115200 as `rx: 'A' (0x41)`.

## Status

**Builds; not yet board-verified.** Bring-up note carried over from usb-hid:
the RP2350 USB controller needs the **48 MHz USB clock** (`PLL_USB` + `clk_usb`),
not part of the default clock tree and not set up by TinyUSB. The test calls
`clocks::usb_init()` before `tusb_init()`. Without it the device never
enumerates. Enumeration requires the Pico 2's **own USB port** cabled to the
host — the CMSIS-DAP debug probe (used for flashing/UART) does not carry the
device.
