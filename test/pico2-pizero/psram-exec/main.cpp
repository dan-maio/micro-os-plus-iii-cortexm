/*
 * pizero-psram-exec-main — SRAM supervisor that loads TWO self-contained apps
 * into PSRAM (app A @ 0x11000000, app B @ 0x11400000; see psram_app.cpp) and
 * SWITCHES between them.
 *
 * The SRAM monitor brings up clocks/UART/PSRAM, copies both app images from
 * flash into their PSRAM regions (staged + retry + clean/invalidate — the three
 * board fixes), then runs a switch loop: point VTOR at the active app's vector
 * table, call its entry (vector[1]); the app heartbeats from PSRAM until a key
 * is pressed on the UART, then returns; the supervisor switches to the other
 * app. Each app keeps its own state in its own PSRAM .bss.
 *
 * See ../pizero-psram-exec/PSRAM-CODE-EXEC-FIXES.md for the three board fixes.
 */

#include <bsp/clocks.hpp>
#include <bsp/led.hpp>
#include <bsp/psram.hpp>
#include <bsp/rp2350.hpp>
#include <bsp/uart.hpp>

#include <hw_result.hpp>

#include <cstdint>

// Shared with the PSRAM apps (all compiled into this one ELF): an app sets
// this on its way back. 0 = none, 2 = the host pressed 'x' to finish.
volatile unsigned g_psram_stop = 0;

// This bare test links no libc exit, so it supplies the endpoint hw_result
// ends through over ARM semihosting (Cortex-M BKPT 0xAB).
namespace
{
  [[noreturn]] void
  semi_exit (unsigned reason)
  {
    while (!uos_debug_host_attached ()) // OpenOCD gone: see debug-host.h
      {
      }
    register unsigned r0 __asm__ ("r0") = 0x18u; // SYS_EXIT
    register unsigned r1 __asm__ ("r1") = reason;
    __asm__ volatile ("bkpt 0xAB" : : "r" (r0), "r" (r1) : "memory");
    for (;;)
      {
      }
  }
} // namespace

extern "C" void _exit (int code) __attribute__ ((noreturn));

extern "C" void
_exit (int code)
{
  semi_exit (code == 0 ? 0x20026u : 0x20023u);
}

// RP2350 XIP cache maintenance: 8-bit writes to XIP_MAINTENANCE_BASE +
// byte-offset (from XIP_BASE) + op per 8-byte line. op 3 = CLEAN, 2 = INVALIDATE.
static void
xip_clean_invalidate (std::uint32_t xip_addr, unsigned size)
{
  constexpr std::uint32_t MAINT = 0x18000000U, XIP_BASE = 0x10000000U;
  std::uint32_t off0 = (xip_addr - XIP_BASE) & ~7u;
  std::uint32_t off1 = ((xip_addr - XIP_BASE) + size + 7u) & ~7u;
  __asm__ volatile ("dmb sy" ::: "memory");
  for (std::uint32_t o = off0; o < off1; o += 8u)
    *reinterpret_cast<volatile std::uint8_t*> (MAINT + o + 3u) = 0; // CLEAN
  __asm__ volatile ("dsb sy" ::: "memory");
  for (std::uint32_t o = off0; o < off1; o += 8u)
    *reinterpret_cast<volatile std::uint8_t*> (MAINT + o + 2u) = 0; // INVALIDATE
  __asm__ volatile ("dsb sy" ::: "memory");
  __asm__ volatile ("isb" ::: "memory");
}

static void
puthex (std::uint32_t v)
{
  char b[9];
  for (int i = 7; i >= 0; --i)
    {
      unsigned d = (v >> (i * 4)) & 0xFU;
      b[7 - i] = static_cast<char> (d < 10 ? '0' + d : 'a' + d - 10);
    }
  b[8] = '\0';
  uart::write (b, 8);
}

// Staging buffer (SRAM): flash -> here -> PSRAM, so M0 and M1 never interleave.
static std::uint8_t g_buf[16384];

