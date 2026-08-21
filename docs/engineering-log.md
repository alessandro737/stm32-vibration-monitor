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

  ## 2026-08-13 (cont.) — SPI1 bring-up, ADXL345 DEVID read

**Goal:** First working SPI transaction. Read ADXL345 DEVID (0x00),
expect 0xE5. One transaction validates CPOL, CPHA, bit order, AF
mapping, prescaler and CS timing simultaneously — chosen deliberately
as the smallest thing that proves the whole config.

**Result:** 0xE5. Confirmed in GDB.

---

### Config decisions

**Mode 3 (CPOL=1, CPHA=1).** Stated directly in the ADXL345 datasheet.
CPOL=1 means SCLK idles high; CPHA=1 means the slave drives on the first
edge and the master latches on the second (rising), which is what gives
the slave setup time. A CPHA error would shift the received byte by one
bit position and often decode as a plausible-looking value rather than
obvious garbage.

**BR=0b111 → ~390 kHz** from APB2 at 100 MHz. Two separate ceilings
apply and they have different failure modes:
- ADXL345 max SCLK is 5 MHz — a *hardware* limit; exceed it and the data
  is genuinely corrupt.
- FX2LP analyzer at 24 MSa/s shared across channels — an *instrument*
  limit; exceed ~1/10 of the sample rate and the data is fine but the
  capture aliases and lies. Rule of thumb adopted: sample rate ≥ 10× SCLK.

Only the first still applies once the analyzer is disconnected.

**Software CS on PA4, not hardware NSS.** NSS in master mode is not a
chip select — it is a multi-master arbitration input. If it reads low the
peripheral assumes another master claimed the bus, sets MODF, and clears
MSTR and SPE, silently dropping out of master mode. SSM=1/SSI=1 makes the
peripheral ignore the physical pin and treat NSS as internally high.

The SSOE hardware-output alternative pulls NSS low while SPE is set and
holds it there — it does not frame transactions. The ADXL345 requires CS
to deassert between nonsequential register accesses, which SSOE cannot
express.

**DFF=0 (8-bit).** The ADXL345's control-byte-plus-data structure looks
16-bit, but DFF sets the width of a DR access, not the frame boundary —
the frame is defined by CS, which is a GPIO. A burst read of X/Y/Z is
1 control byte + 6 data bytes = 7 bytes, which is odd and cannot be
expressed in 16-bit accesses. Byte-at-a-time handles both the 1-byte and
6-byte cases with the same code path.

**CRC off, BIDIMODE=0** (full duplex, 4-wire — the ADXL345's default;
the SPI bit in DATA_FORMAT 0x31 is left at reset).

---

### The full-duplex drain

The non-obvious part of the transfer sequence. Every byte clocked out
clocks a byte in, so a read is:

    CS low
    wait TXE -> write control byte -> wait RXNE -> read DR, DISCARD
    wait TXE -> write dummy        -> wait RXNE -> read DR, KEEP
    wait !BSY
    CS high

Skipping the discard leaves RXNE already set when the second read runs,
so it returns the garbage byte clocked in during the address phase — and
every subsequent transaction stays permanently off by one, returning the
previous transaction's value. The wire traffic looks correct throughout,
which makes this present as a hardware problem when it is not.

Also note TXE goes *before* each write, not after — TXE asserts when the
byte moves from DR into the shift register, not when it reaches the wire.
BSY is the flag that means "transmission in progress"; polling it before
dropping CS is what prevents truncating the last bit.

---

### Bug: disconnected CS

DEVID read returned 0x00. State at the breakpoint:
- `SPI1->SR = 0x2` → TXE set, RXNE clear
- `SPI1->DR = 0x0`

TXE set means the peripheral was transmitting normally, so the master
side was working. RXNE clear with nothing on MISO points at the slave
never responding — a power, ground, or wiring fault rather than a config
error. The CS jumper was not connected, so the ADXL345 was never enabled
and never drove SDO.

Reconnected it; DEVID read 0xE5 immediately.

**Discipline note:** this is the failure mode where the register evidence
distinguishes "master not transmitting" from "slave not responding."
Those are indistinguishable from the returned value alone.

---

### GDB hazard (second instance)

`p/x SPI1->DR` from the debugger *clears RXNE* — reading DR is not a
passive observation. Same class of problem as yesterday's PLLON poke:
touching a peripheral register from GDB changes hardware state. Prefer
inspecting the C variable (`p/x response`), which is a plain memory read
with no side effects.

Worth generalizing: debugger reads of peripheral registers can have the
same side effects as reads from code, including clearing flags and
popping FIFOs.

---

### CMSIS naming note

`GPIOA->AFRL` does not exist. RM0383 documents AFRL and AFRH separately;
CMSIS collapses them into `AFR[2]`, so pins 0–7 are `AFR[0]`. The bit
macros keep the manual's naming (`GPIO_AFRL_AFSEL5_Pos`) even though the
struct field does not — and are AFSEL, not AFRL, in current headers.

