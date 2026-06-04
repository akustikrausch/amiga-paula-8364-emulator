```
/===================================================================\
   _   _  ___   _ ___ _____ ___ _  _____    _  _   _ ___  ___ _  _
  /_\ | |/ / | | / __|_   _|_ _| |/ / _ \  /_\| | | / __|/ __| || |
 / _ \| ' <| |_| \__ \ | |  | || ' <|   / / _ \ |_| \__ \ (__| __ |
/_/ \_\_|\_\\___/|___/ |_| |___|_|\_\_|_\/_/ \_\___/|___/\___|_||_|
\===================================================================/
```

**Akustikrausch (Andreas Wendorf)**

# amiga paula 8364 emulator

a tiny, clean-room emulator of **paula** — the 4-channel sample-dma sound chip
in every commodore amiga (ocs/ecs/aga). pure c++, no dependencies, one header
and one source file. MIT.

you feed it a read callback over your own "chip ram", poke the same `$dff0xx`
registers a real replayer pokes, and pull stereo float audio out. that's the
whole api.

```cpp
amiga::Paula paula;                       // pal colour-clock by default
paula.setReadByteCallback(myChipRamRead); // how paula reads samples

paula.writeRegister16(0xDFF0A0, hi);      // audxlch  } sample pointer
paula.writeRegister16(0xDFF0A2, lo);      // audxlcl  }
paula.writeRegister16(0xDFF0A4, len);     // audxlen  (in words)
paula.writeRegister16(0xDFF0A6, period);  // audxper  (colourclock / rate)
paula.writeRegister16(0xDFF0A8, vol);     // audxvol  (0..64)
paula.writeRegister16(0xDFF096, 0x8201);  // dmacon: master + ch0 on

paula.render(outL, outR, frames, 48000);  // stereo float, ch0+3 L, 1+2 R
```

## why this exists

if you want to play amiga music — tracker replayers, custom 68k players, your
own chiptune engine — you usually reach for **UADE**. uade is great. uade is
also **GPL**, and it drags in a whole UAE-derived amiga. that's a non-starter
for a lot of projects (anything permissively licensed, anything commercial,
anything that just wants the sound chip and nothing else).

this is the other option: just paula, **MIT**, ~400 lines, zero deps. drop the
two files in and go. no full machine, no graphics, no disk — the sound chip,
done properly, and out of your way.

it's deliberately *not* a cycle-accurate silicon model. it nails the behaviour
that actually matters for music: 8-bit sample dma, period→pitch, the audxlen
loop reload, the dmacon master + per-channel enables, and the ch0+3/1+2 stereo
split. linear-interpolated up to whatever output rate you ask for.

## proven, not a toy

this is the exact paula that ships inside **FXChainPlayer**, where it does real
work two different ways:

- it drives the amiga demoscene **PreTracker** (`.prt`) corpus — the original
  68k replayer runs on a 68000 core, writes `$dff0xx`, and *this* chip turns it
  into sound. plays the pouët / demozoo `.prt` catalogue.
- it's driven straight from c++ by a clean-room **musicline editor** engine,
  with no cpu emulation at all — same chip, fed by hand.

so the audio path has been hammered on by real material from both sides. see
it running in a shipping player here:

→ **https://github.com/akustikrausch/FXChainPlayer-Releases**

## build the example

```sh
g++ -std=c++17 -O2 -I. example/example.cpp paula.cpp -o paulatest && ./paulatest
```

writes `paula.wav` — a little 4-voice chord. that's all the integration there
is: a read callback, a few register writes, `render()`.

## the registers it understands

| reg            | offset    | what |
|----------------|-----------|------|
| `audxlch/lcl`  | `$a0/$a2` | sample start pointer (per channel, +$10 each) |
| `audxlen`      | `$a4`     | length in words (loop length) |
| `audxper`      | `$a6`     | period — pitch = colourclock / period |
| `audxvol`      | `$a8`     | volume 0..64 |
| `dmacon`       | `$96`     | bit 9 master, bits 0..3 per-channel enable |
| `intreq/ena`   | `$9c/$9a` | audio-finished irq bits (loop point) |
| `vhposr/vpos`  | `$06/$04` | synthetic beam counter for replayers that poll it |

period→hz uses the colour clock: pal `3.546895 MHz`, ntsc `3.579545 MHz`
(`paula.setClockHz()` to switch).

## what it doesn't do

audio modulation (adkcon amod), blitter-fed samples, and the analog
reconstruction filter — out of scope on purpose. feed `render()`'s output to
your own resampler / filter if you want the soft-amiga lowpass.

## license

MIT. do whatever, keep the notice. © 2026 akustikrausch / andreas wendorf.

clean-room from the amiga hardware reference manual + the usual public docs —
not a line from any other emulator.

---

*greetz to everyone still making the little chip sing.*
