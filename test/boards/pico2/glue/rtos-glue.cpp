/*
 * rtos-glue.cpp — glue between the shared micro-os-plus-iii Cortex-M runtime
 * and the RP2350 board (single-core / Phase 2).
 *
 *  - Early constructor: bring clocks to 150 MHz and init UART BEFORE the kernel
 *    main() runs (so the scheduler's SysTick and the console are correct). Runs
 *    during __libc_init_array, ahead of the kernel.
 *  - SysTick_Handler: drives the RTOS tick (the kernel/port ships a weak, empty
 *    one; we override it to call os_systick_handler()).
 */

#include <bsp/clocks.hpp>
#include <bsp/rp2350.hpp>
#include <bsp/uart.hpp>
#include <bsp/ws2812.hpp> // fault beacon on the RP2350B-MINI-A RGB LED
#include <cstdio>         // snprintf for the retained fault-record summary

// Fault record retained across a warm reset (placed in .noinit, which boot.S
// does NOT zero — see the linker script). fault_report() fills it in before the
// LED beacon; on the next boot pico2_take_fault_record() formats and clears it,
// so a fault on a board with no UART/SWD can still be read over USB-CDC. The
// `which` string pointer is stable across warm resets (it lives in flash, same
// image). g_fault_boot counts consecutive faulting boots.
extern "C"
{
  struct pico2_fault_record
  {
    unsigned long magic;      // FAULT_MAGIC when valid
    const char* which;        // "HARDFAULT" / "BUSFAULT" / ...
    unsigned long boot_stage; // g_boot_stage breadcrumb at fault time
    unsigned long cfsr, hfsr, mmfar, bfar;
    unsigned long pc, lr, psr; // from the exception stack frame
    unsigned long count;       // consecutive faulting boots
  };
}
static constexpr unsigned long FAULT_MAGIC = 0xFA017EC0UL;
__attribute__ ((section (".noinit"))) volatile struct pico2_fault_record
    g_fault_rec;

// Switch instrumentation, written by the port's switch_stacks() (ISR-safe).
extern "C" volatile void* g_sw_old = nullptr;
extern "C" volatile void* g_sw_new = nullptr;
extern "C" volatile void* g_sw_outsp = nullptr;
extern "C" const char* g_sw_old_name = "none";
extern "C" const char* g_sw_new_name = "none";
extern "C" volatile unsigned long g_sw_count = 0;
extern "C" const char* g_sw_hist[8] = { 0 };