Technique: grep the device header rather than guessing at macro names.

---

### Open / next

- [ ] **Logic analyzer capture of the DEVID transaction.** Doing this
      while the code is known-good is deliberate: if the capture
      disagrees with the prediction (CS low, 16 clocks, MOSI 0x80 0x00,
      MISO junk then 0xE5) then the analyzer setup is wrong, not the
      firmware. Learning the instrument on a known signal beats learning
      it mid-bug.
- [ ] Move to breadboard — current jumper-to-jumper wiring leaves no room
      to attach probes. Note breadboard parasitics will matter if SCLK is
      later raised toward 5 MHz.
- [ ] Factor into `bsp/spi.c` (`spi_transfer(tx, rx, len)`) and
      `drivers/adxl345.c` (`adxl_read_reg` on top). The MB=1 burst read
      of 0x32–0x37 is the target — six separate reads would let a new
      sample land mid-sequence and return axes from different points in
      time.
- [ ] Clock verification still open. FX2LP cannot capture MCO1's 20 MHz
      floor from a 100 MHz PLL — needs a scope. Check available scope
      bandwidth.

      ## 2026-08-13 — SPI1 bring-up: ADXL345 DEVID read verified

### Result
SPI1 master, Mode 3 (CPOL=1/CPHA=1), BR=÷256 → 390.6 kHz off 100 MHz APB2,
software NSS on PA4. DEVID (0x00) reads 0xE5, BW_RATE (0x2C) reads 0x0A
(datasheet reset value). Both confirmed on the wire with a logic analyzer
and in the register.

### Instrumentation
FX2LP clone + PulseView/fx2lafw on Windows. Zadig/WinUSB needed on two
distinct USB IDs: 04B4:8613 (bare Cypress) and 1D50:608C (post-firmware-
upload re-enumeration). Initial failures were a wrong driver selected in
PulseView's device dropdown, not a driver-binding problem.
Capture: 4 MSa/s, 1 M samples, falling-edge trigger on CS, D0=SCK D1=MOSI
D2=CS D3=MISO.

### Findings

1. **ADXL345 MISO carries the previous transaction's output during the
   address phase.** During clocks 1–8 the part cannot know the requested
   register, and drives the prior transfer's last byte. Confirmed
   unambiguously: MISO read 0xE5 while 0xAC (read BW_RATE) was being
   clocked in, and 0x0A while 0x80 (read DEVID) was clocked in.
   Implication: the driver must return byte 2. Returning byte 1 passes
   any test where consecutive reads happen to match.

2. **CS deassert was too short to sample.** Loop overhead between
   CS_HIGH() and the next CS_LOW() was ~50–100 ns; sample period at
   4 MSa/s is 250 ns. The decoder merged two transactions into one
   window (80 00 B1 00). Not a decoder fault — an aliasing artifact.
   Also marginal for the part's own state-machine reset. Added delay.

3. **Intermittent single-bit read errors were an observation artifact.**
   Saw DEVID as 0xE4 and BW_RATE as 0x0B intermittently under GDB, always
   a bit-0 error. Hypothesised a stuck LSB (0xE5 has bit0=1 so the fault
   would be invisible there; 0x0A has bit0=0 so it would show) — plausible
   but wrong. A simultaneous capture showed the wire carrying 0xE5 while
   GDB reported 0xE4 in the same transaction, relocating the fault to the
   observation path. Logging 256 back-to-back reads to a .bss buffer and
   dumping after the fact: **0 errors in 256**. Exact mechanism in the
   halt/SWD-read path not isolated; not pursued further.

### Method notes
- `p/x SPI1->DR` pops the RX buffer and clears RXNE — it changes the state
  it reports. Read program variables, not peripheral data registers.
  Likewise SR-then-DR is the documented OVR clear sequence.
- `volatile` stack locals were misreported by GDB at -Os; moving results
  to file-scope statics (.bss, fixed address) resolved the DEVID mismatch.
- A breakpoint set on a `for(volatile ...)` delay loop lands on the
  increment and fires every iteration. `disassemble` confirmed
  0x800028a = `adds r3, #1`.
- Replacing single-shot breakpoint sampling with batch logging turned
  "sometimes wrong" into a measurable rate, which is what settled it.

### Open / next
- Second confirmation run pending: free-run dump vs. a run with
  breakpoints inside the loop, to check errors appear only in stopped
  iterations.
- SPI write path (POWER_CTL, DATA_FORMAT, BW_RATE) not yet implemented.
- Third instance in this project of observation perturbing the system
  (cf. PLLON poke during clock bring-up leaving the PLL running).

## 2026-08-17 — SPI1 receive path: bit 0 carries previous byte (characterized, not root-caused)

### Symptom

Register reads from the ADXL345 returned values whose bit 0 was wrong, while
bits 7:1 were always correct. Presented initially as "first read after init is
wrong, second read is correct" — which was misleading. Reading the same
register twice masks the defect, because the second read inherits a bit 0 that
happens to be correct.

