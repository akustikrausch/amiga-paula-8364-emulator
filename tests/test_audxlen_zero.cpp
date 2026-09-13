// test_audxlen_zero.cpp: an audxlen of 0 plays 65536 words, like the chip.
// ----------------------------------------------------------------------------
// audxlen counts 16-bit words, and paula reads a 0 as the longest length there
// is: the channel plays 65536 words (131072 bytes of chip ram) before it
// reloads its pointer and length. real replayers lean on it: brian postma's
// soundmon writes a loop length of 0 and reads on through the samples stored
// behind it. a 16-bit counter loaded straight with the 0 reloads after a single
// word instead: the channel sits on a one-word loop (a dc level) and raises its
// audio interrupt on every word.
//
// the rig turns one source sample into exactly one output frame: colour clock
// and output rate are the same power of two and the period is 1, so the phase
// accumulator steps by exactly 1.0. every chip address paula reads is
// recorded, so the checks see WHERE the channel reads, not only what comes out.
//
//   len0     a length of 0 reads on past its first word, also after the channel
//            is turned off and on again, and starts without the extra interrupt
//            a counter loaded with 0 raises at once
//   len1     a length of 1 still loops its single word (control)
//   wrap     a length of 0 wraps after 65536 words, not 65535, and the reload
//            loads 65536 again
//   65535    a length of 0xffff wraps one word earlier (0 is not 0xffff)
//   setLoop  a loop latched with length 0 reads on when it takes over
//
// plain c++17, links paula and nothing else. exit code 0 = all checks passed.

#include "paula.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <vector>

using namespace amiga;

namespace {

int g_failed = 0;
int g_total  = 0;
#define EXPECT(cond, msg) do {                                            \
    ++g_total;                                                            \
    if (!(cond)) {                                                        \
        std::fprintf(stderr, "  FAIL: %s (line %d)\n", msg, __LINE__);    \
        ++g_failed;                                                       \
    }                                                                     \
} while (0)

constexpr double   kRate      = 65536.0;          // colour clock and output rate
constexpr uint32_t kFullWords = 65536;            // what the chip plays for audxlen 0
constexpr uint32_t kFullBytes = 2 * kFullWords;
constexpr uint32_t kLoc       = 0x2000;           // where the long samples start

struct Rig {
    Paula paula{kRate};
    std::vector<int8_t>   chip;
    std::vector<uint32_t> reads;       // every chip address paula read, in order
    std::vector<size_t>   irqAtRead;   // reads.size() at each channel-0 audio interrupt

    explicit Rig(size_t bytes) : chip(bytes, 0) {
        paula.setStereoSeparation(1.0f);   // channel 0 alone on the left output
        paula.setReadByteCallback([this](uint32_t a) -> uint8_t {
            reads.push_back(a);
            return a < chip.size() ? uint8_t(chip[a]) : uint8_t(0);
        });
        paula.setInterruptCallback([this](uint16_t bits) {
            if (bits & kIntAud0) irqAtRead.push_back(reads.size());
        });
    }
    Rig(const Rig&) = delete;
    Rig& operator=(const Rig&) = delete;

    void reg(uint32_t r, uint16_t v) { paula.writeRegister16(kCustomBase + r, v); }

    // program channel 0 the way a replayer does and turn it on.
    void start(uint32_t loc, uint16_t len) {
        reg(kReg_AUDxLCH(0), uint16_t(loc >> 16));
        reg(kReg_AUDxLCL(0), uint16_t(loc & 0xffff));
        reg(kReg_AUDxLEN(0), len);
        reg(kReg_AUDxPER(0), 1);
        reg(kReg_AUDxVOL(0), 64);
        on();
    }
    void on()  { reg(kReg_DMACON, uint16_t(kDmaConSetClr | kDmaConMaster | kDmaConAud0)); }
    void off() { reg(kReg_DMACON, kDmaConAud0); }

    // frame f of the left output carries the byte paula fetched at frame f - 1.
    std::vector<float> render(size_t frames) {
        std::vector<float> l(frames), r(frames);
        paula.render(l.data(), r.data(), int(frames), kRate);
        return l;
    }
};

