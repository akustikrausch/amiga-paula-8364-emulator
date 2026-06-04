// paula.h -- amiga paula 8364 audio chip emulator
// ----------------------------------------------------------------------------
// clean-room. built from the public hardware docs only:
//   - amiga hardware reference manual, 3rd ed. (addison-wesley)
//   - http://amigadev.elowar.com/read/ADCD_2.1/Hardware_Manual_guide/
//   - en.wikipedia.org/wiki/Original_Chip_Set#Paula
// not a single line lifted from any other emulator.
//
// paula is the 4-channel sample-dma sound chip in every amiga (ocs/ecs/aga).
// each channel carries its own pointer, length, period (= colour-clock / rate)
// and volume. 8-bit signed samples, hardware loop through the audxlen reload.
// channels 0+3 go to the left output, 1+2 to the right -- the amiga way.
//
// scope is precision-of-behaviour: enough to drive real amiga music replayers
// at a modern output rate. not cycle-accurate silicon. modelled:
//   * dma fetch + 8-bit sample -> output at (colour-clock / period) rate
//     (pal 3.546895 mhz, ntsc 3.579545 mhz)
//   * instant period writes (audxper)
//   * audxlen reload at end-of-sample -> loop, the way trackers that don't
//     toggle dma per row expect
//   * volume 0..64 -> linear gain
//   * dmacon master bit (dmaen) + per-channel audxen bits
// left out on purpose: audio modulation (adkcon amod), blitter-fed samples,
// the analog reconstruction filter -- hand bandwidth to your own resampler.
//
// upsampling is linear interpolation from paula's native rate to your output
// rate. cheap and clean enough -- the source is 8-bit and already band-limited.
//
// MIT. see LICENSE.

#pragma once

#include <array>
#include <cstdint>
#include <functional>

namespace amiga {

// colour-clock used as the divisor in paula's period->hz formula:
//   out_hz = clock_hz / period
// pal machines (the demoscene standard): 3.546895 mhz. ntsc: 3.579545 mhz.
inline constexpr double kPalColorClockHz  = 3'546'895.0;
inline constexpr double kNtscColorClockHz = 3'579'545.0;

inline constexpr int kPaulaChannels = 4;  // ch0+3 -> L, ch1+2 -> R

// custom-chip register offsets (relative to the $dff000 base). replayers hit
// these directly with move.w / move.l.
inline constexpr uint32_t kCustomBase = 0x00DFF000u;
inline constexpr uint32_t kReg_DMACON  = 0x096u;
inline constexpr uint32_t kReg_DMACONR = 0x002u;
inline constexpr uint32_t kReg_INTREQ  = 0x09Cu;
inline constexpr uint32_t kReg_INTREQR = 0x01Eu;
inline constexpr uint32_t kReg_INTENA  = 0x09Au;
inline constexpr uint32_t kReg_INTENAR = 0x01Cu;
inline constexpr uint32_t kReg_ADKCON  = 0x09Eu;
inline constexpr uint32_t kReg_ADKCONR = 0x010u;

// per-channel block at $dff0a0 + 16*ch (ch = 0..3).
inline constexpr uint32_t kRegAudBase(int ch) {
    return 0x0A0u + uint32_t(ch) * 0x10u;
}
inline constexpr uint32_t kReg_AUDxLCH(int ch) { return kRegAudBase(ch) + 0x0u; }
inline constexpr uint32_t kReg_AUDxLCL(int ch) { return kRegAudBase(ch) + 0x2u; }
inline constexpr uint32_t kReg_AUDxLEN(int ch) { return kRegAudBase(ch) + 0x4u; }
inline constexpr uint32_t kReg_AUDxPER(int ch) { return kRegAudBase(ch) + 0x6u; }
inline constexpr uint32_t kReg_AUDxVOL(int ch) { return kRegAudBase(ch) + 0x8u; }
inline constexpr uint32_t kReg_AUDxDAT(int ch) { return kRegAudBase(ch) + 0xAu; }

// dmacon bits.
inline constexpr uint16_t kDmaConSetClr = 0x8000u;
inline constexpr uint16_t kDmaConAud0   = 0x0001u;
inline constexpr uint16_t kDmaConAud1   = 0x0002u;
inline constexpr uint16_t kDmaConAud2   = 0x0004u;
inline constexpr uint16_t kDmaConAud3   = 0x0008u;
inline constexpr uint16_t kDmaConAllAud = 0x000Fu;
inline constexpr uint16_t kDmaConMaster = 0x0200u;

// intreq / intena audio-finished bits.
inline constexpr uint16_t kIntAud0 = 0x0080u;
inline constexpr uint16_t kIntAud1 = 0x0100u;
inline constexpr uint16_t kIntAud2 = 0x0200u;
inline constexpr uint16_t kIntAud3 = 0x0400u;

// you give paula a way to read its sample stream out of your "chip ram".
// it never needs to know how your memory is laid out.
using ReadByteFn = std::function<uint8_t(uint32_t address)>;

// paula raises intreq bits when a channel finishes its audxlen words (loop
// point). wire it to a cia timer or post it into your cpu's irq mask -- your
// call.
using InterruptFn = std::function<void(uint16_t intBitMask)>;

class Paula {
public:
    explicit Paula(double clockHz = kPalColorClockHz) noexcept;