Analyzer captures showed the correct value on MISO in every case. The
divergence is between the pin and the value in RAM.

### Model

Bit 0 of every received byte equals bit 0 of the **previous byte on the wire**.
Bits 7:1 are always correct.

Scored four competing models on-target across three independent sequences with
non-alternating LSB orderings (out of 7):

| model | seq A (write-free) | seq B (single) | seq C (burst) |
|---|---|---|---|
| clean | 3 | 4 | 4 |
| **bit0 held from previous byte** | **7** | **7** | **7** |
| bit0 inverted in place | 4 | 3 | 3 |
| bit0 := bit1 | 4 | 3 | 3 |

Sequence A read only DEVID (0x00 → 0xE5) and BW_RATE (0x2C → 0x0A), both
untouched reset values, so no write occurs. It still showed the defect. The
write path is exonerated; this is read-path only. MOSI carried 0x80 correctly
on the analyzer, consistent with that.

An earlier pattern choice alternated bit 0 across bytes (1,0,1,0,...), under
which "held from previous" and "inverted in place" produce identical output.
That test could not discriminate and was rerun with independent bit 0 / bit 1
variation.

### Eliminated, with evidence

| hypothesis | how it died |
|---|---|
| Stale RX byte / overrun after SPE | `SPI1->SR` read 0x0002 (TXE set, RXNE clear, no OVR, not BSY) immediately before every transaction, all trials |
| First-transaction-after-init effect | Probes 0 and 1 of the 7-probe run were correct; probe 4, well clear of init, was wrong |
| tCS,DIS violation (250 ns min, ADXL345 DS) / CS framing | Burst read of 8 bytes in a **single** CS assertion showed identical corruption on every byte — no CS transitions involved |
| Baud-rate timing race | Byte-identical results at BR=7/5/3 → ~390 kHz, 1.56 MHz, 6.25 MHz. 16× span, zero change |
| Lost SCK edge / bit-count error | Analyzer: 8 SCK pulses per byte, 16 per register read. Also ruled out by bits 7:1 always being correct — a shift would corrupt the upper bits |
| DR access-width (byte-lane vs halfword) | `strb`/`ldrb` and word-read-plus-`uxtb` produced bit-identical results |
| SPI misconfiguration | `CR1 = 0x37F` verified by readback: CPHA=1, CPOL=1, MSTR=1, BR=111, SPE=1, LSBFIRST=0, SSI=1, SSM=1, DFF=0, RXONLY=0, BIDIMODE=0. `CR2 = 0x0000` (no TI frame format, no DMA) |

### Workaround

The model is predictive, so it is also correctable. Clock one extra dummy byte
after the data byte; its bit 0 is the data byte's true bit 0:

```c
v    = spi1_transfer(0x00);   /* bits 7:1 valid, bit 0 stale */
tail = spi1_transfer(0x00);   /* bit 0 == v's true bit 0     */
return (uint8_t)((v & 0xFE) | (tail & 0x01));
```

Verified: `logA` = `E5 E5 0A 0A E5 0A E5 E5`, clean-model score 7/7. Seeded
scratch block (0x1D–0x24) read back byte-exact: `85 8B 8D 92 94 9A 9F A0`.

**Cost:** 3 transfers per register read instead of 2 — 24 SCK cycles instead of
16, 50% more bus time. Relevant against the 10 ms acquisition deadline.

### Open

- **Mechanism unknown.** A validated corrective model is not a root cause. What
  survives is something in the receive path holding the final sampled bit;
  there is no explanation for a shift register that shifts 7 stages and holds
  the 8th.
- **`adxl345_read_multiple_registers` is still uncorrected** and is the path
  acceleration data will use. Every byte in a burst is simultaneously data and
  its successor's predecessor, so the fix is to read N+1 bytes and reassemble —
  byte *i*'s true bit 0 arrives with byte *i+1*. Note this corrupts bit 8 of
  each 16-bit sample (DATAX1 etc.), a 256-LSB error, not a rounding nuisance.
- Next diagnostic if revisited: compare against a second ADXL345 or a different
  SPI slave to separate MCU-side from board/sensor-side.

### Method notes

- **Let the target compute the verdict.** GDB's report and the CPU's value are
  different observation sites. Firmware scored the models itself and exported
  single-byte answers, which removed the debugger from the measurement.
- **Poison initializers.** Statics initialized to `0xAA`/`0xFFFF` instead of
  zero, so "genuine zero" and "never written" are distinguishable. Side benefit:
  confirmed startup copies `.data` from flash to RAM.
- **MCU reset does not reset the ADXL345.** The 3.3V rail stays up through
  `monitor reset halt`, so the sensor retains state from the previous run. Cold
  sensor trials require a USB power cycle. Cold and warm trials must be labeled.
- **Identical `.text` size is not proof of an identical binary.** A byte-lane to
  word-read change was absorbed by existing alignment padding. Verify with
  `objdump -d`, not section sizes.
