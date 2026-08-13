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
  command succeeded. [cmake is case sensitive and file name didnt match
   exactly] Suspect a stale cache from a configure run made before the
  linker script was added.
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

## 2026-08-13 — Clock tree bring-up: HSI → PLL → 100 MHz SYSCLK

**Goal:** Configure SYSCLK to 100 MHz from the internal HSI via the main PLL,
with correct flash latency and bus prescalers. Written directly against RM0383
rather than using CubeMX-generated init.

**Final config (verified from a cold reset, no debugger intervention):**
- HSI 16 MHz → PLLM=8 → 2 MHz VCO input (recommended value per §6.3.2)
- PLLN=100 → 200 MHz VCO output → PLLP=÷2 → **100 MHz SYSCLK**
- AHB ÷1 = 100 MHz, APB1 ÷2 = 50 MHz, APB2 ÷1 = 100 MHz
- Flash: 3 wait states, prefetch + I-cache + D-cache enabled
- `RCC->PLLCFGR` reads `0x24001908`; `SystemCoreClock` reads `100000000`

---

### Bug 1: hang polling PWR_CSR_VOSRDY

Code spun forever on `while ((PWR->CSR & PWR_CSR_VOSRDY) == 0)`. Classified as
a hang, not a fault — GDB halted with `$pc` inside the loop at a valid address,
no HardFault.

Read state before theorizing:
- `RCC->APB1ENR = 0x10000000` → PWREN set, peripheral is clocked
- `PWR->CR = 0xc000` → VOS = 0b11 (Scale 1), write landed
- `PWR->CSR = 0x0` → VOSRDY clear

The first two ruled out my initial suspicion (an RCC clock-enable propagation
delay eating the `PWR->CR` write) — if that were happening, `PWR->CR` would not
have read back my value.

Tested the remaining hypothesis by setting PLLON by hand from the debugger
rather than rebuilding:

    (gdb) set var RCC->CR = RCC->CR | 0x01000000
    (gdb) p/x PWR->CSR
    $9 = 0x4000

VOSRDY asserted immediately. **Fix:** moved the VOSRDY poll to after PLLON /
PLLRDY.

*Caveat: this is an empirical result on one board. I have not yet confirmed in
RM0383 §5.4.2 that the VOSRDY-after-PLLON dependency is a documented hardware
guarantee. TODO before I rely on it.*

---

### Bug 2: PLLCFGR writes silently rejected

After clearing bug 1, SYSCLK came up but `SystemCoreClock` read 96000000, not
the 100 MHz I configured. `RCC->PLLCFGR` read `0x24003010`, which is the
**reset value**: PLLM=16, PLLN=192, PLLP=÷2 → 16/16 × 192 / 2 = 96 MHz. None of
my writes had taken.

Cause: PLLCFGR is only writable while the PLL is off. Because I had turned
PLLON on manually while debugging bug 1, the subsequent PLLCFGR writes in my
code were rejected — with no fault and no error flag. The code appeared to work
and the LED blinked faster, which is exactly why "it blinks" is not verification.

**Fix:** none needed in source — the correct ordering (configure PLLCFGR, then
PLLON) was already there. The rejection was an artifact of my debugger poke.
Confirmed by rebuilding and running from a cold reset.

Worth noting I also got lucky: 3 wait states happens to be correct for 96 MHz
as well as 100 MHz, so the accidental frequency didn't produce a flash access
fault that would have surfaced the problem sooner.

---

### Notes / decisions

- **`SystemCoreClockUpdate()` over hardcoding `SYSCLK_FREQ`.** This is what
  caught bug 2. It re-derives the frequency from PLLCFGR/CFGR, so it reported
  96 MHz truthfully. A hardcoded constant would have claimed 100 MHz on a chip
  running at 96 and every downstream timing calculation would have been 4% off
  with nothing to indicate it.
- **VOS write is arguably redundant.** `PWR_CR` reset value on F411 is `0xc000`
  — already Scale 1. Kept the explicit write anyway: F401 uses a different VOS
  encoding, and I'd rather state intent than depend on a reset value.
- **PLLQ left at reset value 4** → 50 MHz on the Q output. Irrelevant now (no
  USB/SDIO). If USB is ever added, note that a 200 MHz VCO has no integer
  divisor giving 48 MHz, so the whole PLL config would need reworking.
- **APB1 at exactly 50 MHz, APB2 at exactly 100 MHz** — both at the datasheet
  maximum with zero headroom. Deliberate, but verify against DS10314.
- **Unbounded `while` polls on HSIRDY / PLLRDY / VOSRDY / SWS.** Any clock
  failure hangs silently with no output — the worst failure mode to diagnose.
  Fine on a Nucleo where HSI always comes up; should be bounded retries with a
  fault path before this goes anywhere real.
- **`__NOP()` after the APB1ENR write was removed.** Added as a guard against
  the clock-enable delay erratum, but my own register reads had already
  disproved that hypothesis, and a single `nop` isn't the documented workaround
  anyway (that's a read-back of the enable register). Removed rather than left
  in with a comment describing a mechanism I hadn't actually observed.

---

### Open / next

- [ ] Confirm VOSRDY/PLLON dependency in RM0383 §5.4.2
- [ ] Verify APB1/APB2 ceilings against DS10314
- [ ] **Route SYSCLK to MCO1 (PA8) and measure with the FX2LP.** Every number
      above is a register I wrote or a value derived from one — no independent
      measurement yet. Needed before FreeRTOS, since `configCPU_CLOCK_HZ`
      inherits this and a wrong clock there produces timing bugs that look like
      scheduler bugs. Will need MCO1PRE prescaling to get under the analyzer's
      sample rate.

### GDB technique worth keeping

- Read state *before* forming a hypothesis. Three `p/x` calls killed my leading
  theory in under a minute.
- `set var` on a peripheral register tests an ordering hypothesis without a
  build-flash cycle. This is what actually found bug 1.
- `monitor reset` leaves the core *running* → `continue` gives "target not
  halted". Use `monitor reset halt`.
- `step` on a polling loop single-steps forever. `next` steps over,
  `advance <line>` runs to a line without a permanent breakpoint, `jump <line>`
  skips code entirely (only ever within one function — it moves `$pc` and
  nothing else).