// Written by the port's context::create(); reported by fault_report().
namespace os
{
  namespace rtos
  {
    namespace port
    {
      volatile void* os_dbg_last_frame = nullptr;
    }
  }
}

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
  void
  os_systick_handler (void); // kernel: src/rtos/os-clocks.cpp

  // Give the RTOS free store the linker heap. Normally the kernel's startup
  // does this; the RP2350 boot.S replaces that startup, so we do it here,
  // before any C++ constructor / the kernel main() allocates (new thread).
  void
  os_startup_initialize_free_store (void* heap_address,
                                    unsigned int heap_size_bytes);
  extern char _Heap_Begin;
  extern char _Heap_Limit;

  // Bring up clocks + UART + the free store before the kernel main().
  __attribute__ ((constructor (101))) static void
  pico2_early_init (void)
  {
    clocks::init ();
    uart::init ();
    os_startup_initialize_free_store (
        &_Heap_Begin,
        static_cast<unsigned int> (&_Heap_Limit - &_Heap_Begin));
    uart::write ("[boot " __DATE__ " " __TIME__ "] clocks+UART+heap up; entering kernel...\n");
  }

  // Fault dumper: print the fault status registers + the stacked return PC so
  // we know WHY the M33 faulted (FP/NOCP, stack-limit STKOF, INVSTATE, ...).
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
    uart::put_char (static_cast<char> ('0' + rp2350::cpu_id ()));
    uart::write ("\n");
    hex ("  boot = 0x", g_boot_stage);
    hex ("  CFSR = 0x", *(volatile unsigned long*) 0xE000ED28);
    hex ("  HFSR = 0x", *(volatile unsigned long*) 0xE000ED2C);
    hex ("  SHCSR= 0x", *(volatile unsigned long*) 0xE000ED24);
    hex ("  ICSR = 0x", *(volatile unsigned long*) 0xE000ED04);
    hex ("  CPACR= 0x", *(volatile unsigned long*) 0xE000ED88);
    hex ("  FPCCR= 0x", *(volatile unsigned long*) 0xE000EF34);
    {
      unsigned long control, primask, basepri;
      __asm__ volatile ("mrs %0, control" : "=r" (control));
      __asm__ volatile ("mrs %0, primask" : "=r" (primask));
      __asm__ volatile ("mrs %0, basepri" : "=r" (basepri));
      hex ("  CONTROL=0x", control);
      hex ("  PRIMASK=0x", primask);
      hex ("  BASEPRI=0x", basepri);
    }
    // Vector table as read from flash (PendSV @0x3C, SysTick @0x40):
    hex ("  vec[3c]=0x", *(volatile unsigned long*) 0x1000003C);
    hex ("  vec[40]=0x", *(volatile unsigned long*) 0x10000040);
    hex ("  vec[04]=0x", *(volatile unsigned long*) 0x10000004);
    hex ("  SP   = 0x", reinterpret_cast<unsigned long> (frame)); // faulting PSP/MSP
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
    // Dump the restored thread's frame (r4-r11,lr then r0-r3,r12,lr,pc,psr).
    if (g_sw_outsp)
      {
        volatile unsigned long* f
            = reinterpret_cast<volatile unsigned long*> (g_sw_outsp);
        for (int i = 0; i < 17; ++i)
          {
            uart::write ("   f[");
            uart::put_char (static_cast<char> (i < 10 ? '0' + i : 'a' + i - 10));
            hex ("]=0x", f[i]);
          }
      }
    if (frame)
      {
        hex ("  r0   = 0x", frame[0]);
        hex ("  r1   = 0x", frame[1]);
        hex ("  r2   = 0x", frame[2]);
        hex ("  r3   = 0x", frame[3]);
        hex ("  r12  = 0x", frame[4]);
        hex ("  LR   = 0x", frame[5]); // stacked return address
        hex ("  PC   = 0x", frame[6]); // stacked return address
        hex ("  xPSR = 0x", frame[7]);
      }

    // Persist the fault into the .noinit slot so the next (good) boot can print
    // it over USB-CDC — the only diagnostic channel on a board with no UART/SWD.
    {
      unsigned long prev
          = (g_fault_rec.magic == FAULT_MAGIC) ? g_fault_rec.count : 0UL;
      g_fault_rec.which = which;
      g_fault_rec.boot_stage = g_boot_stage;
      g_fault_rec.cfsr = *(volatile unsigned long*) 0xE000ED28;
      g_fault_rec.hfsr = *(volatile unsigned long*) 0xE000ED2C;
      g_fault_rec.mmfar = *(volatile unsigned long*) 0xE000ED34;
      g_fault_rec.bfar = *(volatile unsigned long*) 0xE000ED38;
      g_fault_rec.pc = frame ? frame[6] : 0UL;
      g_fault_rec.lr = frame ? frame[5] : 0UL;
      g_fault_rec.psr = frame ? frame[7] : 0UL;
      g_fault_rec.count = prev + 1UL;
      g_fault_rec.magic = FAULT_MAGIC;
    }

    for (;;)
      {
        // Fault beacon on the WS2812 RGB (GPIO20, the RP2350B-MINI-A LED):
        // alternate red/blue so a fault is visible even with no console.
        // Guarded so a fault inside the WS2812 path can't recurse forever.
        static volatile int beacon = 0;
        if (!beacon)
          {
            beacon = 1;
            ws2812::init ();
            for (;;)
              {
                ws2812::set (40, 0, 0);
                for (volatile int d = 0; d < 400000; ++d)
                  ;
                ws2812::set (0, 0, 40);
                for (volatile int d = 0; d < 400000; ++d)
                  ;
              }
          }
      }
  }

  // If the previous boot faulted, format a one-line summary into buf and clear
  // the record (so it prints once). Returns the string length, or 0 if there is
  // no valid record. Call early in os_main and send the result over USB-CDC.
  int
  pico2_take_fault_record (char* buf, unsigned max)
  {
    if (g_fault_rec.magic != FAULT_MAGIC)
      return 0;
    int n = snprintf (
        buf, max,
        "PREV FAULT: %s boot=0x%lx cfsr=0x%08lx hfsr=0x%08lx "
        "bfar=0x%08lx mmfar=0x%08lx pc=0x%08lx lr=0x%08lx (x%lu)",
        g_fault_rec.which ? g_fault_rec.which : "?",
        g_fault_rec.boot_stage, g_fault_rec.cfsr, g_fault_rec.hfsr,
        g_fault_rec.bfar, g_fault_rec.mmfar, g_fault_rec.pc, g_fault_rec.lr,
        g_fault_rec.count);
    g_fault_rec.magic = 0; // consume it
    if (n < 0)
      return 0;
    if (n > (int) max - 1)
      n = (int) max - 1;
    return n;
  }

  __attribute__ ((naked)) void
  BusFault_Handler (void)
  {
    __asm__ volatile (
        "tst lr, #4        \n"
        "ite eq            \n"
        "mrseq r0, msp     \n"
        "mrsne r0, psp     \n"
        "ldr r1, =1f       \n"
        "b fault_report    \n"
        "1: .asciz \"BUSFAULT\" \n"
        ".align 2          \n");
  }
  __attribute__ ((naked)) void
  MemManage_Handler (void)
  {
    __asm__ volatile (
        "tst lr, #4        \n"
        "ite eq            \n"
        "mrseq r0, msp     \n"
        "mrsne r0, psp     \n"
        "ldr r1, =1f       \n"
        "b fault_report    \n"
        "1: .asciz \"MEMMANAGE\" \n"
        ".align 2          \n");
  }

  __attribute__ ((naked)) void
  HardFault_Handler (void)
  {
    __asm__ volatile (
        "tst lr, #4        \n" // EXC_RETURN bit2: 0=MSP, 1=PSP
        "ite eq            \n"
        "mrseq r0, msp     \n"
        "mrsne r0, psp     \n"
        "ldr r1, =1f       \n"
        "b fault_report    \n"
        "1: .asciz \"HARDFAULT\" \n"
        ".align 2          \n");
  }
  __attribute__ ((naked)) void
  UsageFault_Handler (void)
  {
    __asm__ volatile (
        "tst lr, #4        \n"
        "ite eq            \n"
        "mrseq r0, msp     \n"
        "mrsne r0, psp     \n"
        "ldr r1, =1f       \n"
        "b fault_report    \n"
        "1: .asciz \"USAGEFAULT\" \n"
        ".align 2          \n");
  }
}