- **Don't halt inside a transaction.** Single-stepping holds CS asserted for
  however long you take, which invalidates the capture.

### Artifacts

- `docs/captures/spi_devid_pair.png` — two consecutive DEVID reads. MOSI `80 00`
  both times; MISO `00 E5` then `E5 E5`. The address-phase byte differs (0x00,
  then 0xE5) because the sensor still drives the previous value. RAM held 0xE4
  then 0xE5 — the delta is fully accounted for by the preceding byte's bit 0.
- `docs/captures/spi_clock_count.png` — 8 SCK pulses per byte confirmed.

### Decision

Characterization is bounded and a validated workaround is in place. Stopping
here and moving to FreeRTOS integration, which is the critical path for the
week. Revisit if there is slack.

## 2026-08-17 — SPI1: bit 0 corruption root-caused to SCK output slew rate

### Symptom

ADXL345 register reads returned values whose bit 0 was wrong; bits 7:1 were
always correct. Initially presented as "first read after init is wrong, second
read is correct," which was misleading — reading the same register twice masks
the defect, because the second read inherits a bit 0 that happens to be right.

Logic analyzer captures showed the correct value on MISO in every case. The
divergence was between the pin and the value in RAM.

### Model

Bit 0 of every received byte equals bit 0 of the **previous byte on the wire**.
Bits 7:1 always correct.

Four models scored on-target across three sequences with non-alternating,
independently-varying bit0/bit1 patterns (out of 7):

| model | seq A (write-free) | seq B (single) | seq C (burst) |
|---|---|---|---|
| clean | 3 | 4 | 4 |
| **bit0 held from previous byte** | **7** | **7** | **7** |
| bit0 inverted in place | 4 | 3 | 3 |
| bit0 := bit1 | 4 | 3 | 3 |

Sequence A read only DEVID (0xE5) and BW_RATE (0x0A) — untouched reset values,
no write involved — and still showed the defect, exonerating the write path.
An earlier pattern alternated bit 0 across bytes, under which "held from
previous" and "inverted in place" are indistinguishable; that test could not
discriminate and was rerun.

### Eliminated, with evidence

| hypothesis | how it died |
|---|---|
| Stale RX / overrun after SPE | `SR = 0x0002` (TXE set, RXNE clear, no OVR, not BSY) before every transaction, all trials |
| First-transaction-after-init effect | Probes 0 and 1 correct; probe 4, well clear of init, wrong |
| tCS,DIS violation / CS framing | 8-byte burst in a **single** CS assertion corrupted on every byte |
| Baud-rate timing race | Byte-identical at BR=7/5/3 (~390 kHz, 1.56 MHz, 6.25 MHz) |
| Lost SCK edge / bit-count error | Analyzer: 8 pulses/byte, 16/transaction. Also excluded by bits 7:1 always being correct |
| DR access width | `strb`/`ldrb` and word-read-plus-`uxtb` gave bit-identical results |
| TXE/RXNE pipeline overlap | Full BSY wait between transactions changed nothing |
| SPI misconfiguration | `CR1 = 0x37F` by readback (CPHA/CPOL/MSTR/SPE/SSI/SSM set, BR=111, DFF=0, LSBFIRST=0, RXONLY=0, BIDIMODE=0); `CR2 = 0x0000` |

### Root cause

**`GPIOA->OSPEEDR` was left at its reset value of `00` (lowest slew rate) for
PA5 = SPI1_SCK.**

Slow SCK slew delays the clock edge arriving at the ADXL345. The sensor shifts
MISO on that delayed edge, so its response returns after the STM32 has already
sampled on its own internal, undelayed edge — the sampler captures the previous
bit. One bit of lag, appearing at the last bit sampled in each byte.

Isolated to a single pin by elimination:

| OSPEEDR configuration | result |
|---|---|
| all pins at reset (`00`) | corrupted, stale-hold 7/7 |
| PA7 (MOSI) only at `11` | corrupted, stale-hold 7/7 |
| PA5 (SCK) + PA7 at `11` | clean 7/7 all sequences |
| **PA5 (SCK) only at `11`** | **clean 7/7 all sequences** |

PA6 = MISO is irrelevant, as expected: in master mode it is an input and its
output driver is disabled, so slew configuration has no effect.

Shipped with PA5 and PA7 at `11`. The minimum sufficient setting for SCK was
not characterized — `01` or `10` may well suffice, and the slowest working
setting is the better choice on a breadboard (less ringing and EMI). Left as an
open item rather than a justified decision.

### Verification

All corrective workarounds removed. Post-fix, with plain 2-transfer reads:

- `logA` = `E5 E5 0A 0A E5 0A E5 E5`, clean 7/7
- `logB` = `85 8B 8D 92 94 9A 9F A0` (byte-exact against seeded block), clean 7/7
- `logC` = same, via the burst path, clean 7/7
- Live acceleration, board flat, ±2g / 10-bit: X=10, Y=34, Z=−216. Z magnitude
  slightly under the 256 LSB/g nominal — normal part offset/gain error, and what
  OFSX/OFSY/OFSZ exist to trim. Not yet calibrated.

