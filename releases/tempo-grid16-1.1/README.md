# Tempo Grid 16 / Trad-Seq 1.1 — hardware test build

A switchable sixteen-step sequencer for CHOMPI TEMPO. See [the complete controls, installation and limitations guide](../../TEMPO-GRID.md).

## Install on an existing Tempo card

Back up the card, power CHOMPI off, and replace the single root-level firmware binary with `CHOMPI_TEMPO_GRID16.bin` from this folder. Keep the existing sample folders and JSON settings. Ensure exactly one root `.bin`, eject cleanly, then boot. Do not copy this entire repository onto the card. Users of Tape/Wave should use the complete Tempo card layout described in the guide.

In Slice view, hold Shift first and then Loop for 0.75 seconds to enable/disable Grid. White keys 1–15 are steps 1–15; the rightmost black key is step 16. `_b4`, `_b8`, and `_b16` filename tags select the loop length.

## Build and validation

- Firmware source: commit `9b0379a` on top of official upstream `a73d732613da684e4de844619b690776f0f50ccf`.
- Arm GNU Toolchain 13.3.rel1; Daisy Seed / CHOMPI BOOT_SRAM.
- Binary: 266,396 bytes of the 288,768-byte execution region.
- SHA-256: `e77609c8ef9fa0f50e72ba6bc6ec7ae817a4315a629f376f42218aeb66993b44`.
- Sanitized DSP, production-clock and sequencer-adapter tests pass; audio checks and host results are included here.
- Clean-upstream patch replay and packaged source/binary identity pass.
- Owner reports successful boot and Think-loop Grid playback. Full hardware acceptance and CPU headroom remain unmeasured. This is not a multi-window firmware.

The build matches the previously installed 1.1 binary byte-for-byte. Packaging changes do not alter its DSP.

## Known inherited limitation

Tempo's existing RAM save/copy routine has byte-count and destination-addressing defects identified by source review. Use preloaded samples and avoid save/copy to slots pending a separate tested repair. Recording also has no beat tag and defaults to a four-beat Grid cycle. See the guide for all limitations.

To regenerate the full package, run `tools/test-grid.sh`, then `tools/build-grid.sh` with `GCC_PATH` pointing at the required toolchain, then `python3 tools/verify-package.py`. The generated package includes a factory Tempo card profile, the exact original rollback binary, debug symbols and checksums. This folder intentionally contains only the firmware and compact validation evidence.