    // hook up your memory + interrupt callbacks. set the read callback before
    // render() or you get silence.
    void setReadByteCallback(ReadByteFn fn) { read_ = std::move(fn); }
    void setInterruptCallback(InterruptFn fn) { onInterrupt_ = std::move(fn); }

    // wipe all channel state. call on load + hard reset.
    void reset() noexcept;

    // a 16-bit register write from your cpu. addr is the full 24-bit address
    // (e.g. $dff096); we mask the high bits and dispatch. word writes only --
    // the real chip ignores byte writes to these registers anyway.
    void writeRegister16(uint32_t addr, uint16_t value) noexcept;

    // read back dmaconr / intreqr / intenar / adkconr (+ vhposr/vpos, see .cpp).
    uint16_t readRegister16(uint32_t addr) const noexcept;

    // render `frames` stereo frames at `outSr` hz into two mono float buffers.
    // ch0+3 sum into outL, ch1+2 into outR. range is roughly [-1, +1] for
    // typical 8-bit samples at full volume -- no clip/limit, your mix bus owns
    // that.
    void render(float* outL, float* outR, int frames, double outSr) noexcept;

    void setClockHz(double hz) noexcept { clockHz_ = hz; }
    double clockHz() const noexcept { return clockHz_; }

    // peek at a channel -- handy for tests / vu meters.
    struct ChannelState {
        uint32_t locPtr;
        uint16_t lenWords;
        uint16_t periodTicks;
        uint16_t volume;
        bool     dmaEnabled;
    };
    ChannelState channelState(int ch) const noexcept;

    // raw dmacon mirror (master bit $0200 + per-channel bits 0..3).
    uint16_t rawDmaCon() const noexcept { return dmaCon_; }

private:
    struct Channel {
        uint32_t locPtrLatched = 0;   // last full audxlch:audxlcl write
        uint16_t lenWordsLatched = 0;
        uint16_t period   = 1;        // never zero
        uint16_t volume   = 0;
        uint32_t curPtr   = 0;        // live byte ptr into the sample
        uint16_t curWordsLeft = 0;    // words left until reload
        uint8_t  curSampleL = 0;      // 2 bytes per word
        uint8_t  curSampleH = 0;
        bool     onLowByte = true;
        double   phase    = 0.0;      // 0..1 sub-sample accumulator
        bool     dmaWantsRestart = true;
        int8_t   curOut   = 0;
        int8_t   nextOut  = 0;
    };

    void serviceChannel_(Channel& c, int chIdx,
                         double samplesPerOutFrame,
                         float& mixOut) noexcept;
    void advanceOneSourceSample_(Channel& c, int chIdx) noexcept;

    double      clockHz_;
    ReadByteFn  read_;
    InterruptFn onInterrupt_;
    std::array<Channel, kPaulaChannels> channels_;
    uint16_t    dmaCon_  = 0;
    uint16_t    intReq_  = 0;
    uint16_t    intEna_  = 0;
    uint16_t    adkCon_  = 0;
    // synthetic vhposr/vpos counter -- see readRegister16 in the .cpp.
    mutable uint16_t vhposrCounter_ = 0;
};

} // namespace amiga