### What made this hard

**Frequency-independence was misread as ruling out timing.** Identical results
across a 16× baud sweep looked like proof that no race was involved. Wrong
inference: slew delay is roughly fixed per *edge* and does not scale with the
bit period, so a fixed-position sampling edge misses a slow-arriving level
identically at every baud rate. This single bad inference cost most of the day
and steered the investigation away from the actual cause.

**The logic analyzer structurally could not show this.** At 8 MSa/s it
quantizes to clean logic levels, and the *value* on MISO was always correct.
What was wrong was arrival *time* relative to a sampling edge. That is a
scope-class measurement; a logic analyzer will report a late-but-valid level as
valid. Every capture taken today was accurate and every one was misleading.

**Loopback (MOSI jumpered to MISO) reproduced the signature but is not a
faithful model.** Tying output to input creates a near-zero-delay path from the
transmit shift register to the receive sampler, so it can reproduce a matching
symptom by a different route. It correctly proved the sensor and breakout were
not required — the defect was MCU-side — but the mechanism it exhibits is not
the sensor-path mechanism, and the PA7-only result later showed MOSI slew was
irrelevant with a real slave.

**Identical `.text` size is not proof of an identical binary.** Twice today a
real code change left every section size unchanged (absorbed by existing
alignment padding, or because only constants changed). Verify with `objdump -d`
or by reading the peripheral register, not by section sizes.

### Method notes

- **Let the target compute the verdict.** Firmware scored the competing models
  itself and exported single-byte answers, removing the debugger from the
  measurement path. GDB's report and the CPU's value are separate observation
  sites and were being conflated.
- **Poison initializers.** Statics set to `0xAA`/`0xFFFF` rather than zero, so
  "genuine zero" and "never written" are distinguishable. Also confirmed startup
  copies `.data` from flash to RAM.
- **Read the register, don't infer the configuration.** `p/x GPIOA->OSPEEDR`
  settled a stale-binary question in one command. Reset value is `0x0C000000` —
  bits 27:26 are PA13/SWDIO, set by the debug port, not by application code.
- **MCU reset does not reset the ADXL345.** The 3.3V rail stays up through
  `monitor reset halt`, so the sensor retains state across runs. Cold sensor
  trials require a USB power cycle; cold and warm trials must be labeled.
- **Don't halt inside a transaction.** Single-stepping holds CS asserted for
  however long you take, invalidating the capture.

### Open

- Minimum sufficient OSPEEDR value for SCK not characterized.
- ADXL345 offset registers not calibrated.
- Analyzer captures in `captures/` predate the fix and show the *correct*
  wire values — worth keeping precisely because they demonstrate why the
  instrument could not see this.

### Attribution

OSPEEDR was never in the hypothesis list. Seven proposed mechanisms were
eliminated with evidence and none was correct; the actual cause was found
independently after that list was exhausted.

## 2026-08-19 — FreeRTOS configuration

### Goal
Produce a defensible FreeRTOSConfig.h for the F411RE and clear any
blockers before the two-task blink checkpoint.

### Design decisions

**Task decomposition.** Two tasks, split by independent timing domain
rather than by blocking call. Acquisition runs on a period set by the
sensor; the logger runs on a period set by buffer fill and flash
latency. The test for whether two candidates are really two tasks:
would merging them into one sequential function break a deadline? Here
yes, because flash erase stalls for up to 400 ms against a 10 ms
sample period.

**Priority assignment: rate monotonic.** Shortest *period* gets highest
priority — not shortest execution time. Acquisition (10 ms) outranks
the logger (hundreds of ms). Utilization is around 5% against the
two-task RM bound of 82.8%, so the schedulability check is not close
to binding.

Set configMAX_PRIORITIES=5 to accommodate a medium-priority CPU hog at
level 2. That task is not filler — unbounded priority inversion
requires a task that can preempt the mutex holder, so without a level
between acquisition and logger there is nothing to demonstrate.
configUSE_TIMERS=0 partly to avoid the timer service task landing on
that same level.

**Static allocation.** Every task, queue, and mutex is created once at
startup and never deleted, so a heap buys nothing. Setting
configSUPPORT_DYNAMIC_ALLOCATION=0 turns an accidental allocation into
a link error rather than a runtime dependency, and every byte of RAM
becomes a named symbol in the map file instead of hiding inside one
opaque heap array. Obligates vApplicationGetIdleTaskMemory, since the
kernel has no way to obtain the idle task's TCB and stack itself.

**configCPU_CLOCK_HZ = SystemCoreClock.** Read at runtime inside
xPortStartScheduler when it computes the SysTick reload. This creates
an ordering dependency — clock_init() and SystemCoreClockUpdate() must
precede vTaskStartScheduler(). If SystemCoreClock still held its 16 MHz
reset value, every delay in the system would run 6.25x fast with
nothing crashing. Same class of silent-wrongness that
SystemCoreClockUpdate() was chosen to catch during clock bringup.