// Copy one app image flash->PSRAM (staged, retry, clean+invalidate), then zero
// its PSRAM .bss. Returns true on verified copy.
static bool
load_app (const char* name, const char* lma, std::uint8_t* vma, unsigned n,
          char* bss0, char* bss1)
{
  uart::write ("load app ");
  uart::write (name);
  uart::write (": ");
  puthex (n);
  uart::write (" bytes -> 0x");
  puthex (reinterpret_cast<std::uint32_t> (vma));
  uart::write (" ");

  if (n > sizeof (g_buf))
    {
      uart::write ("TOO BIG\n");
      return false;
    }
  for (unsigned i = 0; i < n; ++i)
    {
      g_buf[i] = static_cast<std::uint8_t> (lma[i]);
    }
  volatile std::uint8_t* dst = vma;
  bool ok = false;
  int att = 0;
  for (; att < 16 && !ok; ++att)
    {
      __asm__ volatile ("cpsid i" ::: "memory");
      for (unsigned i = 0; i < n; ++i)
        {
          dst[i] = g_buf[i];
        }
      __asm__ volatile ("dmb sy" ::: "memory");
      __asm__ volatile ("cpsie i" ::: "memory");
      xip_clean_invalidate (reinterpret_cast<std::uint32_t> (vma), n);
      ok = true;
      for (unsigned i = 0; i < n; ++i)
        {
          if (dst[i] != g_buf[i])
            {
              ok = false;
              break;
            }
        }
    }
  if (ok)
    {
      for (char* p = bss0; p < bss1; ++p)
        {
          *p = 0;
        }
      xip_clean_invalidate (reinterpret_cast<std::uint32_t> (bss0),
                            static_cast<unsigned> (bss1 - bss0));
    }
  uart::write (ok ? "OK (" : "FAIL (");
  puthex (static_cast<std::uint32_t> (att));
  uart::write (" attempts)\n");
  return ok;
}

// Run the app whose vector table is at `vma`: point VTOR there and call its
// entry (vector[1]). Returns when the app yields (key pressed).
static void
run_app (std::uint32_t vma)
{
  constexpr std::uint32_t VTOR = 0xE000ED08U;
  rp2350::reg (VTOR) = vma;
  __asm__ volatile ("dsb sy" ::: "memory");
  __asm__ volatile ("isb" ::: "memory");
  void (*entry) (void)
      = reinterpret_cast<void (*) (void)> (
          reinterpret_cast<const volatile std::uint32_t*> (vma)[1]);
  entry ();
}

int
main (void)
{
  clocks::init ();
  uart::init ();
  led::init ();

  uart::write ("\n=== PSRAM dual-app supervisor (Pi-Zero RP2350B) ===\n");

  bool ok = psram::init ();
  uart::write ("psram::init(): ");
  uart::write (ok ? "PASS\n" : "FAIL\n");
  if (!ok)
    {
      for (;;)
        {
          __asm__ volatile ("wfi");
        }
    }

  // Fix 3: M1 SCK at full speed 75 MHz (CLKDIV=2) for the dense instruction fetch.
  rp2350::reg (0x400D0020U)
      = (0x2u << 28) | (1u << 30) | (1u << 25) | (3u << 23)
        | (4u << 17) | (31u << 12) | (2u << 8) | (2u << 0);
  __asm__ volatile ("dmb sy" ::: "memory");

  extern char __psram_a_lma_start[], __psram_a_vma_start[],
      __psram_a_vma_end[], __psram_a_bss_start[], __psram_a_bss_end[];
  extern char __psram_b_lma_start[], __psram_b_vma_start[],
      __psram_b_vma_end[], __psram_b_bss_start[], __psram_b_bss_end[];

  bool a_ok = load_app (
      "A", __psram_a_lma_start,
      reinterpret_cast<std::uint8_t*> (__psram_a_vma_start),
      static_cast<unsigned> (__psram_a_vma_end - __psram_a_vma_start),
      __psram_a_bss_start, __psram_a_bss_end);
  bool b_ok = load_app (
      "B", __psram_b_lma_start,
      reinterpret_cast<std::uint8_t*> (__psram_b_vma_start),
      static_cast<unsigned> (__psram_b_vma_end - __psram_b_vma_start),
      __psram_b_bss_start, __psram_b_bss_end);

  if (!a_ok || !b_ok)
    {
      uart::write ("app load failed -> halting.\n");
      for (;;)
        {
          led::on ();
          for (volatile std::uint32_t i = 0; i < 400000U; ++i) {}
          led::off ();
          for (volatile std::uint32_t i = 0; i < 400000U; ++i) {}
        }
    }

  const std::uint32_t apps[2]
      = { reinterpret_cast<std::uint32_t> (__psram_a_vma_start),
          reinterpret_cast<std::uint32_t> (__psram_b_vma_start) };

  uart::write ("\nBoth apps loaded in PSRAM. Supervisor running.\n");
  uart::write ("Press ESC to switch between APP A and APP B; x to finish.\n");

  int cur = 0;
  for (;;)
    {
      g_psram_stop = 0;
      run_app (apps[cur]); // runs from PSRAM until ESC (switch) or 'x' (end)
      if (g_psram_stop == 2u)
        {
          // The host pressed 'x': both apps were loaded and ran from PSRAM.
          uart::write ("\nRESULT: PASS (both PSRAM apps loaded and ran)\n");
          hw_result::ok ();
        }
      uart::write ("\n=== supervisor: switching apps ===\n");
      cur ^= 1;
    }
}