bool near(double a, double b) { return std::fabs(a - b) <= 1e-4; }

// reads `from` .. `from + count - 1` walk the addresses start, start + 1, ...
bool readsRun(const std::vector<uint32_t>& reads, size_t from, uint32_t start, size_t count) {
    if (reads.size() < from + count) return false;
    for (size_t i = 0; i < count; ++i)
        if (reads[from + i] != start + uint32_t(i)) return false;
    return true;
}

// interrupts raised after the first read and before `reads` addresses were read.
size_t interruptsBefore(const std::vector<size_t>& irqs, size_t reads) {
    size_t n = 0;
    for (size_t at : irqs) n += at > 0 && at < reads;
    return n;
}

bool interruptAt(const std::vector<size_t>& irqs, size_t reads) {
    for (size_t at : irqs)
        if (at == reads) return true;
    return false;
}

// audio interrupts at the two moments a channel starts: turned on, and turned
// off and on again after 32 words. whatever the model raises there, a length
// of 0 has to raise the same as any other length; a counter started at 0
// reloads before its first word and raises one more each time.
size_t startInterrupts(uint16_t len) {
    Rig rig(256);
    rig.start(0, len);
    rig.render(64);
    rig.off();
    rig.on();
    const size_t restart = rig.reads.size();
    rig.render(64);
    size_t n = 0;
    for (size_t at : rig.irqAtRead) n += at == 0 || at == restart;
    return n;
}

} // namespace