### Concepts clarified

**Mutex vs. binary semaphore.** Not a scalar difference. A mutex has
ownership (only the taker may give it back) and priority inheritance;
a binary semaphore has neither. Priority inheritance is a temporary,
targeted loan — the kernel raises the *holder* to the *blocker's*
priority until release, so the holder finishes and lets go fast. It is
not "tasks share a priority." Signaling → semaphore. Locking → mutex.

Important limit: inheritance does not fix a holder that simply holds
too long. It only bounds the case where a medium-priority task
preempts the holder.

**Critical section scoping.** The shared resource is the SPI peripheral
and the three wires — not the flash chip's internal state. During a
sector erase the pins are electrically idle while the chip grinds
internally. Holding the mutex across the erase-and-poll loop conflates
"my operation isn't finished" with "the shared resource is in use,"
and blocks acquisition for up to 400 ms. Scoping the lock to each
transaction drops that to microseconds. Generalizes to I2C EEPROM
write cycles, SD card busy signaling, and any DMA transfer (hold while
setting up, not while waiting for completion).

**Tick rate vs. CPU clock.** configTICK_RATE_HZ is the resolution of
time-based *blocking*, not a clock speed. SysTick reload =
configCPU_CLOCK_HZ / configTICK_RATE_HZ; the core still runs at
100 MHz between ticks. Tick rate does not limit sampling rate — a
hardware interrupt fires independently of it — but it does quantize
xTaskDelayUntil wake times, which is the concrete argument for
hardware-triggered sampling over delay-based sampling.

**Two preemption paths.** Event-driven (an API call or ISR readies a
higher-priority task → switch immediately, microseconds) and
time-driven (tick discovers an expired delay → switch in the tick
handler, quantized to the tick). Different mechanisms, different
latency numbers. Needs distinguishing when interpreting scope traces.

**Handler wiring.** The Cortex-M vector table is an array of addresses;
the hardware knows no names. The startup file fills slots 11/14/15
with SVC_Handler / PendSV_Handler / SysTick_Handler. port.c defines
its handlers under FreeRTOS names. The config #defines rename
FreeRTOS's functions *to* the CMSIS names at compile time, so port.c's
strong definitions override the startup file's weak aliases.

### Verification
- grep across src/: no SysTick references outside the config #define.
- Startup file (Templates/gcc/startup_stm32f411xe.s) declares
  SysTick/PendSV/SVC as .weak aliases to Default_Handler — no
  duplicate-symbol conflict with port.c.
- Kernel is V11.x (prvCreateIdleTasks references configNUMBER_OF_CORES
  and prvPassiveIdleTask). Confirmed vApplicationGetIdleTaskMemory's
  third parameter is configSTACK_DEPTH_TYPE*, which resolves to
  uint32_t on this port — matched the declared type rather than the
  currently-resolved one, since older kernels defaulted it to uint16_t
  and a mismatch there writes 4 bytes through a 2-byte pointer.

### Open items
- SPI test main.c to be replaced; spi.c and ADXL345.c temporarily out
  of the build to isolate the blink milestone.
- PA5 conflict: LD2 shares the pin with SPI1_SCK. Needs a different
  GPIO for per-task instrumentation once SPI returns.
- Bring-up delay function: kernel now owns SysTick. Needs a spin-delay
  for pre-scheduler init, and TIM2 (32-bit, free-running, no ISR) for
  microsecond timestamping during measurement.

### Next
Two-task blink with xTaskCreateStatic. Acceptance: both LEDs blink at
independent rates, `p xTickCount` in GDB advances ~1000/sec, `bt` from
a task breakpoint shows a sane stack. No SPI until all three pass.

## 2026-08-19 (evening) — Two-task blink checkpoint

### Goal
Prove the kernel starts, switches, and keeps time before adding any
driver code. Isolating this from SPI means any failure here is a config
failure, not a design failure.

### What broke first
The V11 kernel #errors on missing configUSE_IDLE_HOOK and
configUSE_TICK_HOOK rather than defaulting them. Initially read as an
annoyance; on reflection it is the right call — an application-provided
hook is not something a kernel should assume the absence of, since a
silently-disabled hook would be a much worse failure than a compile
error. Same philosophy as configASSERT.

Also had to add timers.c, event_groups.c, and stream_buffer.c to the
build despite configUSE_TIMERS=0. tasks.c references symbols from them
regardless; --gc-sections removes the dead code, so the cost is zero.

### GPIO init ordering
Between reset and led_init(), PA8/PB10 are high-impedance inputs. The
moment MODER switches them to output they drive whatever ODR holds —
reset value 0, which for active-low wiring means *on*. Writing BSRR
before MODER parks the desired value so it takes effect the instant the
pin becomes an output, eliminating the glitch.

Microseconds and invisible on an LED, but the same pattern matters for
chip selects (a CS glitching low can put a device into an unexpected
state) and anything driving a relay or MOSFET. Worth doing correctly
here so the habit is in place where it counts.

