#include <cstdlib>
#include <cstdio>
#include "ax_internal.h"
#include <vector>
#include <cstring>

#if defined(RECOMP_PROJECT_FFCC)

std::vector<uint8_t>& FfccAramBuffer();

namespace AxDspHle {

void ReadPBGameCube(uint32_t addr, AXPBWii& pb) {
    std::memset(&pb, 0, sizeof(pb));
    
    pb.next_pb_hi = MixRead16(addr + 0x00);
    pb.next_pb_lo = MixRead16(addr + 0x02);
    pb.this_pb_hi = MixRead16(addr + 0x04);
    pb.this_pb_lo = MixRead16(addr + 0x06);
    pb.src_type = MixRead16(addr + 0x08);
    pb.coef_select = MixRead16(addr + 0x0A);
    pb.mixer_control_lo = MixRead16(addr + 0x0C);
    pb.running = MixRead16(addr + 0x0E);
    pb.is_stream = MixRead16(addr + 0x10);
    
    pb.mixer.main_left.volume = MixRead16(addr + 0x12);
    pb.mixer.main_left.volume_delta = MixRead16(addr + 0x14);
    pb.mixer.main_right.volume = MixRead16(addr + 0x16);
    pb.mixer.main_right.volume_delta = MixRead16(addr + 0x18);
    pb.mixer.auxA_left.volume = MixRead16(addr + 0x1A);
    pb.mixer.auxA_left.volume_delta = MixRead16(addr + 0x1C);
    pb.mixer.auxA_right.volume = MixRead16(addr + 0x1E);
    pb.mixer.auxA_right.volume_delta = MixRead16(addr + 0x20);
    pb.mixer.auxB_left.volume = MixRead16(addr + 0x22);
    pb.mixer.auxB_left.volume_delta = MixRead16(addr + 0x24);
    pb.mixer.auxB_right.volume = MixRead16(addr + 0x26);
    pb.mixer.auxB_right.volume_delta = MixRead16(addr + 0x28);
    pb.mixer.auxB_surround.volume = MixRead16(addr + 0x2A);
    pb.mixer.auxB_surround.volume_delta = MixRead16(addr + 0x2C);
    pb.mixer.main_surround.volume = MixRead16(addr + 0x2E);
    pb.mixer.main_surround.volume_delta = MixRead16(addr + 0x30);
    pb.mixer.auxA_surround.volume = MixRead16(addr + 0x32);
    pb.mixer.auxA_surround.volume_delta = MixRead16(addr + 0x34);
    
    pb.initial_time_delay.on = MixRead16(addr + 0x36);
    pb.initial_time_delay.addrMemHigh = MixRead16(addr + 0x38);
    pb.initial_time_delay.addrMemLow = MixRead16(addr + 0x3A);
    pb.initial_time_delay.offsetLeft = MixRead16(addr + 0x3C);
    pb.initial_time_delay.offsetRight = MixRead16(addr + 0x3E);
    pb.initial_time_delay.targetLeft = MixRead16(addr + 0x40);
    pb.initial_time_delay.targetRight = MixRead16(addr + 0x42);
    
    pb.updates.num_updates[0] = MixRead16(addr + 0x44);
    pb.updates.num_updates[1] = MixRead16(addr + 0x46);
    pb.updates.num_updates[2] = MixRead16(addr + 0x48);
    // updNum[3] and updNum[4]: a GameCube frame is 5 ms, so these two exist and were dropped.
    pb.updates.num_updates[3] = MixRead16(addr + 0x4A);
    pb.updates.num_updates[4] = MixRead16(addr + 0x4C);
    pb.updates.data_hi = MixRead16(addr + 0x4E);
    pb.updates.data_lo = MixRead16(addr + 0x50);
    
    pb.dpop.main_left = MixRead16(addr + 0x52);
    pb.dpop.auxA_left = MixRead16(addr + 0x54);
    pb.dpop.auxB_left = MixRead16(addr + 0x56);
    pb.dpop.main_right = MixRead16(addr + 0x58);
    pb.dpop.auxA_right = MixRead16(addr + 0x5A);
    pb.dpop.auxB_right = MixRead16(addr + 0x5C);
    pb.dpop.main_surround = MixRead16(addr + 0x5E);
    pb.dpop.auxA_surround = MixRead16(addr + 0x60);
    pb.dpop.auxB_surround = MixRead16(addr + 0x62);
    
    pb.vol_env.cur_volume = MixRead16(addr + 0x64);
    pb.vol_env.cur_volume_delta = MixRead16(addr + 0x66);
    
    pb.audio_addr.looping = MixRead16(addr + 0x6E);
    // The GameCube and Wii AX sample-format constants are the same values (0 = DSP ADPCM,
    // 10 = PCM signed 16-bit, 25 = PCM signed 8-bit), and this mixer's Accelerator decodes them
    // bitwise: width = fmt & 3, decoder = (fmt >> 2) & 3, gain shift = (fmt >> 4) & 3.
    // 10 -> width 2 (16-bit), 25 -> width 1 (8-bit), 0 -> width 0 (4-bit nibble), which is exactly
    // right, so the value is passed through. Remapping it to (1 << 2) / (2 << 2) zeroed the width
    // bits and made 16-bit voices decode as nibbles, which is audible as static.
    pb.audio_addr.sample_format = MixRead16(addr + 0x70);
    pb.audio_addr.loop_addr_hi = MixRead16(addr + 0x72);
    pb.audio_addr.loop_addr_lo = MixRead16(addr + 0x74);
    pb.audio_addr.end_addr_hi = MixRead16(addr + 0x76);
    pb.audio_addr.end_addr_lo = MixRead16(addr + 0x78);
    pb.audio_addr.cur_addr_hi = MixRead16(addr + 0x7A);
    pb.audio_addr.cur_addr_lo = MixRead16(addr + 0x7C);
    
    for (int i = 0; i < 16; ++i) {
        pb.adpcm.coefs[i] = MixRead16(addr + 0x7E + i * 2);
    }
    pb.adpcm.gain = MixRead16(addr + 0x9E);
    pb.adpcm.pred_scale = MixRead16(addr + 0xA0);
    pb.adpcm.yn1 = MixRead16(addr + 0xA2);
    pb.adpcm.yn2 = MixRead16(addr + 0xA4);
    
    pb.src.ratio_hi = MixRead16(addr + 0xA6);
    pb.src.ratio_lo = MixRead16(addr + 0xA8);
    pb.src.cur_addr_frac = MixRead16(addr + 0xAA);
    pb.src.last_samples[0] = MixRead16(addr + 0xAC);
    pb.src.last_samples[1] = MixRead16(addr + 0xAE);
    pb.src.last_samples[2] = MixRead16(addr + 0xB0);
    pb.src.last_samples[3] = MixRead16(addr + 0xB2);
    
    pb.adpcm_loop_info.pred_scale = MixRead16(addr + 0xB4);
    pb.adpcm_loop_info.yn1 = MixRead16(addr + 0xB6);
    pb.adpcm_loop_info.yn2 = MixRead16(addr + 0xB8);
    
    pb.lpf.on = MixRead16(addr + 0xBA);
    pb.lpf.yn1 = MixRead16(addr + 0xBC);
    pb.lpf.a0 = MixRead16(addr + 0xBE);
    pb.lpf.b0 = MixRead16(addr + 0xC0);
}

void WritePBGameCube(uint32_t addr, const AXPBWii& pb) {
    // The real GameCube DSP writes the running volume back every frame; the game's own sound
    // engine reads it (AXVPB.c __AXServiceVPB) to decide whether a ramp has finished. Without
    // this the engine keeps re-applying full-scale volume and the mixer saturates at 32767,
    // which is audible as loud static.
    // A/B switch while the ramp semantics are unsettled: WIICOMPILED_AX_VOLWB=0 disables it.
    { static const bool wb = [] { const char* e = std::getenv("WIICOMPILED_AX_VOLWB");
                                  return !e || e[0] != '0'; }();
      if (wb) MixWrite16(addr + 0x64, static_cast<uint16_t>(pb.vol_env.cur_volume)); }
    MixWrite16(addr + 0x0E, pb.running);
    MixWrite16(addr + 0x7A, pb.audio_addr.cur_addr_hi);
    MixWrite16(addr + 0x7C, pb.audio_addr.cur_addr_lo);
    MixWrite16(addr + 0xA0, pb.adpcm.pred_scale);
    MixWrite16(addr + 0xA2, pb.adpcm.yn1);
    MixWrite16(addr + 0xA4, pb.adpcm.yn2);
    MixWrite16(addr + 0xAA, pb.src.cur_addr_frac);
    MixWrite16(addr + 0xAC, pb.src.last_samples[0]);
    MixWrite16(addr + 0xAE, pb.src.last_samples[1]);
    MixWrite16(addr + 0xB0, pb.src.last_samples[2]);
    MixWrite16(addr + 0xB2, pb.src.last_samples[3]);
    MixWrite16(addr + 0xBC, pb.lpf.yn1);
}

AXMixControl ConvertMixerControlGameCube(uint16_t mixerControl) {
    uint32_t ret = 0;
    if (mixerControl & 0x0001) ret |= MIX_MAIN_L;
    if (mixerControl & 0x0002) ret |= MIX_MAIN_R;
    if (mixerControl & 0x0004) ret |= MIX_MAIN_S;
    if (mixerControl & 0x0008) {
        if (mixerControl & 0x0001) ret |= MIX_MAIN_L_RAMP;
        if (mixerControl & 0x0002) ret |= MIX_MAIN_R_RAMP;
        if (mixerControl & 0x0004) ret |= MIX_MAIN_S_RAMP;
    }
    if (mixerControl & 0x0010) ret |= MIX_AUXA_L;
    if (mixerControl & 0x0020) ret |= MIX_AUXA_R;
    if (mixerControl & 0x0080) ret |= MIX_AUXA_S;
    if (mixerControl & 0x0040) {
        if (mixerControl & 0x0010) ret |= MIX_AUXA_L_RAMP;
        if (mixerControl & 0x0020) ret |= MIX_AUXA_R_RAMP;
    }
    if (mixerControl & 0x0100) {
        if (mixerControl & 0x0080) ret |= MIX_AUXA_S_RAMP;
    }
    if (mixerControl & 0x0200) ret |= MIX_AUXB_L;
    if (mixerControl & 0x0400) ret |= MIX_AUXB_R;
    if (mixerControl & 0x1000) ret |= MIX_AUXB_S;
    if (mixerControl & 0x0800) {
        if (mixerControl & 0x0200) ret |= MIX_AUXB_L_RAMP;
        if (mixerControl & 0x0400) ret |= MIX_AUXB_R_RAMP;
    }
    if (mixerControl & 0x2000) {
        if (mixerControl & 0x1000) ret |= MIX_AUXB_S_RAMP;
    }
    return static_cast<AXMixControl>(ret);
}

void TranslateCommandListGameCube(uint32_t addr, uint32_t sizeWords, uint16_t* outList, size_t maxWords, uint32_t* outSizeWords) {
#if defined(RECOMP_PROJECT_FFCC)
    { static unsigned s_n = 0; if (++s_n <= 5 || s_n % 500 == 0) std::fprintf(stderr, "[audio] gc cmdlist %u addr=0x%08X words=%u%c", s_n, addr, sizeWords, 10); }
#endif
    uint32_t inIdx = 0;
    uint32_t outIdx = 0;
    uint32_t lastPbAddr = 0;
    
    auto canRead = [&](uint32_t words) { return inIdx + words <= sizeWords; };
    auto push = [&](uint16_t val) { if (outIdx < maxWords) outList[outIdx++] = val; };
    
    while (canRead(1)) {
        uint16_t cmd = MixRead16(addr + inIdx * 2);
        inIdx++;
        
        switch (cmd) {
        case 0:
            if (canRead(2)) {
                push(0x00);
                push(MixRead16(addr + inIdx * 2));
                push(MixRead16(addr + (inIdx + 1) * 2));
                inIdx += 2;
            }
            break;
        case 2:
            if (canRead(2)) {
                lastPbAddr = (MixRead16(addr + inIdx * 2) << 16) | MixRead16(addr + (inIdx + 1) * 2);
                inIdx += 2;
            }
            break;
        case 3:
            push(0x04);
            push(lastPbAddr >> 16);
            push(lastPbAddr & 0xFFFF);
            break;
        case 4:
            if (canRead(4)) {
                push(0x05);
                push(0x8000);
                for (int i=0; i<4; i++) push(MixRead16(addr + (inIdx + i) * 2));
                inIdx += 4;
            }
            break;
        case 5:
            if (canRead(4)) {
                push(0x06);
                push(0x8000);
                for (int i=0; i<4; i++) push(MixRead16(addr + (inIdx + i) * 2));
                inIdx += 4;
            }
            break;
        case 0xE:
            if (canRead(4)) {
                push(0x0B);
                push(0x8000);
                for (int i=0; i<4; i++) push(MixRead16(addr + (inIdx + i) * 2));
                inIdx += 4;
            }
            break;
        case 0x12:
            if (canRead(4)) {
                push(0x0A);
                push(MixRead16(addr + inIdx * 2));
                push(MixRead16(addr + (inIdx + 1) * 2));
                push(MixRead16(addr + (inIdx + 2) * 2));
                push(MixRead16(addr + (inIdx + 3) * 2));
                inIdx += 4;
            }
            break;
        case 0xF:
            push(0x0E);
            inIdx = sizeWords;
            break;
        case 7:
        case 0x11:
            if (canRead(2)) inIdx += 2;
            break;
        case 0x10:
            if (canRead(4)) inIdx += 4;
            break;
        case 0x13:
            if (canRead(12)) inIdx += 12;
            break;
        default:
            inIdx = sizeWords;
            break;
        }
    }
    push(0x0E);
    *outSizeWords = outIdx;
}

uint8_t ReadAramByteGameCube(uint32_t addr, uint16_t is_stream)
{
    // The GameCube DSP always reads sample data out of ARAM. PB field 0x10 (type) says whether a voice
    // is a plain one or a STREAMING one; it does not say where the samples live. RedSound puts a
    // stream's per-channel planes in ARAM as well:
    //     m_waveBase = RedStreamAramGetChannelPlane(streamData->m_aramBuffer, channelIndex)
    // Routing type-1 voices to main memory made them decode whatever happened to occupy that physical
    // address. Audible as constant scratching, and it leaves the two channels uncorrelated with one of
    // them saturating at full scale while its partner stays near silent.
    (void)is_stream;

    std::vector<uint8_t>& aram = FfccAramBuffer();
    if (addr < aram.size()) {
        return aram[addr];
    }

    // Out of ARAM range: report once rather than silently returning zeros, because zeros fed to an
    // ADPCM decoder ring up to full scale rather than going quiet.
    static unsigned s_outOfRange = 0;
    if ((++s_outOfRange % 20000u) == 1u) {
        std::fprintf(stderr, "[audio] sample read past end of ARAM: addr 0x%08x (count %u)%c",
                     addr, s_outOfRange, 10);
    }
    return 0;
}

} // namespace AxDspHle

#endif
