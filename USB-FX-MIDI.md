# CHOMPI USB FX + MIDI — experimental test firmware 0.1

**Software-validated hardware test candidate. No CHOMPI was available to enumerate, listen to, or measure. Do not treat this as a proven USB audio release.** This is the firmware foundation for a phone-controlled instrument and a one-sample hardware effects print workflow. Cellophane app integration is not implemented.

## What this build does

- Exposes one stereo phone/computer-to-CHOMPI stream, one stereo processed return, and one USB MIDI port over the external USB-C connector. USB Audio Class 1; fixed 48 kHz, signed 16-bit stereo. The controller runs at USB full speed (12 Mbit/s).
- Sends USB input through TEMPO's shared granular delay/freeze, reverb, and final compression/saturation. The chromatic page's FX mix controls the input's dry/wet contribution. Master gain affects both physical outputs **and the recorded USB return**. Input gain and analog monitoring do not affect the USB feed.
- Sends the post-output-effects master left/right signal back over USB, and to the physical outputs. No microphone, Aux input, resident sample voice, or hardware resampling signal enters the return. Input is normalized PCM, without the analog input's 8× gain.
- Retains existing TEMPO MIDI notes, CCs and clock behavior, now with nonblocking USB transmission. Incoming MIDI uses the existing parser. Outgoing transport supports TEMPO's complete 1–3 byte messages; arbitrary outgoing SysEx, a new controller mapping, per-window focus, and state synchronization are future work.
- Runs as a **dedicated USB effects build**: local sample playback and Trad-Seq audio are bypassed. Use the supplied Trad-Seq rollback binary for your normal standalone setup. Do not use hardware sample capture/save operations in this experimental mode.

This is not yet the complete “control every stem / Replace or Save as new” product. Those are app and controller-profile work after the USB route passes hardware testing. Pitch shifting, sample slicing and the individual sample filters are not inserted into this live USB effects path.

## Files and installation later

The generated `CHOMPI-USB-FX-MIDI-0.1-test.zip` contains `firmware/CHOMPI_USB_FX_MIDI.bin`, debug symbols, a manifest, test evidence, synthetic test WAVs, this guide, and the known Trad-Seq 1.1 rollback binary. It contains no personal samples and is **not** an SD-card replacement layout.

When hardware is available, use an existing working TEMPO card:

1. Power CHOMPI off. Back up and verify the card.
2. Move its current root firmware `.bin` into the computer backup. Copy only `firmware/CHOMPI_USB_FX_MIDI.bin` to the card root. Leave the existing sample directories and JSON files in place. Keep exactly one root-level firmware `.bin`.
3. Eject the card, insert it into CHOMPI, and power on. CHOMPI loads firmware from the card at boot; see the official mini guide, “SD Card: Firmware,” pp.13–14. The retained TEMPO card layout and one-binary swap are this project's tested Grid installation method, not a claim that the stock TAPE manual documents this custom USB build.
4. Start with low headphone and host playback levels. For rollback, power off and replace the root binary with `rollback/CHOMPI_TEMPO_GRID16.bin`, again leaving exactly one `.bin` in the root. Do not copy either containing folder to the card.

No mounted card or installed firmware was changed while developing this build.

## First hardware session: Mac first, then phone

Use a data-capable cable. Connect CHOMPI to the Mac and look for **CHOMPI USB FX + MIDI** in Audio MIDI Setup and its MIDI window. Confirm two input channels, two output channels, and 48 kHz / 16-bit audio. Confirm left/right independently with `channel-id.wav`. Begin around −18 dB host level; effects and master gain can add level.

Power is a separate unverified constraint. The descriptors request up to 500 mA; that is not a measured device power budget. CHOMPI's mini guide recommends 5 V / 2 A power (p.2). Check with a charged battery and record the exact host/cable/power setup. If the phone cannot power the device, a powered data hub may be needed; this build does not promise that a phone alone can power or charge CHOMPI.

Use a DAW that can play to and record from the same stereo device. Send only the chosen test/sample track to CHOMPI. Record input channels 1–2. **Turn off software monitoring of the return to CHOMPI's output** to prevent a digital feedback loop; listen at CHOMPI's physical output. This device exposes only one stereo send. Solo the source for an isolated print; other stems cannot share this send without also entering the effects and recording. A separate dry monitoring pair is not implemented.

Test playback-only and capture-only before duplex. Capture-only can contain a deliberately retained delay/freeze tail, but never a new microphone or local sample signal. Then run duplex with sustained tones while moving effects controls and sending/receiving MIDI. Confirm note-on, note-off, CC, and clock in both directions. Physical controls still follow existing TEMPO page/shift rules, not a new Cellophane map.

Next test the intended phone with an existing audio/MIDI host application capable of full-duplex external audio. Record phone model, OS, app, buffer size, cable, power arrangement and device format. macOS success alone does not establish phone compatibility. No app has been chosen or purchased for this test.

## Acceptance checklist and evidence to capture

