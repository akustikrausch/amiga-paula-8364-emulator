# changelog

all notable changes to the paula 8364 emulator. dates are `yyyy-mm-dd`.

## 2026-06-14 — playback fidelity

new behaviour, all additive — existing callers keep working unchanged (the new
knobs default to sensible values, and the only default that *changed* is the
resampler, see below).

- **`setLoop(ch, loc, lenWords)`** — latch a loop region a channel reloads on
  its next block-wrap without disturbing the live playhead. lets a one-shot
  intro play once and the body loop after (the amiga two-stage
  audxlc+audxlen-while-playing program). sustained synth voices need this; a
  tracker that toggles dma per row does not.
- **selectable resampling** via `setInterpolation(Interp::Nearest | Linear)`.
  **the default is now `Nearest`** (zero-order hold) — it keeps and aliases the
  top octave, which is the bright, authentic amiga character. `Linear` (the
  previous behaviour) is still one call away for a softer, less-aliased image.
- **anti-click gain glide** — channel volume now slews toward its target over
  ~2 ms instead of stepping. note-ons, envelope steps and `audxvol` writes no
  longer zipper. always on; inaudible on steady volumes.
- **`setStereoSeparation(0..1)`** — cross-blend the hard amiga ch0+3/1+2
  ping-pong toward centre. `1.0` = the original hard-pan, lower = a natural
  image on headphones. default `0.85`.
- **`setChannelMuted(ch, bool)`** — mute a channel for stems / soloing. the
  channel keeps its full dma + gain state running; only its mix contribution
  drops, so unmuting is glitch-free. default all-audible (a no-op).

these all come from the shipping FXChainPlayer build, where the same chip drives
the PreTracker corpus and a clean-room musicline engine.

## 2026-06-04 — initial release

clean-room MIT paula 8364: 4-channel sample dma, period→pitch, audxlen loop
reload, dmacon master + per-channel enables, ch0+3/1+2 stereo split, intreq on
loop point, synthetic vhposr/vpos for replayers that busy-wait on the beam, and
linear-interpolated upsampling to any output rate. one header, one source, no
dependencies.
