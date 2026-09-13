# changelog

all notable changes to the paula 8364 emulator. dates are `yyyy-mm-dd`.

## 2026-09-13: the audio interrupt at the start and with the last word

a fix, no api change.

- **turning a channel on now requests its audio interrupt.** the chip requests
  it as soon as a channel has read `audxlc` and `audxlen` into its back-up
  registers (hardware reference manual, "joining tones"), and that happens
  when dma starts the channel, not only at the restart. software that joins
  tones by writing the next segment in the interrupt used to get its first
  interrupt at the end of the first segment, after that segment had been read
  again, so the first segment played twice. now a, b, c play once each.
- **the restart happens as the last word of a block begins.** the length
  counter finishes at one, and the pointer and length for the restart are read
  right then, together with the interrupt. before, both came one word later,
  after the last word had played. a location written while the last word
  already plays now waits for the following restart, as on the chip.
- the interrupt goes out after the fetch has completed its sample state, so a
  handler may write the next segment or restart the channel safely.
- new: `tests/test_audio_interrupt.cpp` checks the start interrupt, its timing,
  tones joined by register writes and by `setLoop()`, and the late latch.
  `tests/test_audxlen_zero.cpp` now expects one interrupt per channel start.

found in FXChainPlayer, where the same chip runs 68k music that joins tones by
interrupt.

## 2026-09-13: audxlen 0 plays 65536 words

a fix, no api change.

- **a length of 0 is the longest length, not the shortest.** the chip reads an
  `audxlen` of 0 as 65536 words (131072 bytes of chip ram) before it reloads
  pointer and length. the emulator loaded that 0 straight into a 16-bit word
  counter, so the channel reloaded after a single word, sat on a one-word loop
  (a dc level) and raised its audio interrupt on every word. both places that
  load the counter (the dmacon start and the loop reload) now turn a 0 into
  65536, and the counter is wide enough to hold it. `setLoop(ch, loc, 0)` gets
  the full length its comment always promised.
- replayers that write a loop length of 0 now read on through the sample memory
  behind it, the way they do on an amiga. brian postma's soundmon is one of them.
- `channelState(ch).lenWords` still reports `audxlen` as written, so a 0 stays 0.
- new: `tests/test_audxlen_zero.cpp` records every chip address paula reads. a
  0 reads on, wraps after 65536 words (not 65535), reloads 65536 again and
  starts like any other length; a 1 still loops its single word. `ctest` runs
  it when this is the top-level project.

found in FXChainPlayer, where the same chip plays soundmon modules.

## 2026-06-14: playback fidelity

new behaviour, all additive: existing callers keep working unchanged (the new
knobs default to sensible values, and the only default that *changed* is the
resampler, see below).

- **`setLoop(ch, loc, lenWords)`**: latch a loop region a channel reloads on
  its next block-wrap without disturbing the live playhead. lets a one-shot
  intro play once and the body loop after (the amiga two-stage
  audxlc+audxlen-while-playing program). sustained synth voices need this; a
  tracker that toggles dma per row does not.
- **selectable resampling** via `setInterpolation(Interp::Nearest | Linear)`.
  **the default is now `Nearest`** (zero-order hold): it keeps and aliases the
  top octave, which is the bright, authentic amiga character. `Linear` (the
  previous behaviour) is still one call away for a softer, less-aliased image.
- **anti-click gain glide**: channel volume now slews toward its target over
  ~2 ms instead of stepping. note-ons, envelope steps and `audxvol` writes no
  longer zipper. always on; inaudible on steady volumes.
- **`setStereoSeparation(0..1)`**: cross-blend the hard amiga ch0+3/1+2
  ping-pong toward centre. `1.0` = the original hard-pan, lower = a natural
  image on headphones. default `0.85`.
- **`setChannelMuted(ch, bool)`**: mute a channel for stems / soloing. the
  channel keeps its full dma + gain state running; only its mix contribution
  drops, so unmuting is glitch-free. default all-audible (a no-op).

these all come from the shipping FXChainPlayer build, where the same chip drives
the PreTracker corpus and a clean-room musicline engine.

## 2026-06-04: initial release

clean-room MIT paula 8364: 4-channel sample dma, period to pitch, audxlen loop
reload, dmacon master + per-channel enables, ch0+3/1+2 stereo split, intreq on
loop point, synthetic vhposr/vpos for replayers that busy-wait on the beam, and
linear-interpolated upsampling to any output rate. one header, one source, no
dependencies.
