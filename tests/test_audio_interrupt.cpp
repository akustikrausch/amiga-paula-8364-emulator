// test_audio_interrupt.cpp: paula's audio interrupt at the channel start and
// with the last word of a block, like the chip.
// ----------------------------------------------------------------------------
// the amiga hardware reference manual (chapter 5, "the audio state machine"
// and "joining tones") has the audio interrupt of a dma channel follow the
// moment the channel has read audxlc and audxlen into its back-up registers.
// that happens when dma is turned on, and again when the length counter
// finishes (counts to one), just as the last word of the block starts its
// output: the pointer restarts and the length counter reloads there. software
// joins tones by writing the next segment's location and length in the
// interrupt, and the next restart takes it.
//
// the rig turns one source sample into exactly one output frame (colour clock
// = output rate, period 1), records every chip read, and renders one frame
// per call, so each interrupt knows its output frame and whether a register
// write or the rendering requested it.
//
//   start    turning a channel on requests one interrupt at once, off and on
//            requests another, a running channel is not restarted, and a
//            channel bit without the master bit starts nothing
//   timing   the restart interrupt comes as the last word of the block begins,
//            not a word later
//   chain    segments written in the interrupt play once each: a, b, c, c...
//            (by register writes and by setLoop())
//   latch    a location written while the last word already plays waits for
//            the following restart
//   len0     a block of 65536 words requests it with its 65536th word
//
// plain c++17, links paula and nothing else. exit code 0 = all checks passed.

#include "paula.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <functional>
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

struct Seg {
    uint32_t loc;
    uint16_t len;
};

struct Irq {
    size_t read;       // chip reads done when it was requested
    long   frame;      // output frame being rendered, -1 for a register write
};

struct Rig {
    Paula paula{kRate};
    std::vector<int8_t>   chip;
    std::vector<uint32_t> reads;        // every chip address paula read, in order
    std::vector<Irq>      irqs;         // channel-0 audio interrupts
    std::function<void()> onIrq;        // what the "software" does in the interrupt
    long                  frame = -1;
    size_t                framesDone = 0;

    explicit Rig(size_t bytes) : chip(bytes, 0) {
        paula.setStereoSeparation(1.0f);
        paula.setReadByteCallback([this](uint32_t a) -> uint8_t {
            reads.push_back(a);
            return a < chip.size() ? uint8_t(chip[a]) : uint8_t(0);
        });
        paula.setInterruptCallback([this](uint16_t bits) {
            if (!(bits & kIntAud0)) return;
            irqs.push_back({reads.size(), frame});
            if (onIrq) onIrq();
        });
    }
    Rig(const Rig&) = delete;
    Rig& operator=(const Rig&) = delete;

    void reg(uint32_t r, uint16_t v) { paula.writeRegister16(kCustomBase + r, v); }

    // audxlc and audxlen of channel 0, the way a replayer writes them.
    void point(uint32_t loc, uint16_t len) {
        reg(kReg_AUDxLCH(0), uint16_t(loc >> 16));
        reg(kReg_AUDxLCL(0), uint16_t(loc & 0xffff));
        reg(kReg_AUDxLEN(0), len);
    }
    void start(uint32_t loc, uint16_t len) {
        point(loc, len);
        reg(kReg_AUDxPER(0), 1);
        reg(kReg_AUDxVOL(0), 64);
        on();
    }
    void on()  { reg(kReg_DMACON, uint16_t(kDmaConSetClr | kDmaConMaster | kDmaConAud0)); }
    void off() { reg(kReg_DMACON, kDmaConAud0); }

    // one output frame per call, so an interrupt knows the frame it came in.
    void render(size_t frames) {
        float l = 0.0f, r = 0.0f;
        for (size_t i = 0; i < frames; ++i) {
            frame = long(framesDone++);
            paula.render(&l, &r, 1, kRate);
        }
        frame = -1;
    }

    size_t fromWrites() const {
        size_t n = 0;
        for (const Irq& q : irqs) n += q.frame < 0;
        return n;
    }
    std::vector<long> renderFrames() const {
        std::vector<long> f;
        for (const Irq& q : irqs)
            if (q.frame >= 0) f.push_back(q.frame);
        return f;
    }
};

// reads `from` .. `from + count - 1` walk the addresses start, start + 1, ...
bool readsRun(const std::vector<uint32_t>& reads, size_t from, uint32_t start, size_t count) {
    if (reads.size() < from + count) return false;
    for (size_t i = 0; i < count; ++i)
        if (reads[from + i] != start + uint32_t(i)) return false;
    return true;
}

} // namespace

