# Tempo Grid 16 — CHOMPI hardware test build

A switchable modification of CHOMPI TEMPO 1.0, based on the official open-source release at commit `a73d732613da684e4de844619b690776f0f50ccf`.

**Status:** Version 1.2 is a hardware-test candidate for the Daisy Seed / CHOMPI BOOT_SRAM target with Arm GNU Toolchain 13.3.rel1. Version 1.1 boots and plays, but the owner reports continuous crackling. Version 1.2 addresses a large burst of stretching work in the audio callback. Software tests pass; removal of the audible crackling and actual processor headroom remain unverified on hardware. It is a community modification, not an official CHOMPI release.

## Version 1.2: continuous-crackle investigation

CHOMPI processes 24 frames per callback at 48 kHz: a 500 microsecond deadline. Version 1.1 performs 137 waveform-alignment candidate comparisons at a grain boundary, every 480 frames (10 milliseconds). Each candidate checks 24 stereo positions. That concentrates 3,288 stereo comparisons into one audio callback, in addition to both engines and effects. A missed deadline is a credible cause of regular crackling, but no on-device timing capture has yet proved it is the sole cause of this report.

Version 1.2 searches ahead, evaluating one candidate every three output frames, and consumes the completed result at the next grain boundary. All candidates finish within 411 frames, before the 480-frame deadline. The busiest 24-frame block now has at most eight candidates (192 stereo comparisons), including blocks crossing step boundaries. It retains the same candidate order and stereo alignment; 150 fixed-control combinations across beat spans, pitch, reverse, and tempo render sample-for-sample identically to 1.1 in host tests. Live pitch, reverse or tempo changes invalidate a pending result and use the nominal grain position for that hop; the next search uses the new settings. No allocation, locks, SD I/O, or whole-search fallback is added to the audio callback.

### Test this candidate

Use the **firmware-only** update below so your current drum chops and settings stay in place. Do not copy the supplied factory `sd-card` profile onto your existing sound bank. Keep the existing card backup and old binary on your computer.

1. Start with the same Think loop, tempo, output connection and listening level that crackled in 1.1. Listen to 1.2 with Grid disabled, then enable Grid with all steps on. Compare at a comfortable low level.
2. Try several muted steps, 80/160/240 BPM, forward/reverse and pitch changes. Compare a 4-, 8- and 16-beat loop. Listen for continuous crackle separately from occasional step-transition clicks.
3. Repeat with the chromatic engine and effects active. Those share the callback budget. If crackle appears only under that load, note the settings.
4. If crackling persists, disable Grid again. Report whether it remains in ordinary Tempo mode and whether lowering output level changes it. A short recording is useful if available. The included `rollback/CHOMPI_TEMPOv1_0.bin` is the exact official Tempo fallback, installed using the same one-bin update process.

The target's existing debugger-visible `testTimer` stores the most recent callback duration in microseconds. A full hardware qualification must measure worst-case callback time across grain/step boundaries and effects load, not just that last value or average CPU usage. Host work-count tests prove a scheduling bound, not an STM32 execution-time bound.

## What it does

The selected Slice-engine sample (or its trimmed window) is divided into sixteen equal time regions. Step 1 plays slice 1, step 2 plays slice 2, through step 16, then repeats. Files without a beat tag use a four-beat cycle (sixteenth-note steps). A filename token `_b8` selects eight beats (eighth-note steps), and `_b16` selects sixteen beats (quarter-note steps). All spans keep sixteen equally spaced slices. A muted step remains in place as a rest; later slices never move earlier.

The sample is stretched/compressed to fit the selected span using stereo waveform-aligned overlap/add (WSOLA). Pitch remains independently controlled by the Sample Staging knob. This is a compact real-time granular stretcher: texture and small pitch deviations can occur, especially on very short slices or extreme tempo changes. It is not transparent offline stretching. The existing slice volume, pan, filter, reducer and global FX remain in the signal path. Grid playback uses fixed 1 ms boundary fades; the stock attack, release, sustain and loop settings remain stored for ordinary Tempo playback.

The mod starts **disabled** at each power-up. All sixteen steps initially play. The mask survives mode toggles and sample changes during the session, but is not saved to the SD card or included in Tempo's A/B snapshots. A/B and sample/preset operations remain available for Tempo's existing settings.

## Version 1.1: sample beat lengths

Name a sample `slice_a1_b4_amen.wav`, `slice_a2_b8_think.wav`, or `slice_a3_b16_blakroc.wav`. The token must end at an underscore, dot or end of name. Missing/invalid tokens default to four beats. Selecting a sample loads its span and restarts the grid. Trimming keeps the selected span, stretching the trimmed window across it. The live recording buffer defaults to four beats. Stock Tempo ignores the tag when Grid is disabled. Stock on-device save/copy operations rename files and therefore reset their span to four beats; restore the tag on the computer after saving/copying.

## Install

