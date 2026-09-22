# Host-side keyboard forwarder for the Pico 2 usb-hid device.
# Needs the libusb-1.0 development package (e.g. `sudo pacman -S libusb`
# or `sudo apt install libusb-1.0-0-dev`).
CFLAGS  ?= -O2 -Wall -Wextra
kbd_forward: kbd_forward.c
	$(CC) $(CFLAGS) $< -o $@ $(shell pkg-config --cflags --libs libusb-1.0)
clean:
	rm -f kbd_forward
.PHONY: clean
