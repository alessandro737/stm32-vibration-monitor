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