int main() {
    std::printf("test_audxlen_zero: audxlen 0 plays 65536 words\n");

    // --- len0: reads on past the first word ------------------------------------
    // word 0 is +100, everything behind it -100.
    {
        Rig rig(8192);
        rig.chip[0] = rig.chip[1] = 100;
        for (size_t i = 2; i < rig.chip.size(); ++i) rig.chip[i] = -100;
        rig.start(0, 0);
        const auto out = rig.render(4096);
        EXPECT(readsRun(rig.reads, 0, 0, 4096),
               "a length of 0 reads on through chip ram past the first word");
        EXPECT(out[1] > 0.0f && out[2] > 0.0f, "a length of 0 plays its first word (+100)");
        size_t below = 0;
        for (size_t f = 3; f < out.size(); ++f) below += out[f] < 0.0f;
        EXPECT(below == out.size() - 3, "a length of 0 goes on into the -100 behind its first word");
        EXPECT(interruptsBefore(rig.irqAtRead, rig.reads.size() + 1) == 0,
               "a length of 0 raises no reload interrupt within its first 2048 words");

        // off and on again: the channel starts over at its pointer and reads on.
        rig.off();
        rig.on();
        const size_t before = rig.reads.size();
        rig.render(64);
        EXPECT(readsRun(rig.reads, before, 0, 64),
               "turned off and on again, a length of 0 starts over and reads on past the first word");
    }
    EXPECT(startInterrupts(0) == startInterrupts(1),
           "a length of 0 starts like any other length: no extra interrupt when the channel is turned on");

    // --- len1: the control -------------------------------------------------------
    {
        Rig rig(8192);
        rig.chip[0] = rig.chip[1] = 100;
        for (size_t i = 2; i < rig.chip.size(); ++i) rig.chip[i] = -100;
        rig.start(0, 1);
        const auto out = rig.render(4096);
        bool loops = rig.reads.size() == 4096;
        for (size_t i = 0; loops && i < rig.reads.size(); ++i) loops = rig.reads[i] == uint32_t(i & 1);
        EXPECT(loops, "a length of 1 loops its single word");
        bool above = true;
        for (size_t f = 1; f < out.size(); ++f) above = above && out[f] > 0.0f;
        EXPECT(above, "a length of 1 never reaches the -100 behind its word");
        bool everyWord = rig.irqAtRead.size() >= 2047;
        for (size_t i = 1; everyWord && i < rig.irqAtRead.size(); ++i)
            everyWord = rig.irqAtRead[i] - rig.irqAtRead[i - 1] == 2;
        EXPECT(everyWord, "a length of 1 raises its audio interrupt one word apart");
    }

    // --- wrap: 65536 words, then 65536 again ----------------------------------------
    // word 0 is +100, the body -100, word 65535 (the last one played) +50, the
    // word behind the loop +20 and must never sound.
    const auto fullPattern = [](std::vector<int8_t>& m) {
        m[kLoc] = m[kLoc + 1] = 100;
        for (uint32_t i = 2; i < kFullBytes - 2; ++i) m[kLoc + i] = -100;
        m[kLoc + kFullBytes - 2] = m[kLoc + kFullBytes - 1] = 50;
        m[kLoc + kFullBytes] = m[kLoc + kFullBytes + 1] = 20;
        for (uint32_t i = kFullBytes + 2; i < kFullBytes + 64; ++i) m[kLoc + i] = -20;
    };
    {
        Rig rig(kLoc + kFullBytes + 64);
        fullPattern(rig.chip);
        rig.start(kLoc, 0);
        const auto out = rig.render(kFullBytes + 8);
        EXPECT(readsRun(rig.reads, 0, kLoc, kFullBytes),
               "a length of 0 plays 65536 words, all 131072 bytes");
        EXPECT(readsRun(rig.reads, kFullBytes, kLoc, 8),
               "after 65536 words the reload starts over at the pointer and loads 65536 words again");
        EXPECT(near(out[kFullBytes - 1], 50.0 / 128.0) && near(out[kFullBytes], 50.0 / 128.0),
               "the 65536th word sounds");
        EXPECT(near(out[kFullBytes + 1], 100.0 / 128.0) && near(out[kFullBytes + 2], 100.0 / 128.0)
                   && out[kFullBytes + 3] < 0.0f,
               "then the first word again, never the word behind the loop");
        EXPECT(interruptsBefore(rig.irqAtRead, kFullBytes) == 0 && interruptAt(rig.irqAtRead, kFullBytes),
               "the reload interrupt comes after 65536 words, not before");
    }

    // --- 65535: the longest non-zero length -------------------------------------------
    {
        Rig rig(kLoc + kFullBytes + 64);
        fullPattern(rig.chip);
        rig.start(kLoc, 0xffff);
        const auto out = rig.render(kFullBytes);
        EXPECT(readsRun(rig.reads, 0, kLoc, kFullBytes - 2) && readsRun(rig.reads, kFullBytes - 2, kLoc, 2),
               "a length of 65535 wraps one word before a length of 0");
        EXPECT(near(out[kFullBytes - 1], 100.0 / 128.0), "a length of 65535 never plays word 65535");
    }

    // --- setLoop: a latched loop of length 0 ----------------------------------------------
    // a two-word one-shot at 0x100 hands over to a loop at 0x4000 latched with
    // length 0. only the reload loads that length.
    {
        constexpr uint32_t kShot = 0x100, kLoop = 0x4000;
        Rig rig(kLoop + 8192);
        for (uint32_t i = 0; i < 4; ++i) rig.chip[kShot + i] = 60;
        for (uint32_t i = 0; i < 8192; ++i) rig.chip[kLoop + i] = int8_t(i < 2 ? 90 : -90);
        rig.start(kShot, 2);
        rig.paula.setLoop(0, kLoop, 0);
        const auto out = rig.render(4 + 4096);
        EXPECT(readsRun(rig.reads, 0, kShot, 4), "the one-shot plays its two words");
        EXPECT(readsRun(rig.reads, 4, kLoop, 4096),
               "a loop latched with length 0 reads on from its start instead of looping one word");
        EXPECT(out[4 + 4096 - 1] < 0.0f, "the latched loop reaches the -90 behind its first word");
        EXPECT(interruptAt(rig.irqAtRead, 4) && interruptsBefore(rig.irqAtRead, 4 + 4096 + 1) == 1,
               "one interrupt when the loop takes over, none within its first 2048 words");
        EXPECT(rig.paula.channelState(0).lenWords == 0, "the channel state reports audxlen as written");
    }

    std::printf(g_failed ? "  %d of %d failed\n" : "  all %d of %d passed\n",
                g_failed ? g_failed : g_total, g_total);
    return g_failed ? 1 : 0;
}