| Test | Required observation |
|---|---|
| Enumeration | Stable audio + MIDI device; correct format/channels; no reconnect loop |
| Signal identity | Left/right, polarity and level correct; silence has no microphone/Aux/sample contamination |
| Shared effects | Delay/freeze, reverb and output dynamics audibly affect the USB return; record gesture changes |
| Clock endurance | At least one hour duplex, FX active and MIDI traffic; no increasing buffer faults, clicks or drift |
| MIDI | Notes, note-offs, CC and clock survive simultaneous audio; no stuck notes or MIDI feedback |
| Lifecycle | At least 20 start/stops, playback-only, capture-only, close/reopen, configuration changes, suspend/resume, unplug/replug |
| Latency | Record `latency-impulses.wav`; measure round-trip offset and variation, including host buffering; do not assume a fixed compensation |
| Timing | Measure worst-case audio callback and USB IRQ duration on hardware; audio period is 500 µs. Host execution times are not MCU measurements |
| Recovery | No stale queued audio/MIDI, frozen stream or unrelated endpoint loss. A retirement timeout requires reconnect/power cycle and a bug report |
| Rollback | Original Trad-Seq binary boots and plays the previously working sample |

Optional diagnostics: `tools/usb-diagnostics.py --count 60 > usb-counters.jsonl` reads a versioned EP0 counter snapshot. It needs PyUSB/libusb and OS permission; it never detaches an audio driver or selects a new USB configuration. A host driver may prevent these reads. Counters are cumulative since boot; compare their change during steady playback, separately from deliberate interruption tests. They include playback/capture FIFO faults, malformed packets, dropped MIDI, ISO recoveries, retirement failures, missed SOFs, and audio callback maximum/500 µs over-budget count. The audio timer excludes early boot/loading returns and does not measure USB IRQ duration or interrupt-entry latency. Zero counters alone are not proof of glitch-free sound.

Keep a take recoverable until it has been auditioned. In the future app, replacement will change a sample reference with Undo; it will not overwrite a shared original file. Delay/freeze/reverb state can persist between streams. Clear or let tails decay before measuring a new isolated print; a defined tail/reset policy is still app work.

## Implementation and software evidence

Build switch: `USB_AUDIO=1`, separate `BUILD_DIR=build-usb`. The default firmware remains available without that switch. The external OTG_HS controller uses its embedded full-speed PHY, USB DMA disabled. Static buffers and bounded queues are used; no USB allocation, delay, busy retry or blocking endpoint-abort loop occurs in the audio callback. Vendor initialization and normal endpoint operations still use the vendored HAL; real interrupt timing must be measured.

The 291-byte configuration has two function associations and five interfaces: audio control, playback (alt 0/1), capture (alt 0/1), MIDI control, MIDI streaming. MIDI uses `01/81`; audio `02/82`; explicit feedback `83`. Audio packets permit 47/48/49 stereo frames; feedback is three-byte 10.14. A codec-frame/SOF estimator and slow ring-level correction handle independent clocks. USB hardware frame deltas account for coalesced SOF interrupts. IN packets are queued from completion callbacks because this HAL schedules the next frame; SOF-only submission would halve the capture rate.

Endpoint retirement is an asynchronous state machine. OUT retirement waits for global NAK and drains the shared RX FIFO through the normal HAL; it never flushes MIDI or EP0 packets. IN retirement disables the transfer and flushes only its own FIFO. Stop/start, configuration zero, suspend and reset have distinct handling. Staging memory remains alive through retirement. A 12 ms timeout quarantines the endpoint rather than spinning in an interrupt. This register sequence is source-reviewed and simulated, not silicon-validated.

USB vendor/product IDs are inherited from the existing experimental Daisy/CHOMPI stack (`0483:5740`); this is **not an assigned new USB product identity** or a certified shipping device. Product name, device release and `FX-` serial prefix distinguish this prototype from the existing TEMPO profile. Resolve identity/licensing and physical power declarations before distributing a production release.

Run `tools/test-usb.sh` for the real composite class and vendored USB control stack under ASan/UBSan, plus production FX routing and endpoint-retirement tests. It covers descriptors, controls, invalid requests, independent streams, MIDI, lifecycle, and synthetic one-hour duplex clock drift at ±1000 ppm with 16 ms feedback updates. These simulations use a modeled peripheral and do not simulate all electrical, bus scheduling, OS-driver or interrupt behaviors. DSP routing tests use deterministic DSP stand-ins; they prove isolation and connection points, not sound quality. `tools/test-grid.sh` covers the existing sequencer regressions. `tools/build-usb.sh` creates the ARM binary and package; `tools/verify-usb-package.py` verifies file hashes and source identity.

Primary references: [USB Audio 1.0](https://www.usb.org/sites/default/files/audio10.pdf), [USB MIDI 1.0](https://www.usb.org/sites/default/files/midi10.pdf), [ST USB peripheral driver](https://github.com/STMicroelectronics/stm32h7xx-hal-driver/blob/master/Src/stm32h7xx_ll_usb.c), and the vendored source changes in this branch. The accompanying research audit records why this stack needs explicit feedback, distinct endpoints and retirement handling.