Slew rate deliberately left at reset value. This is the inverse of the
SPI1_SCK bug: there the default was mismatched to the requirement
because edge time was a meaningful fraction of a 2.5 us bit period.
For an LED at 1 Hz, edge time is irrelevant and the slowest setting is
strictly better — less di/dt, less ringing, less EMI. Same register,
opposite conclusion, because the signal's requirement differs.

### One task function, two instances
Rather than two near-identical functions, one blink_task parameterized
by a config struct passed through pvParameters. The kernel passes a
single untyped void* through untouched; the task owns the cast. Same
callback idiom as qsort's comparator.

Lifetime is the trap: vTaskStartScheduler() never returns, so main's
stack frame is abandoned. A non-static local config would leave both
tasks dereferencing memory that has been repurposed as someone else's
stack — silent corruption, hard to trace. static const puts them in
.rodata.

### On-target verification

**Tick rate.** xTickCount 71422 -> 72490 over roughly one second =
1068 ticks. Within reaction-time error of 1 kHz. This is the check that
matters, because a configCPU_CLOCK_HZ mismatch does not crash anything —
it would just make every delay in the system proportionally wrong. If
SystemCoreClock had still held its 16 MHz reset value, the count would
have been ~6400.

**Task identity.** A breakpoint in the loop body hits alternately with
pvParameters=0x800161c <green_cfg> and 0x8001610 <red_cfg>. Note the
first attempt set the breakpoint on the prologue line (the cfg
assignment), which executes once per task at startup and had already
passed — the breakpoint never hit. Set breakpoints inside the loop, not
in the one-shot prologue.

**Backtrace.** GDB reports "previous frame identical to this frame
(corrupt stack?)" with a bogus uxListRemove frame beneath the task.
This is expected on every FreeRTOS task, forever: a task has no caller.
port.c synthesizes a fake exception frame at stack creation so the
first context switch can "return" into the task, and there is nothing
beneath it. The unwinder grabs whatever word is there — confirmed by
finding 0x08000ed5 (uxListRemove+1) as a stale value near the top of
the stack. Frame #0 is correct; the rest is GDB guessing.

**Stack usage.** uxTaskGetStackHighWaterMark was unavailable in GDB —
--gc-sections stripped it since nothing calls it. Read the fill pattern
directly instead: method-2 overflow checking fills stacks with
0xa5a5a5a5 at creation, so the transition marks the high water line.

Deepest touched word at index 103 of 128 => ~25 words (~100 B) peak,
~412 B free. Usage is not a solid block — there are 0xa5a5a5a5 gaps
above the deepest point, since alignment padding and unwritten slots
get skipped. High water mark is deepest penetration, not count of
dirty words.

Also visible at the deep end: 0x000001f4 = 500 decimal, the green
task's period_ms spilled during the vTaskDelay call chain. That call
is the deepest path in the task.

**Saved context, read directly off the stack:**

0xfffffffd EXC_RETURN (software-saved by port.c)
0x00000000 r0
0x20000228 r1
0x10000000 r2
0xe000e000 r3 (SCS base)
0x000132a4 r12
0x08000d09 LR
0x08000d44 PC
0x61000000 xPSR (bit 24 Thumb, top nibble NZCV)

0xFFFFFFFD decodes as: return to Thread mode, use PSP, *basic* frame.
Confirms tasks run on the process stack, and that lazy FPU stacking
never fired because nothing here touches float. A float-using task
would show 0xFFFFFFED with the frame 18 words larger — which is the
concrete reason to budget +35 words for any task doing floating point,
including indirectly via printf("%f").

### Baselines recorded
- RAM 4120 B / 128 KB (3.14%): two 512 B task stacks, idle's 1 KB,
  TCBs, kernel bookkeeping. Every byte traceable in the map file — the
  practical payoff of static allocation over a heap array.
- Flash 5716 B / 512 KB (1.09%)
- Task stack peak: ~25 words for GPIO write + vTaskDelay

Sizing basis for the real tasks: acquisition adds a SPI transfer and a
queue send, so budget 60-80 words and round to 128. Logger needs more
if it holds a page buffer — that goes in static storage, not on the
stack.

### Config surface now validated against hardware
Vector table and CMSIS handler renaming, SysTick reload derived from
SystemCoreClock, static allocation with vApplicationGetIdleTaskMemory,
task creation, pvParameters plumbing, preemptive context switching.
None of this is assumed any more.

### Next
1. Restore spi.c / ADXL345.c to the build.
2. Convert green task to acquisition at 10 ms with xTaskDelayUntil.
   Toggle a GPIO on entry/exit and measure the actual period on the
   logic analyzer — establishes the measurement rig before any mutex
   exists to complicate the trace.
3. Only then add the logger task and the SPI mutex.

Open: PA5 conflict (LD2 vs SPI1_SCK) still needs resolving for
instrumentation pins. TIM2 free-running for microsecond timestamps not
yet set up.

