/**
 * @file kbd_forward.c
 * @brief PC-side keyboard forwarder for the Pico usb-hid driver.
 *
 * Grabs a Linux evdev keyboard, translates each key PRESS/REPEAT to an ASCII
 * character (US layout, with Shift), and sends it to the board's USB-HID
 * device (VID:PID 0xCafe:0x4004) as an HID OUTPUT report on the interrupt-OUT
 * endpoint. The board's tud_hid_set_report_cb() receives it and prints it on
 * UART0.
 *
 * Report format (matches micro-os-plus-iii-pico/src/usb-hid/usb_hid.c):
 *   byte[0] = length, byte[1..] = characters, padded to the endpoint size (64).
 *
 * Usage:
 *   sudo ./kbd_forward                      # auto-pick first keyboard
 *   sudo ./kbd_forward /dev/input/event3    # explicit device
 *
 * Build: make        (needs libusb-1.0 dev package)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <signal.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <linux/input.h>
#include <libusb-1.0/libusb.h>

#define USB_VID 0xCAFE
#define USB_PID 0x4012
#define REPORT_LEN 64
#define TIMEOUT_MS 1000

// ---- US keymap: evdev keycode -> ASCII (base / shifted) --------------------
// Covers the main block (codes 1..57). 0 = no printable char.
static const char kmap_base[64] = {
  /*0*/ 0,   /*1 ESC*/ '\x1b',
  /*2..13*/ '1','2','3','4','5','6','7','8','9','0','-','=',
  /*14 BKSP*/ '\b', /*15 TAB*/ '\t',
  /*16..27*/ 'q','w','e','r','t','y','u','i','o','p','[',']',
  /*28 ENTER*/ '\n', /*29 LCTRL*/ 0,
  /*30..41*/ 'a','s','d','f','g','h','j','k','l',';','\'','`',
  /*42 LSHIFT*/ 0, /*43 \*/ '\\',
  /*44..53*/ 'z','x','c','v','b','n','m',',','.','/',
  /*54 RSHIFT*/ 0, /*55 KP* */ '*', /*56 LALT*/ 0, /*57 SPACE*/ ' ',
  0,0,0,0,0,0
};
static const char kmap_shift[64] = {
  0, '\x1b',
  '!','@','#','$','%','^','&','*','(',')','_','+',
  '\b','\t',
  'Q','W','E','R','T','Y','U','I','O','P','{','}',
  '\n', 0,
  'A','S','D','F','G','H','J','K','L',':','"','~',
  0, '|',
  'Z','X','C','V','B','N','M','<','>','?',
  0, '*', 0, ' ',
  0,0,0,0,0,0
};

static volatile int g_stop = 0;
static void on_sigint (int sig) { (void) sig; g_stop = 1; }

// Find the HID interrupt-OUT endpoint (fallback 0x01 = TinyUSB EPNUM_HID_OUT).
static int
find_int_out (libusb_device_handle* dev, uint8_t* ep_out)
{
  struct libusb_config_descriptor* cfg;
  if (libusb_get_active_config_descriptor (libusb_get_device (dev), &cfg) != 0)
    return -1;
  int found = -1;
  for (int i = 0; i < cfg->bNumInterfaces && found; ++i)
    {
      const struct libusb_interface_descriptor* alt
          = &cfg->interface[i].altsetting[0];
      for (int j = 0; j < alt->bNumEndpoints; ++j)
        {
          const struct libusb_endpoint_descriptor* e = &alt->endpoint[j];
          if ((e->bmAttributes & 0x03) == LIBUSB_TRANSFER_TYPE_INTERRUPT
              && (e->bEndpointAddress & 0x80) == 0)
            {
              *ep_out = e->bEndpointAddress;
              found = 0;
              break;
            }
        }
    }
  libusb_free_config_descriptor (cfg);
  return found;
}

