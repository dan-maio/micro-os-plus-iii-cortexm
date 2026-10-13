/*
 * rtos-glue.cpp — glue between the shared micro-os-plus-iii Cortex-M runtime
 * and the RP2040 board.
 *
 * The Pico 2 board's rtos-glue.cpp (test/boards/pico2/glue/rtos-glue.cpp),
 * for the Cortex-M0+:
 *
 *  - Early constructor: bring clocks to 125 MHz, init the UART and give the
 *    RTOS free store the linker heap BEFORE the kernel main() runs. Runs
 *    during __libc_init_array, ahead of the kernel.
 *  - The switch instrumentation and boot breadcrumb the port core writes.
 *  - A HardFault reporter. ARMv6-M has HardFault only (no BusFault,
 *    MemManage or UsageFault, no CFSR/HFSR), so it dumps the stacked frame,
 *    the last context switch and the boot stage, and keeps a record in
 *    .noinit. The Pico has no WS2812, so there is no LED beacon.
 */

#include <bsp/clocks.hpp>
#include <bsp/rp2040.hpp>
#include <bsp/uart.hpp>
#include <cstdio> // snprintf for the retained fault-record summary

// Fault record retained across a warm reset (placed in .noinit, which boot.S
// does NOT zero — see the linker script). fault_report() fills it in; on the
// next boot pico_take_fault_record() formats and clears it.
extern "C"
{
  struct pico_fault_record
  {
    unsigned long magic;      // FAULT_MAGIC when valid
    const char* which;        // "HARDFAULT"
    unsigned long boot_stage; // g_boot_stage breadcrumb at fault time
    unsigned long pc, lr, psr; // from the exception stack frame
    unsigned long count;       // consecutive faulting boots
  };
}
static constexpr unsigned long FAULT_MAGIC = 0xFA017EC0UL;
__attribute__ ((section (".noinit"))) volatile struct pico_fault_record
    g_fault_rec;

// Switch instrumentation, written by the port's switch_stacks() (ISR-safe).
extern "C" volatile void* g_sw_old = nullptr;
extern "C" volatile void* g_sw_new = nullptr;
extern "C" volatile void* g_sw_outsp = nullptr;
extern "C" const char* g_sw_old_name = "none";
extern "C" const char* g_sw_new_name = "none";
extern "C" volatile unsigned long g_sw_count = 0;
extern "C" const char* g_sw_hist[8] = { 0 };

// Boot breadcrumb, set at each bring-up step so fault_report() can show how
// far scheduler start got. Incremented by the port code (extern "C").
extern "C" volatile unsigned g_boot_stage = 0;
extern "C" void
g_boot_mark (unsigned s)
{
  g_boot_stage = s;
}

