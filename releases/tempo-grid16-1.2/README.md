# Trad-Seq 1.2 — audio scheduling test candidate

Version 1.1 was reported to crackle continuously. This candidate spreads the stretcher's alignment search over the preceding grain instead of doing it in a single audio block. Hardware confirmation is still required; this is not a claim of a proven hardware fix.

## Install without replacing your samples

Power CHOMPI off and back up the current card. Replace only the root-level firmware with this folder's `CHOMPI_TEMPO_GRID16.bin`, keeping all sample folders and JSON settings. Ensure exactly one root `.bin`, eject cleanly, and boot. Do not copy this whole folder onto the card. [Complete controls, rollback and test sequence](../../TEMPO-GRID.md).

Compare the same Think loop with Grid disabled and enabled, then try mutes, tempo, pitch, reverse, and effects. If crackling remains, note whether ordinary Tempo playback also crackles. No new samples or layout changes are included.

## Evidence

- Source: `dd6c63639c52f54ad9cced6f00dfd39ae530f1de`; author/committer zak-raindog.
- Compiler: Arm GNU 13.3.rel1 / GCC 13.3.1; CHOMPI BOOT_SRAM.
- Binary: 266,596 bytes, within the 288,768-byte execution region.
- SHA-256: `184d32fd311095262f1dc960b60287e3861a4b4fcae2d6ad75e71dfc3bf2b0ab`.
- Search work per 24-frame callback: maximum **137 → 8** candidates, a 17.1× reduction in peak alignment-search work. Average total processing cost is not claimed to fall by that factor.
- 150 fixed-control configurations render identically sample for sample to 1.1, including fractional tempo ratios. Live-control and boundary cases remain finite/bounded and stay within the work limit.
- Sanitized DSP, sequencer, clock, stereo/duration/pitch tests pass.
- Full package checksums, clean-upstream patch replay and ZIP identity pass.
- No physical hardware test or worst-case callback duration measurement yet.

Run `bash tools/test-grid.sh`, `bash tools/build-grid.sh`, and `python3 tools/verify-package.py` to regenerate the full package, including the official Tempo rollback. The inherited save/copy issue remains: use preloaded samples and avoid on-device save/copy to slots until repaired separately.