## 2026-08-19 (late) — Acquisition task

### Goal
Single task, 10 ms period, real ADXL345 reads. No mutex, no logger.
Isolating this means any timing problem found here belongs to the task
or the tick, not to contention.

### Bugs found

**DEVID compared against the wrong constant.**
```c
if (id != ADXL345_REG_DEVID) return false;   // 0x00, the register ADDRESS
```
Wanted ADXL345_DEVICE_ID (0xE5), the expected value. As written the
function returned false whenever the sensor was working and true if the
bus was dead and returning zeros — exactly inverted. The two macro names
differ by one word and sit four lines apart in the header, which is the
whole problem. Renaming the value constant to something that cannot be
mistaken for an address would have prevented it.

Generalizes: when a header defines both an address and its expected
contents, the names need to be structurally different, not just
different strings.

**Missing POWER_CTL write.** The ADXL345 comes out of reset in standby.
Every SPI transaction succeeds, CS toggles correctly, the decode looks
perfect — and DATAX0..DATAZ1 all read zero. This is a failure mode that
looks like a bus problem but isn't, which is exactly why reading DEVID
first is worth the two lines: DEVID is readable in standby, so a good
DEVID plus zero data localizes the fault to configuration immediately.

**Pointer where storage belonged.** Initial version declared
`static ADXL345_Accel_t *accel;` (an uninitialized pointer), passed
`&accel` as pvParameters, and called `adxl345_read_acceleration(&accel)`
— address-of a pointer, twice over, with nothing ever allocated.

The underlying confusion was about what the struct *is*. A sample is an
output produced fresh each iteration; it belongs as a task local. A
device handle would be configuration and could reasonably arrive via
pvParameters. Decided against a handle entirely: there is exactly one
ADXL345 on one bus, so a handle is ceremony. If the W25Q64 later
creates a genuine need for a shared "SPI device" concept, build it then
with the real requirement in hand rather than speculatively.

### Hardware
OpenOCD failed with `init mode failed` and reported target voltage
4.42 V — should be ~3.3 V. Cause was seating the Nucleo directly in the
breadboard; the header rows do not straddle the center channel, so
adjacent pins short through shared columns and 5V bridged onto 3V3.
Fix: Nucleo stays off the breadboard, jumpers run from its headers to
the sensor and passives.

Worth remembering that the voltage reading in OpenOCD's banner is a
real diagnostic, not decoration. 4.42 V pointed straight at the wiring
before any time was spent on SWD configuration or connect-under-reset.

### GDB: scope and initialization
First attempt at `p sample` broke at line 50 — the function's opening
brace, before the local is initialized. Read back
`x = -23131, y = -23131`, which is 0xA5A5: the FreeRTOS stack fill
pattern, not data. The two axes being *identical* was the giveaway.

Second attempt failed with "No symbol sample in current context" while
stopped in prvIdleTask — locals only exist in their own frame.

Both are obvious in retrospect and both cost a minute. The rule:
breakpoint placement has to account for where a value becomes valid,
not just where the variable is named. File-scope statics (like
`overruns`) are readable from anywhere; locals are not.

### Verification
Rotating the board moved gravity cleanly between axes:

x=3, y=31, z=-223
x=-256, y=11, z=12
x=4, y=-255, z=10

~250-256 counts on the loaded axis, other two within ~30 of zero. At
±2g and 10-bit that is 256 counts/g, which matches the datasheet
exactly. Signs reflect breakout orientation, not an error.

This is the end-to-end proof: measure mode active, SPI byte order
correct, little-endian int16 reassembly correct.

overruns = 0. No missed deadlines at 10 ms with a ~150 us transfer.

### Known inefficiency, deliberately left alone
spi1_transfer() calls spi1_wait_idle() between *every* byte, waiting for
BSY to clear before checking RXNE. That fully serializes the transfer —
no pipelining — and busy-waits at the task's priority the whole time.
At 390 kHz (BR=111, PCLK/256) a 7-byte read is ~144 us of spinning.

Invisible with one task. Once the logger exists, that is 144 us the
logger cannot run. BSY only needs to be clear before deasserting CS at
the end of a transaction, not between bytes.

Not changing it now: this milestone is measuring baseline timing, and
moving two variables at once destroys the measurement. Fix it after the
baseline capture exists.

### Next
1. Logic analyzer on PA8: confirm 10.00 ms period, ~150 us pulse width.
   4 MSa/s, rising-edge trigger, ~100 ms window. This is the baseline
   trace that the broken-mutex and fixed-mutex captures get compared
   against — same axes, same settings, three screenshots.
2. Add DATA_FORMAT and BW_RATE writes to adxl345_init(). Defaults are
   already ±2g / 100 Hz, but writing them explicitly makes the coupling
   between the sensor's ODR and the task period visible in code instead
   of inherited silently.
3. Then the logger task and the SPI mutex.

Open: TIM2 free-running microsecond counter still not set up. Will want
it for in-code timing once the mutex work starts.