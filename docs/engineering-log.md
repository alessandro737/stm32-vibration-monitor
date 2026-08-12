# Engineering Log

Running notes on design decisions, failures, and their resolutions.
Written as work happened, not reconstructed afterward.

---

## 2026-08-06 — Toolchain and repository setup

**Goal.** Get a working cross-compilation and debug environment before
hardware arrives, so that week 1 is spent on SPI rather than on tooling.

**Done.**
- WSL2 (Ubuntu 24.04) as the development environment. usbipd-win
  forwards the ST-LINK; PulseView stays native on Windows to avoid
  forwarding a second USB device through WSLg.
- Installed the Arm GNU Toolchain from Arm's tarball rather than
  `apt install gcc-arm-none-eabi` — the Ubuntu package has a history of
  shipping a broken newlib-nano, which surfaces as link errors that
  look like source bugs.
- OpenOCD, CMake, Ninja, picocom from apt.
- Repository initialized. FreeRTOS-Kernel and CMSIS_5 added as
  submodules so the exact revisions built against are visible.

**What surprised me.** WSL2 doesn't expose USB devices at all by
default — I'd assumed it behaved like a VM with passthrough. Everything
downstream (OpenOCD, the ST-LINK's virtual COM port) depends on
`usbipd attach` succeeding first.

**Open questions.**
- Whether `arm-none-eabi-gdb` from the tarball runs cleanly on 24.04, or
  whether `gdb-multiarch` is needed as a fallback. Untestable until the
  board arrives.
- udev rules unverified for the same reason.

**Next.** Write the CMake toolchain file and get a blinky to *link*.
Can't run it, but `arm-none-eabi-size` and `objdump -h` will confirm
the linker script and startup file agree before hardware shows up.

---

## 2026-08-12 - First code on hardware: GPIO blinky over SWD

**Goal.** Get the board connected, flash a program, and verify the whole
debug path end to end. Blinky is the vehicle; the real deliverable is
knowing that OpenOCD, GDB, and the flash path all work before there is
any nontrivial code to debug.

**Done.**
- Soldered headers onto the W25Q64 flash module. ADXL345 came
  pre-soldered.
- ST-LINK forwarded into WSL2 with `usbipd attach --wsl --busid [X-X]`.
  Enumerates as `0483:374b`. udev rules were already in place from the
  apt `openocd` package — no manual rules file needed.
- OpenOCD 0.11.0 connects over `hla_swd`. Reports target voltage 3.24 V
  and **6 hardware breakpoints, 4 hardware watchpoints** — that is the
  real budget, worth remembering before I try to set a seventh.
- Wrote `src/main.c`: RCC clock enable for GPIOA, PA5 as push-pull
  output, toggle via BSRR with busy-wait delays.
- Flashed over SWD from GDB (`load`), LED blinking.
- Added `.gdbinit` to connect, halt, and load in one step.

**Design decision: BSRR over ODR.** `ODR |= (1 << 5)` is a
read-modify-write — three instructions, interruptible between any two.
BSRR is write-only and ignores zero bits, so a set or clear is a single
atomic store with no need to know the state of other pins. Irrelevant
for single-threaded blinky, but in week 2 the ISR marker pins and
`acq_task` will both write to the same port, and a race there would
corrupt the latency measurements I intend to publish. Chose the habit
that transfers.

**Bugs found and fixed.**
- `BSSR` instead of `BSRR` in four places. Caught at compile time.
- `GPIOA->BSRR |= ...` — reached for `|=` reflexively on a **write-only**
  register. Reading BSRR returns undefined data, so the OR would have
  written garbage back. This is the exact mistake BSRR exists to
  prevent. Same reflex will bite on `EXTI->PR` in week 2, which is
  write-1-to-clear: `|=` there would silently clear pending interrupts I
  never handled.
- `GPIO_MODER_MODER5_1` instead of `MODER5_0`. Misread the CMSIS naming
  convention — the trailing digit is a **bit position within the 2-bit
  field**, not a value. `MODER5_0` = register bit 10, `MODER5_1` = bit
  11. Output mode is field value `01`, so `MODER5_0` alone. Setting both
  would have selected analog mode.

**Verified in GDB.** Walked the causal chain rather than assuming:
`RCC->AHB1ENR` bit 0 set after the clock enable → `GPIOA->MODER` field 5
reads `01` (0xA8000400) → `GPIOA->ODR` bit 5 toggling. Each link
confirmed before moving to the next. This narrated top-down approach is
the thing to practice, not the individual commands.

**What surprised me.**
- **The reset stack pointer changed after `load`.** First `monitor reset
  halt` showed `msp: 0x20000560` — ST's factory demo program, still in
  flash. After flashing mine, reset gave `msp: 0x20020000`, which is
  `0x20000000 + 128K` = `_estack`. That is the CPU reading the first word
  of `.isr_vector` directly into SP. The reset sequence and my linker
  script, confirmed against each other on real silicon.
- **`GPIOA->MODER` resets to `0xA8000000`, not zero.** PA13, PA14, and
  PA15 come out of reset configured as alternate function because they
  are SWDIO, SWCLK, and JTDI. Reconfiguring those as GPIO would
  permanently disconnect the debugger until a full chip erase. Answers a
  question I had been carrying about why the reset value looked wrong.
- **A watchpoint on a for-loop variable failed** with "No symbol i in
  current context" — the variable is scoped to the loop block and I was
  stopped at the top of `main`, where it does not exist yet. Debug info
  only names what is live at the current PC. Obvious in retrospect, but
  it clarified that GDB is reading DWARF scope records, not a flat
  symbol table.
- **`RAM: 1568 B` in the link output is not real usage.** It is
  `_Min_Heap_Size` (512) + `_Min_Stack_Size` (1024) + ~32 bytes of
  actual variables. Those two are link-time assertions that fail the
  build if RAM is over-committed, not runtime allocations.

**Open questions.**
- The first `cmake` configure failed with "cannot open linker script
  file" for a path that existed. A second configure with the identical
  command succeeded. [Note here what, if anything, actually changed —
  or state plainly that it is unexplained.] Suspect a stale cache from
  a configure run made before the linker script was added.
- The `_close` / `_lseek` / `_read` / `_write` "not implemented and will
  always fail" warnings are `nosys.specs` stubs and the linker notes
  they may be garbage-collected. Should confirm from the `.map` file
  whether they survive into the binary.
- OpenOCD 0.11.0 (2021) is old — hence the `hla_swd` transport, the
  cosmetic `flash size = 512 bytes` line, and the speed-match noise.
  Works, so not churning now, but a newer build has the `st-link`
  adapter driver and better SWO support, which will matter for real
  latency tracing.

**Next.** SysTick before the PLL. It gives me a first ISR, a real
`delay_ms()` instead of a magic loop count, and a millisecond timebase
for every measurement from here on — and it is FreeRTOS's tick source,
so the mechanism carries into week 2. Then `clock.c`: flash latency
raised **before** the clock, HSE from the ST-LINK's 8 MHz MCO into the
PLL for 100 MHz, then AHB/APB prescalers. The SysTick reload value will
have to change, which is the point. Blink rate changing is free visual
confirmation the PLL locked.