extern "C"
{
  // Give the RTOS free store the linker heap. Normally the kernel's startup
  // does this; the RP2040 boot.S replaces that startup, so it is done here,
  // before any C++ constructor / the kernel main() allocates (new thread).
  void
  os_startup_initialize_free_store (void* heap_address,
                                    unsigned int heap_size_bytes);
  extern char _Heap_Begin;
  extern char _Heap_Limit;

  // Bring up clocks + UART + the free store before the kernel main().
  __attribute__ ((constructor (101))) static void
  pico_early_init (void)
  {
    clocks::init ();
    uart::init ();
    os_startup_initialize_free_store (
        &_Heap_Begin,
        static_cast<unsigned int> (&_Heap_Limit - &_Heap_Begin));
    uart::write ("[boot " __DATE__ " " __TIME__
                 "] clocks+UART+heap up; entering kernel...\n");
  }

  static void
  hex (const char* k, unsigned long v)
  {
    uart::write (k);
    char b[9];
    b[8] = '\0';
    for (int i = 7; i >= 0; --i)
      {
        unsigned d = (v >> (i * 4)) & 0xF;
        b[7 - i] = static_cast<char> (d < 10 ? '0' + d : 'a' + d - 10);
      }
    uart::write (b, 8);
    uart::write ("\n");
  }

  void
  fault_report (unsigned long* frame, const char* which)
  {
    uart::write ("\n!! ");
    uart::write (which);
    uart::write (" on core ");
    uart::put_char (static_cast<char> ('0' + rp2040::cpu_id ()));
    uart::write ("\n");
    hex ("  boot = 0x", g_boot_stage);
    hex ("  ICSR = 0x", *(volatile unsigned long*) 0xE000ED04);
    {
      unsigned long control, primask;
      __asm__ volatile ("mrs %0, control" : "=r" (control));
      __asm__ volatile ("mrs %0, primask" : "=r" (primask));
      hex ("  CONTROL=0x", control);
      hex ("  PRIMASK=0x", primask);
    }
    hex ("  SP   = 0x", reinterpret_cast<unsigned long> (frame));
    hex ("  VTOR = 0x", *(volatile unsigned long*) 0xE000ED08);
    // Last context switch (from the port's switch_stacks):
    uart::write ("  sw old  = ");
    uart::write (g_sw_old_name);
    hex (" @0x", reinterpret_cast<unsigned long> (g_sw_old));
    uart::write ("  sw new  = ");
    uart::write (g_sw_new_name);
    hex (" @0x", reinterpret_cast<unsigned long> (g_sw_new));
    hex ("  sw outsp= 0x", reinterpret_cast<unsigned long> (g_sw_outsp));
    hex ("  sw count= 0x", g_sw_count);
    for (unsigned i = 0; i < 8; ++i)
      {
        uart::write ("   hist[");
        uart::put_char (static_cast<char> ('0' + i));
        uart::write ("]=");
        uart::write (g_sw_hist[i] ? g_sw_hist[i] : "0");
        uart::write ("\n");
      }
    if (frame)
      {
        hex ("  r0   = 0x", frame[0]);
        hex ("  r1   = 0x", frame[1]);
        hex ("  r2   = 0x", frame[2]);
        hex ("  r3   = 0x", frame[3]);
        hex ("  r12  = 0x", frame[4]);
        hex ("  LR   = 0x", frame[5]);
        hex ("  PC   = 0x", frame[6]); // stacked return address
        hex ("  xPSR = 0x", frame[7]);
      }

    {
      unsigned long prev
          = (g_fault_rec.magic == FAULT_MAGIC) ? g_fault_rec.count : 0UL;
      g_fault_rec.which = which;
      g_fault_rec.boot_stage = g_boot_stage;
      g_fault_rec.pc = frame ? frame[6] : 0UL;
      g_fault_rec.lr = frame ? frame[5] : 0UL;
      g_fault_rec.psr = frame ? frame[7] : 0UL;
      g_fault_rec.count = prev + 1UL;
      g_fault_rec.magic = FAULT_MAGIC;
    }

    for (;;)
      {
        __asm__ volatile ("wfi");
      }
  }

  // If the previous boot faulted, format a one-line summary into buf and
  // clear the record (so it prints once). Returns the string length, or 0 if
  // there is no valid record.
  int
  pico_take_fault_record (char* buf, unsigned max)
  {
    if (g_fault_rec.magic != FAULT_MAGIC)
      return 0;
    int n = snprintf (buf, max,
                      "PREV FAULT: %s boot=0x%lx pc=0x%08lx lr=0x%08lx "
                      "psr=0x%08lx (x%lu)",
                      g_fault_rec.which ? g_fault_rec.which : "?",
                      g_fault_rec.boot_stage, g_fault_rec.pc, g_fault_rec.lr,
                      g_fault_rec.psr, g_fault_rec.count);
    g_fault_rec.magic = 0; // consume it
    if (n < 0)
      return 0;
    if (n > (int) max - 1)
      n = (int) max - 1;
    return n;
  }

  // ARMv6-M: no IT blocks, and an unconditional B reaches only +-2 KB, so
  // pick the stack with a branch and reach fault_report() through a register.
  __attribute__ ((naked)) void
  HardFault_Handler (void)
  {
    __asm__ volatile (
        "movs r0, #4       \n" // EXC_RETURN bit2: 0=MSP, 1=PSP
        "mov r1, lr        \n"
        "tst r0, r1        \n"
        "beq 1f            \n"
        "mrs r0, psp       \n"
        "b 2f              \n"
        "1: mrs r0, msp    \n"
        "2: ldr r1, =3f    \n"
        "ldr r2, =fault_report \n"
        "bx r2             \n"
        ".align 2          \n"
        "3: .asciz \"HARDFAULT\" \n"
        ".align 2          \n");
  }
}
