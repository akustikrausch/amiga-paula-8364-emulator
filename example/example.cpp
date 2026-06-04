// example.cpp -- drive paula by hand and dump a wav.
//
// build (any c++17 compiler), from the repo root:
//   g++  -std=c++17 -O2 -I. example/example.cpp paula.cpp -o paulatest
//   cl   /std:c++17 /O2 /I. example\example.cpp paula.cpp /Fe:paulatest.exe
// run: ./paulatest   -> writes paula.wav (a little 4-voice chord)
//
// the whole point: you never touch silicon. you hand paula a read callback
// over your own "chip ram", poke the same $dff0xx registers a real replayer
// pokes, and pull stereo float out of render(). that's it.

#include "paula.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

int main() {
    // ---- our "chip ram": a handful of one-cycle 8-bit waveforms ----------
    // amiga samples are signed 8-bit. paula loops a channel over its audxlen
    // words forever, so a single-cycle wave at the right period = a held note.
    std::vector<int8_t> chip;
    chip.push_back(0); chip.push_back(0);   // addr 0 = silence guard

    auto putWave = [&](int len, auto gen) -> uint32_t {
        uint32_t addr = uint32_t(chip.size());
        for (int i = 0; i < len; ++i) chip.push_back(int8_t(gen(i, len)));
        if (chip.size() & 1) chip.push_back(0);  // pad to a word
        return addr;
    };

    const double TAU = 6.283185307179586;
    uint32_t saw  = putWave(64, [](int i, int n){ return (i * 256 / n) - 128; });
    uint32_t sine = putWave(64, [&](int i, int n){ return int(120.0 * std::sin(TAU * i / n)); });
    uint32_t sqr  = putWave(64, [](int i, int n){ return i < n/2 ? 100 : -100; });

    // ---- paula + the read callback over our chip ram ---------------------
    amiga::Paula paula(amiga::kPalColorClockHz);
    paula.setReadByteCallback([&](uint32_t a) -> uint8_t {
        return a < chip.size() ? uint8_t(chip[a]) : 0u;
    });

    // helper: program one channel exactly like a 68k replayer would.
    auto playChannel = [&](int ch, uint32_t addr, uint16_t period, uint16_t vol) {
        const uint32_t base = amiga::kCustomBase;
        paula.writeRegister16(base + amiga::kReg_AUDxLCH(ch), uint16_t(addr >> 16));
        paula.writeRegister16(base + amiga::kReg_AUDxLCL(ch), uint16_t(addr & 0xFFFF));
        paula.writeRegister16(base + amiga::kReg_AUDxLEN(ch), 32);   // 64 bytes / 2
        paula.writeRegister16(base + amiga::kReg_AUDxPER(ch), period);
        paula.writeRegister16(base + amiga::kReg_AUDxVOL(ch), vol);
    };

    // a little minor chord across the four voices. volumes kept modest so the
    // two-voices-per-side sum stays under unity -- no clip.
    playChannel(0, saw,  254, 30);   // ~a-3 -- left
    playChannel(1, sine, 320, 32);   //        right
    playChannel(2, sqr,  428, 30);   //        right
    playChannel(3, sine, 214, 28);   //        left

    // master dma on + all four channels.
    paula.writeRegister16(amiga::kCustomBase + amiga::kReg_DMACON,
                          uint16_t(amiga::kDmaConSetClr | amiga::kDmaConMaster |
                                   amiga::kDmaConAllAud));

    // ---- render 2 seconds @ 48k and dump a wav ---------------------------
    const int sr = 48000, secs = 2, frames = sr * secs;
    std::vector<float> L(frames), R(frames);
    paula.render(L.data(), R.data(), frames, double(sr));

    std::FILE* f = std::fopen("paula.wav", "wb");
    if (!f) { std::printf("can't open paula.wav\n"); return 1; }
    auto w32 = [&](uint32_t v){ std::fputc(v&255,f); std::fputc((v>>8)&255,f); std::fputc((v>>16)&255,f); std::fputc((v>>24)&255,f); };
    auto w16 = [&](uint16_t v){ std::fputc(v&255,f); std::fputc((v>>8)&255,f); };
    const uint32_t dataBytes = uint32_t(frames) * 2 * 2;  // stereo, 16-bit
    std::fputs("RIFF", f); w32(36 + dataBytes); std::fputs("WAVE", f);
    std::fputs("fmt ", f); w32(16); w16(1); w16(2); w32(sr); w32(sr*4); w16(4); w16(16);
    std::fputs("data", f); w32(dataBytes);
    for (int i = 0; i < frames; ++i) {
        auto clip = [](float x){ x = x < -1 ? -1 : (x > 1 ? 1 : x); return int16_t(x * 32767.0f); };
        w16(uint16_t(clip(L[i]))); w16(uint16_t(clip(R[i])));
    }
    std::fclose(f);

    float peak = 0.0f;
    for (int i = 0; i < frames; ++i) { peak = std::fmax(peak, std::fabs(L[i])); peak = std::fmax(peak, std::fabs(R[i])); }
    std::printf("wrote paula.wav -- %d frames, peak %.3f. give it a listen.\n", frames, peak);
    return 0;
}