### Known inherited save/copy defect

Use preloaded samples for this test build. Source review of Tempo's existing `copyRamToRam` found that it multiplies a byte count by four again and computes destinations without distinguishing the sample bank. Its addressing also differs from boot-time allocation. This PR does not repair that upstream save/copy path; avoid saving or copying recorded samples to slots until a separately tested repair is available. This is a source finding, not a reproduced hardware failure. Live effects and preloaded sample playback use separate paths.

### SD update

1. Power CHOMPI off. Back up the entire current microSD card to your computer, including samples and JSON files. A spare FAT32 card is convenient for testing.
2. If the card already has a working **Tempo 1.0** layout, remove its root-level firmware `.bin` and copy **only** `firmware-only/CHOMPI_TEMPO_GRID16.bin` to the root. Keep its sample folders, `options.json`, and `presets.json`.
3. If coming from Tape/Wave, or using a spare card, copy the **contents** of `sd-card/` to the root of a FAT32 card. This supplies the factory Tempo sample folders/settings plus the modified binary. Do not put the enclosing `sd-card` directory itself on the card.
4. Ensure there is exactly **one root-level `.bin`** on the card. Never copy the whole release package, `rollback/`, or `debug/` to the card.
5. Eject the card cleanly, put it into CHOMPI, and power on. The existing bootloader should display its slow rainbow firmware-loading animation. Wait for startup to finish.