int main() {
    std::printf("test_audio_interrupt: the audio interrupt at the start and at the last word\n");

    // --- start ---------------------------------------------------------------------
    {
        Rig rig(64);
        rig.start(0x10, 2);
        EXPECT(rig.irqs.size() == 1 && rig.irqs[0].frame < 0 && rig.irqs[0].read == 0,
               "turning a channel on requests its audio interrupt at once, before a sample is read");
        EXPECT((rig.paula.readRegister16(kCustomBase + kReg_INTREQR) & kIntAud0) != 0,
               "the start request shows in intreqr");
        rig.render(1);
        rig.reg(kReg_DMACON, uint16_t(kDmaConSetClr | kDmaConMaster | kDmaConAud1));
        EXPECT(rig.fromWrites() == 1, "turning another channel on does not restart a running one");
        rig.off();
        rig.on();
        EXPECT(rig.fromWrites() == 2, "turned off and on again, the channel requests the interrupt again");
    }
    {
        Rig rig(64);
        rig.point(0x10, 2);
        rig.reg(kReg_AUDxPER(0), 1);
        rig.reg(kReg_DMACON, uint16_t(kDmaConSetClr | kDmaConAud0));
        EXPECT(rig.irqs.empty(), "a channel bit without the master bit starts nothing and requests nothing");
        rig.reg(kReg_DMACON, uint16_t(kDmaConSetClr | kDmaConMaster));
        EXPECT(rig.irqs.size() == 1 && rig.irqs[0].frame < 0,
               "the master bit starts the channel and requests its interrupt");
    }

    // --- timing: with the last word, not after it ----------------------------------
    // a block of two words: word 1 begins at frame 0, word 2 (the last) at frame
    // 2, the restart at frame 4. the interrupt belongs to frames 2, 6, 10.
    {
        Rig rig(64);
        rig.start(0x10, 2);
        rig.render(12);
        const auto f = rig.renderFrames();
        EXPECT(f.size() == 3 && f[0] == 2 && f[1] == 6 && f[2] == 10,
               "the restart interrupt comes as the last word of the block begins (frames 2, 6, 10), not a word later");
        EXPECT(readsRun(rig.reads, 0, 0x10, 4) && readsRun(rig.reads, 4, 0x10, 4) && readsRun(rig.reads, 8, 0x10, 4),
               "the block itself still plays whole and restarts after its last word");
    }

    // --- chain: joining tones in the interrupt ---------------------------------------
    // a: 2 words at 0x100, b: 3 words at 0x200, c: 1 word at 0x300.
    {
        Rig rig(0x400);
        std::vector<Seg> next{{0x200, 3}, {0x300, 1}};
        rig.onIrq = [&] {
            if (next.empty()) return;
            rig.point(next.front().loc, next.front().len);
            next.erase(next.begin());
        };
        rig.start(0x100, 2);
        rig.render(2 * (2 + 3 + 1 + 1));
        EXPECT(readsRun(rig.reads, 0, 0x100, 4) && readsRun(rig.reads, 4, 0x200, 6)
                   && readsRun(rig.reads, 10, 0x300, 2) && readsRun(rig.reads, 12, 0x300, 2),
               "segments written in the interrupt play one after the other, each once: a, b, c, then c loops");
    }
    {
        Rig rig(0x400);
        std::vector<Seg> next{{0x200, 3}, {0x300, 1}};
        rig.onIrq = [&] {
            if (next.empty()) return;
            rig.paula.setLoop(0, next.front().loc, next.front().len);
            next.erase(next.begin());
        };
        rig.start(0x100, 2);
        rig.render(2 * (2 + 3 + 1 + 1));
        EXPECT(readsRun(rig.reads, 0, 0x100, 4) && readsRun(rig.reads, 4, 0x200, 6)
                   && readsRun(rig.reads, 10, 0x300, 2) && readsRun(rig.reads, 12, 0x300, 2),
               "setLoop() from the interrupt joins the segments the same way");
    }

    // --- latch: too late for this restart ------------------------------------------------
    {
        Rig rig(0x400);
        rig.start(0x100, 2);
        rig.render(3);                       // the last word of a began at frame 2
        rig.point(0x200, 1);                 // written while it plays
        rig.render(1 + 4 + 2);
        EXPECT(readsRun(rig.reads, 0, 0x100, 4) && readsRun(rig.reads, 4, 0x100, 4)
                   && readsRun(rig.reads, 8, 0x200, 2),
               "a location written while the last word already plays is taken at the following restart: a plays once more");
    }

    // --- len0: 65536 words ------------------------------------------------------------------
    {
        Rig rig(kFullBytes + 64);
        rig.start(0x10, 0);
        rig.render(kFullBytes + 4);
        const auto f = rig.renderFrames();
        EXPECT(rig.fromWrites() == 1 && !f.empty() && f[0] == long(kFullBytes - 2),
               "a block of 65536 words requests the interrupt at its start and with its 65536th word");
    }

    std::printf(g_failed ? "  %d of %d failed\n" : "  all %d of %d passed\n",
                g_failed ? g_failed : g_total, g_total);
    return g_failed ? 1 : 0;
}