static int
auto_open_keyboard (char* path_out, size_t path_len)
{
  DIR* byid = opendir ("/dev/input/by-id");
  if (byid)
    {
      struct dirent* e;
      while ((e = readdir (byid)) != NULL)
        {
          size_t n = strlen (e->d_name);
          if (n > 10 && strcmp (e->d_name + n - 10, "-event-kbd") == 0
              && strstr (e->d_name, "-if") == NULL)
            {
              char path[512];
              snprintf (path, sizeof path, "/dev/input/by-id/%s", e->d_name);
              int fd = open (path, O_RDONLY);
              if (fd >= 0)
                {
                  char name[128] = "?";
                  ioctl (fd, EVIOCGNAME (sizeof name), name);
                  printf ("Using keyboard: %s (%s)\n", path, name);
                  snprintf (path_out, path_len, "%s", path);
                  closedir (byid);
                  return fd;
                }
            }
        }
      closedir (byid);
    }
  DIR* d = opendir ("/dev/input");
  if (!d)
    return -1;
  struct dirent* e;
  while ((e = readdir (d)) != NULL)
    {
      if (strncmp (e->d_name, "event", 5) != 0)
        continue;
      char path[280];
      snprintf (path, sizeof path, "/dev/input/%s", e->d_name);
      int fd = open (path, O_RDONLY | O_NONBLOCK);
      if (fd < 0)
        continue;
      unsigned long evbits = 0;
      unsigned long keybits[(KEY_MAX + 1) / (8 * sizeof (long)) + 1];
      memset (keybits, 0, sizeof keybits);
      ioctl (fd, EVIOCGBIT (0, sizeof evbits), &evbits);
      ioctl (fd, EVIOCGBIT (EV_KEY, sizeof keybits), keybits);
      if ((evbits & (1ul << EV_KEY))
          && (keybits[KEY_A / (8 * sizeof (long))]
              & (1ul << (KEY_A % (8 * sizeof (long))))))
        {
          char name[128] = "?";
          ioctl (fd, EVIOCGNAME (sizeof name), name);
          printf ("Using keyboard: %s (%s)\n", path, name);
          snprintf (path_out, path_len, "%s", path);
          closedir (d);
          return fd;
        }
      close (fd);
    }
  closedir (d);
  return -1;
}

int
main (int argc, char* argv[])
{
  char kbd_path[512] = "";
  int kfd;

  if (argc > 1)
    {
      snprintf (kbd_path, sizeof kbd_path, "%s", argv[1]);
      kfd = open (kbd_path, O_RDONLY);
      if (kfd < 0) { perror (kbd_path); return 1; }
    }
  else
    {
      kfd = auto_open_keyboard (kbd_path, sizeof kbd_path);
      if (kfd < 0)
        {
          fprintf (stderr, "No keyboard found in /dev/input (need root?)\n");
          return 1;
        }
      close (kfd);
      kfd = open (kbd_path, O_RDONLY);
      if (kfd < 0) { perror (kbd_path); return 1; }
    }

  libusb_context* ctx = NULL;
  if (libusb_init (&ctx) != 0)
    {
      fprintf (stderr, "libusb_init failed\n");
      return 1;
    }
  libusb_device_handle* dev
      = libusb_open_device_with_vid_pid (ctx, USB_VID, USB_PID);
  if (!dev)
    {
      fprintf (stderr,
               "Board not found (%04x:%04x). Is the Pico USB connected "
               "and the firmware running?\n", USB_VID, USB_PID);
      return 1;
    }
  libusb_set_auto_detach_kernel_driver (dev, 1); // kick the kernel hid driver
  if (libusb_claim_interface (dev, 0) != 0)
    {
      fprintf (stderr, "claim_interface failed (try sudo / udev rule)\n");
      return 1;
    }
  uint8_t ep_out = 0x01;
  if (find_int_out (dev, &ep_out) != 0)
    fprintf (stderr, "warning: no interrupt-OUT endpoint found; using 0x01\n");
  printf ("Connected to Pico %04x:%04x (int-OUT ep 0x%02x) — "
          "forwarding keys (Ctrl-C to stop)\n", USB_VID, USB_PID, ep_out);

  struct sigaction sa;
  memset (&sa, 0, sizeof sa);
  sa.sa_handler = on_sigint;
  sigaction (SIGINT, &sa, NULL);

  int shift = 0;
  struct input_event ev;
  while (!g_stop)
    {
      struct pollfd pfd = { .fd = kfd, .events = POLLIN };
      int pr = poll (&pfd, 1, 500);
      if (pr < 0)
        break;
      if (pr == 0)
        continue;
      ssize_t n = read (kfd, &ev, sizeof ev);
      if (n != (ssize_t) sizeof ev)
        break;
      if (ev.type != EV_KEY)
        continue;

      // Track shift state.
      if (ev.code == KEY_LEFTSHIFT || ev.code == KEY_RIGHTSHIFT)
        {
          shift = (ev.value != 0);
          continue;
        }
      // Only send on press (1) and auto-repeat (2), not release (0).
      if (ev.value == 0)
        continue;
      if (ev.code >= 64)
        continue;
      char c = shift ? kmap_shift[ev.code] : kmap_base[ev.code];
      if (c == 0)
        continue;

      uint8_t rep[REPORT_LEN];
      memset (rep, 0, sizeof rep);
      rep[0] = 1;          // length
      rep[1] = (uint8_t) c; // the character
      int sent = 0;
      int rc = libusb_interrupt_transfer (dev, ep_out, rep, REPORT_LEN, &sent,
                                          TIMEOUT_MS);
      if (rc != 0)
        {
          fprintf (stderr, "interrupt_transfer: %s\n", libusb_error_name (rc));
          break;
        }
      printf ("sent '%c' (0x%02x)\r\n",
              (c >= 32 && c < 127) ? c : '.', (unsigned) (uint8_t) c);
    }

  printf ("\nStopping.\n");
  libusb_release_interface (dev, 0);
  libusb_close (dev);
  libusb_exit (ctx);
  close (kfd);
  return 0;
}