This is an application update via SD card. No bootloader replacement or DFU operation is required for an already functioning CHOMPI. Follow the official [build/update guide](https://github.com/CHOMPI-Club/CHOMPI/blob/main/firmware/README.md) if your device has no working bootloader.

## First play

1. Put the mode toggle **down** (Shift/JAMMI position).
2. Hold the **CHOMPI key** (Shift) and press the **second black key from the left** to select the **Slice engine**. While still in Shift, press the **first white key** to select factory slice sample 1. Release the keys.
3. Hold **CHOMPI/Shift first**, then hold **Loop** for at least **0.75 seconds**. The Loop light turns cyan. Release Loop, then Shift. The grid is now enabled.
4. Tap **Play**. The white playhead moves across the sixteen steps. Tempo's ordinary Play start/stop behavior remains in use; a long Play hold retains Tempo's engine-decoupling behavior.
5. Tap a grid key to mute/unmute that position. Teal = enabled; very dim teal = muted. The current position is white when enabled and red when muted.
6. Turn the large **Transport/Tempo knob** to change the bar tempo. Tap-tempo still works. The free tempo range is 80–240 quarter-note BPM, matching Tempo's original clock range. The grid uses the sample filename’s beat span; Tempo's per-engine subdivision setting does not change the grid.
7. Repeat **Shift + hold Loop** to disable the mod and return to the original Tempo slice pattern generator. The original pattern is retained.

## Sixteen-key layout

CHOMPI has fifteen white keys, so the final step uses the **rightmost black key**. There is no alternating/double-purpose last step in Grid mode.

| Grid positions | Physical keys |
|---|---|
| 1–15 | All fifteen white keys, left to right |
| 16 | Rightmost black key (immediately left of white key 14, above the 13–14 gap) |

The other black keys do not trigger notes while viewing the grid. Holding Shift opens the usual Tempo sample/preset menu, including its normal use of the black keys.

| Action | Control |
|---|---|
| Enable/disable Grid | In Slice engine, hold Shift, then hold Loop for 0.75 s |
| Toggle one step | Tap its grid key |
| Mute all / enable all | Tap Loop without Shift; if all are enabled it mutes all, otherwise it enables all |
| Start/stop | Tap Play |
| Change BPM / tap tempo | Transport/Tempo knob / press it |
| Select sample | Hold Shift and press the sample's white key |
| Trim sample | Sample Start and Sample End, page 1 |
| Pitch / reverse | Sample Staging, page 1; reverse reverses audio within each slice, not step order |
| Return pitch to default | Shift + press Sample Staging, page 1 |
| Clock source | Existing Tempo control: Shift + hold Transport knob for about 1 s |

Selecting a new sample, changing trim, starting transport or enabling Grid restarts at step 1. Stop/start returns to step 1. The existing chromatic engine can continue alongside the grid. Switching the foreground view to Chromatic does not disable a running grid.

Recording temporarily silences the grid; when recording ends, playback restarts on the resulting selected buffer. To test rests as silence, turn FX mix down: delay/reverb tails from previous enabled steps can legitimately continue through muted steps. Input monitoring may also be audible with the mode toggle up.

## MIDI

Internal and external clock both use 6, 12 or 24 incoming pulses per grid step for 4-, 8- or 16-beat loops (MIDI's 24 pulses per quarter note). Existing Tempo clock and transport options still govern MIDI input/output. The factory profile enables outgoing transport; for incoming Start/Stop, set the `Midi Start-Stop Message Behavior` value in the card's `options.json` to `1` (both directions) or `3` (receive only), or start/stop locally while receiving clock.

MIDI Start, when enabled in Tempo's transport options, restarts Grid at step 1; Stop stops it. The grid uses the underlying clock directly and ignores the Slice arpeggiator's subdivision. No new MIDI note sequence is emitted by Grid; ordinary chromatic-engine MIDI behavior is preserved. Slice MIDI note input is ignored while Grid is enabled, leaving the stored Tempo pattern intact.

If external clock disappears, the current slice finishes its estimated step duration and falls silent. The playhead waits for further clock pulses; it does not free-run. The first externally clocked step may use the previous tempo estimate until the first pulse interval is measured.

## Ten-minute hardware acceptance test

- Boot the complete test card. Confirm ordinary Tempo works before enabling Grid.
- Select Slice sample 1, enable Grid, and start. Confirm all sixteen positions advance, including the separate last black key.
- Select tagged 4-, 8- and 16-beat samples. Confirm the complete grid takes 1, 2 and 4 bars at the same quarter-note tempo.
- Mute steps 2, 4, 8 and 16. Confirm these are rests and the bar length is unchanged. Turn effects down to isolate dry rests.
- Tap Loop once to restore all steps. Tap again to mute all. Confirm the playhead continues moving in silence. Tap once more to restore all.
- Change tempo slowly and then quickly. Confirm the bar stays in time and pitch is substantially retained. Check headphones and main outputs for distortion/dropouts; repeat with both engines and effects active.
- Try pitch, reverse, trim, a very short sample and a newly recorded buffer. An empty or reversed trim window should be silent, not crash.
- Toggle the mod off and on while stopped and while playing. Confirm the stored Tempo pattern returns and no old slice notes get stuck.
- Test incoming MIDI clock and Start/Stop if used in your setup, then disconnect the clock and check the expected silence/wait behavior.
- Power-cycle: ordinary Tempo should return with Grid disabled and all steps reset to enabled.

Stop here if the unit fails to boot or audio repeatedly drops out, and use the rollback procedure. The host tests cannot establish target CPU headroom or SD/USB hardware compatibility.

## Rollback

Power off, remove the modified root `.bin`, and put `rollback/CHOMPI_TEMPOv1_0.bin` at the card root as the only `.bin`. This is the **original official factory binary**, copied byte-for-byte from the upstream Tempo card profile. Reinsert and boot. To return to a different previous firmware, restore the entire card backup, including its matching sample/configuration layout.

## Implementation plan and completion

1. **Establish a baseline:** pin upstream release, obtain the specified ARM compiler, build unchanged Tempo. Done.
2. **Add an isolated grid core:** fixed sixteen positions, independent mute bits, 6/12/24-pulse step clock, bounded stereo granular reads, no dynamic allocation. Done in `StepGrid.h`.
3. **Integrate as an optional Slice path:** use existing sample selection/trim, transport, output processing and clock; bypass slice pattern generation only while enabled. Done.
4. **Add physical controls:** Shift+Loop hold, sixteen explicit keys, playhead/mute lights, all-on/all-off. Done.
5. **Guard integration hazards:** fix empty-pattern accesses used by transport/LEDs, initialize slice voice priorities, guard unassigned voice keys during slicing, and process the stereo slice envelope once per frame. Done.
6. **Verify and package:** sanitizer tests, real sequencer/clock host adapters, objective rendered-audio checks, ARM link/memory checks, reproducible build tools, factory card profile, rollback and checksums. Done. Physical acceptance remains for the owner.

## Rebuild and inspect

Source: `firmware/chompi-tempo/code/src/`. `StepGrid.h` is the portable DSP/sequencer core. Integrations are limited to SampleManager, SliceEngine, ArpeggiatorSequencer, NormalPage, MenuPage, clockManager, MidiManager and the main audio callback.

- `tools/test-grid.sh` runs host tests with AddressSanitizer and UndefinedBehaviorSanitizer, then spectral/duration checks (clang++ and Python with NumPy required). Clock/sequencer adapter tests compile the production class bodies with only hardware dependencies replaced.
- `tools/build-grid.sh` makes a clean ARM build and packages it. Set `GCC_PATH` to the `bin` directory of **Arm GNU Toolchain 13.3.rel1**. By default it uses the project-local toolchain downloaded beside this checkout. The official vendored libDaisy/DaisySP archives are used as provided by CHOMPI.
- `validation/grid-build.log`, `host-tests.log` and `audio-checks.json` contain validation evidence. `debug/CHOMPI_TEMPO_GRID16.elf` and `.map` accompany the release for debugging.
- `tempo-grid.patch` in the package is the firmware-source diff against the pinned upstream commit, including the new grid header. It can be applied with `git apply` from the upstream repository root.

The code and factory assets retain their upstream licenses; see `LICENSE`, `THIRD_PARTY.md` and `TRADEMARKS.md`.
