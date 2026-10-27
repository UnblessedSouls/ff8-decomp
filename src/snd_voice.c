#include "common.h"
#include "psxsdk/libspu.h"
#include "sound.h"
#include "snd_dma.h"
#include "snd_init.h"
#include "snd_voice.h"

void sndReleaseVoice(s32 a0);

void func_80014FFC(s32 voice, SndVoiceParam *param, s32 scale) {
    u16 *reg;
    s32 left;
    s32 right;

    param->updateFlags = 0;
    reg = (u16 *)(0x1F801C00 + (voice << 4));
    if (scale == 0) {
        left = param->volLeft;
        right = param->volRight;
    } else {
        left = param->volLeft * scale;
        left >>= 7;
        right = param->volRight * scale;
        right >>= 7;
    }
    *reg++ = left & 0x7FFF;
    *reg++ = right & 0x7FFF;
    *reg++ = param->sampleRate;
    *reg++ = param->startAddr >> 3;
    *reg++ = param->adsrLow;
    *reg = param->adsrHigh;
    reg[2] = param->repeatAddr >> 3;
}

/**
 * @brief Applies pending SPU register updates for a voice.
 *
 * Reads the update flags from the voice parameter structure. For each
 * set flag, calls the corresponding SPU register function (pitch, volume,
 * sample address, repeat address, ADSR). Clears processed flags after each
 * call and exits early when all flags are handled.
 *
 * @param a0 SPU voice index (0-23).
 * @param a1 Pointer to the voice parameter structure.
 * @param a2 The owning track's flags (unused).
 */
void func_800150A8(s32 a0, SndVoiceParam *a1, s32 a2) {
    s32 flags = a1->updateFlags;

    if (flags == 0) {
        return;
    }
    a1->updateFlags = 0;

    if (flags & 0x10) {
        flags &= ~0x10;
        spuSetVoicePitch(a0, a1->sampleRate);
        if (flags == 0) {
            return;
        }
    }

    if (flags & 0x3) {
        flags &= ~0x3;
        spuSetVoiceVolume(a0, a1->volLeft, a1->volRight, a1->sweep);
        if (flags == 0) {
            return;
        }
    }

    if (flags & 0x80) {
        flags &= ~0x80;
        spuSetVoiceStartAddr(a0, a1->startAddr);
        if (flags == 0) {
            return;
        }
    }

    if (flags & 0x10000) {
        flags &= ~0x10000;
        spuSetVoiceRepeatAddr(a0, a1->repeatAddr);
        if (flags == 0) {
            return;
        }
    }

    if (flags & 0x6600) {
        flags &= ~0x6600;
        spuSetVoiceAdsrHigh(a0, a1->adsrHigh);
        if (flags == 0) {
            return;
        }
    }

    if (flags & 0x9900) {
        spuSetVoiceAdsrLow(a0, a1->adsrLow);
    }
}

void func_800151C4(SoundSeqTrack *track, u32 bit, s32 isSfx) {
    s32 val;
    u32 depth;
    u32 base;
    s16 *wave;

    if (track->panFade != 0) {
        track->panFade--;
        val = track->panShifted + track->panTarget;
        if ((val & 0xFFE00000) != (track->panShifted & 0xFFE00000)) {
            track->unkF4.updateFlags |= 3;
        }
        track->panShifted = val;
    }
    if (track->notePitchFade != 0) {
        track->notePitchFade--;
        val = track->notePitch + track->unkE2;
        if ((val & 0xFF00) != (track->notePitch & 0xFF00)) {
            track->unkF4.updateFlags |= 3;
        }
        track->notePitch = val;
    }
    if (isSfx == 0) {
        if (track->unk82 != 0) {
            track->unk82--;
            val = track->pitchBend + track->unkDA;
            if ((val & 0x7F00) != (track->pitchBend & 0x7F00)) {
                track->unkF4.updateFlags |= 3;
            }
            track->pitchBend = val;
        }
    }
    if (track->unkA0 != 0) {
        track->unkA0--;
    }
    if (track->unkB4 != 0) {
        track->unkB4--;
    }
    if (track->noiseDuration != 0 && --track->noiseDuration == 0) {
        if (isSfx == 0) {
            g_sndSeqState->unk3C ^= bit;
        } else {
            D_80075028.unk1C ^= bit;
        }
        D_80077288[2] |= 0x110;
    }
    if (track->reverbDuration != 0 && --track->reverbDuration == 0) {
        if (isSfx == 0) {
            g_sndSeqState->unk44 ^= bit;
        } else {
            D_80075028.unk24 ^= bit;
        }
        D_80077288[2] |= 0x100;
    }
    if (track->delaySendTarget != 0) {
        track->delaySendTarget--;
        track->delaySend += track->unkAE;
        depth = (track->delaySend & 0x7F00) >> 8;
        if (track->delaySend & 0x8000) {
            base = (depth * track->loopCounter) >> 7;
        } else {
            base = (depth * ((track->loopCounter * 15) >> 8)) >> 7;
        }
        track->unkA8 = base;
        if (track->unkA0 == 0 && track->unkA4 != 1) {
            wave = track->unk18;
            if (wave[0] == 0 && wave[1] == 0) {
                wave += wave[2];
            }
            val = (track->unkA8 * wave[0]) >> 16;
            if (val != track->portamento) {
                track->portamento = val;
                track->unkF4.updateFlags |= 0x10;
                if (val >= 0) {
                    track->portamento = val * 2;
                }
            }
        }
    }
    if (track->volumeLfoTarget != 0) {
        track->volumeLfoTarget--;
        track->volumeLfo += track->unkC0;
        if (track->unkB4 == 0 && track->unkB8 != 1) {
            s32 vol;

            wave = track->unk1C;
            if (wave[0] == 0 && wave[1] == 0) {
                wave += wave[2];
            }
            vol = ((s16)(track->panShifted >> 16) * (track->pitchBend >> 8)) >> 7;
            val = ((vol * (track->volumeLfo >> 8)) << 9) >> 16;
            val = (val * wave[0]) >> 15;
            if (val != track->volumeLfoStop) {
                track->volumeLfoStop = val;
                track->unkF4.updateFlags |= 3;
            }
        }
    }
    if (track->panLfoTarget != 0) {
        track->panLfoTarget--;
        track->panLfo += track->unkCE;
        if (track->unkC6 != 1) {
            wave = track->unk20;
            if (wave[0] == 0 && wave[1] == 0) {
                wave += wave[2];
            }
            val = ((track->panLfo >> 8) * wave[0]) >> 15;
            if (val != track->panLfoStop) {
                track->panLfoStop = val;
                track->unkF4.updateFlags |= 3;
            }
        }
    }
    if (track->unk90 != 0) {
        track->unk90--;
        val = track->cmdDataPtr + track->unk4C;
        if ((val & 0xFFFF0000) != (track->cmdDataPtr & 0xFFFF0000)) {
            track->unkF4.updateFlags |= 0x10;
        }
        track->cmdDataPtr = val;
    }
}

void func_80015668(SoundSeqTrack *track, u32 bit) {
    s32 vol;
    s32 pitch;
    s32 val;
    s32 flags;
    s16 *wave;
    s32 mono;
    s32 value;

    vol = ((s16)(track->panShifted >> 16) * (track->pitchBend >> 8)) >> 7;
    flags = track->flags;
    if (flags & 1) {
        if (track->unkA0 == 0) {
            if (--track->unkA4 == 0) {
                track->unkA4 = track->unkA2;
                wave = track->unk18;
                if (wave[0] == 0 && wave[1] == 0) {
                    track->unk18 = wave + wave[2];
                }
                value = *track->unk18++;
                val = (track->unkA8 * value) >> 16;
                if (val != track->portamento) {
                    track->portamento = val;
                    track->unkF4.updateFlags |= 0x10;
                    if (val >= 0) {
                        track->portamento = val * 2;
                    }
                }
            }
        }
    }
    if (flags & 2) {
        if (track->unkB4 == 0) {
            if (--track->unkB8 == 0) {
                track->unkB8 = track->unkB6;
                wave = track->unk1C;
                if (wave[0] == 0 && wave[1] == 0) {
                    track->unk1C = wave + wave[2];
                }
                value = *track->unk1C++;
                val = ((vol * (track->volumeLfo >> 8)) << 9) >> 16;
                val = (val * value) >> 15;
                if (val != track->volumeLfoStop) {
                    track->volumeLfoStop = val;
                    track->unkF4.updateFlags |= 3;
                }
            }
        }
    }
    if (flags & 4) {
        if (--track->unkC6 == 0) {
            track->unkC6 = track->unkC4;
            wave = track->unk20;
            if (wave[0] == 0 && wave[1] == 0) {
                track->unk20 = wave + wave[2];
            }
            value = *track->unk20++;
            val = ((track->panLfo >> 8) * value) >> 15;
            if (val != track->panLfoStop) {
                track->panLfoStop = val;
                track->unkF4.updateFlags |= 3;
            }
        }
    }
    if (flags & 0x20) {
        vol = ((s16)(track[-1].unkF4.sampleRate << 1) * (track->pitchBend >> 8)) >> 7;
        track->unkF4.updateFlags |= 3;
    }
    if (track->unkF4.updateFlags & 3) {
        vol += track->volumeLfoStop;
        vol = (vol * (s32)(((u32)g_sndSeqState->unk50 >> 16) & 0x7F)) >> 7;
        val = ((track->notePitch >> 8) + track->panLfoStop) & 0xFF;
        if (D_8007728C & 2) {
            mono = (vol * D_80051954) >> 15;
            track->unkF4.volRight = mono;
            track->unkF4.volLeft = mono;
        } else {
            track->unkF4.volLeft = (vol * D_80051854[val]) >> 15;
            track->unkF4.volRight = (vol * D_80051854[val ^ 0xFF]) >> 15;
        }
    }
    if (flags & 0x10) {
        pitch = track[-1].unkF4.sampleRate + track->portamento + (s16)(track->cmdDataPtr >> 16);
        val = D_80077280 & 0xFF0000;
        if (val != 0) {
            val >>= 16;
            if (val < 0x80) {
                pitch += (pitch * val) >> 7;
            } else {
                pitch = (pitch * val) >> 8;
            }
        }
        track->unkF4.sampleRate = ((u16)track->timing + pitch) & 0x3FFF;
        track->unkF4.updateFlags |= 0x10;
    } else if (track->unkF4.updateFlags & 0x10) {
        pitch = track->loopCounter + track->portamento + (s16)(track->cmdDataPtr >> 16);
        val = D_80077280 & 0xFF0000;
        if (val != 0) {
            val >>= 16;
            if (val < 0x80) {
                pitch += (pitch * val) >> 7;
            } else {
                pitch = (pitch * val) >> 8;
            }
        }
        track->unkF4.sampleRate = ((u16)track->timing + pitch) & 0x3FFF;
    }
}

void func_80015AC8(SoundSeqTrack *track, u32 bit) {
    s32 vol;
    s32 pitch;
    s32 val;
    s32 mul;
    s32 pan;
    s32 flags;
    s16 *wave;
    s32 mono;
    s32 value;

    vol = ((s16)(track->panShifted >> 16) * (track->pitchBend >> 8)) >> 7;
    flags = track->flags;
    if (flags & 1) {
        if (--track->unkA4 == 0) {
            track->unkA4 = track->unkA2;
            wave = track->unk18;
            if (wave[0] == 0 && wave[1] == 0) {
                track->unk18 = wave + wave[2];
            }
            value = *track->unk18++;
            val = (track->unkA8 * value) >> 16;
            if (val != track->portamento) {
                track->portamento = val;
                track->unkF4.updateFlags |= 0x10;
                if (val >= 0) {
                    track->portamento = val * 2;
                }
            }
        }
    }
    if (flags & 2) {
        if (--track->unkB8 == 0) {
            track->unkB8 = track->unkB6;
            wave = track->unk1C;
            if (wave[0] == 0 && wave[1] == 0) {
                track->unk1C = wave + wave[2];
            }
            val = ((vol * (track->volumeLfo >> 8)) << 9) >> 16;
            value = *track->unk1C++;
            val = (val * value) >> 15;
            if (val != track->volumeLfoStop) {
                track->volumeLfoStop = val;
                track->unkF4.updateFlags |= 3;
            }
        }
    }
    if (flags & 4) {
        if (--track->unkC6 == 0) {
            track->unkC6 = track->unkC4;
            wave = track->unk20;
            if (wave[0] == 0 && wave[1] == 0) {
                track->unk20 = wave + wave[2];
            }
            value = *track->unk20++;
            val = ((track->panLfo >> 8) * value) >> 15;
            if (val != track->panLfoStop) {
                track->panLfoStop = val;
                track->unkF4.updateFlags |= 3;
            }
        }
    }
    if (flags & 0x20) {
        vol = ((s16)(track[-1].unkF4.sampleRate << 1) * (track->pitchBend >> 8)) >> 7;
        track->unkF4.updateFlags |= 3;
    }
    if (track->unkF4.updateFlags & 3) {
        vol += track->volumeLfoStop;
        if (track->keyOnMask & 0x2000000) {
            pan = 0x80;
        } else {
            vol = (vol * (s8)(track->expression >> 8)) >> 7;
            pan = ((track->notePitch + track->pitchModDepth) >> 8) + track->panLfoStop + 0x80;
            pan &= 0xFF;
        }
        if (D_8007728C & 2) {
            mono = (vol * D_80051954) >> 15;
            track->unkF4.volRight = mono;
            track->unkF4.volLeft = mono;
        } else {
            track->unkF4.volLeft = (vol * D_80051854[pan]) >> 15;
            track->unkF4.volRight = (vol * D_80051854[pan ^ 0xFF]) >> 15;
        }
    }
    if (flags & 0x10) {
        pitch = track[-1].unkF4.sampleRate + track->portamento + (s16)(track->cmdDataPtr >> 16);
        mul = track->loopLimit & 0xFF00;
        if (!(track->keyOnMask & 0x2000000)) {
            if (mul != 0) {
                mul >>= 8;
                if (mul < 0x80) {
                    pitch += (pitch * mul) >> 7;
                } else {
                    pitch = (pitch * mul) >> 8;
                }
            }
        }
        track->unkF4.sampleRate = ((u16)track->timing + pitch) & 0x3FFF;
        track->unkF4.updateFlags |= 0x10;
    } else if (track->unkF4.updateFlags & 0x10) {
        pitch = track->loopCounter + track->portamento + (s16)(track->cmdDataPtr >> 16);
        mul = track->loopLimit & 0xFF00;
        if (!(track->keyOnMask & 0x2000000)) {
            if (mul != 0) {
                mul >>= 8;
                if (mul < 0x80) {
                    pitch += (pitch * mul) >> 7;
                } else {
                    pitch = (pitch * mul) >> 8;
                }
            }
        }
        track->unkF4.sampleRate = ((u16)track->timing + pitch) & 0x3FFF;
    }
}

/**
 * @brief Finds the SPU voice with the lowest priority and frees it for reuse.
 *
 * Scans all 24 voice entries in the D_80073CB0 array (stride 8, priority
 * at offset +4) to find the voice with the lowest priority value. If found,
 * calls sndReleaseVoice to release it from all active sequences.
 *
 * @return The index of the freed voice, or 0x18 (24) if no voice is available.
 */
s32 sndAllocVoice(void) {
    u32 i;
    u16 min_val;
    u32 min_idx;
    UnkStruct80073CB0 *p;

    i = 0;
    min_val = 0x7FFF;
    min_idx = 0x18;
    p = D_80073CB0;

    do {
        if (p->unk04 < (s16)min_val) {
            min_val = p->unk04;
            min_idx = i;
        }
        i++;
        p++;
    } while (i < 0x18);

    if ((s16)min_val == 0x7FFF) {
        return 0x18;
    }
    if (min_idx < 0x18) {
        sndReleaseVoice(min_idx);
    }
    return min_idx;
}

void func_80015FE0(SoundSeqTrack *track, u32 active, u32 newVoices, s32 *keyOn) {
    UnkStruct80073CB0 *env;
    u32 keyed;
    u32 bit;
    s32 i;
    u32 voice;

    bit = 1;
    i = 0;
    keyed = active & g_sndSeqState->unk10;
    do {
        if (active & bit) {
            func_80015668(track, bit);
            if (track->unkF4.updateFlags != 0) {
                if (D_8007507C & bit) {
                    track->unkF4.volRight = 0;
                    track->unkF4.volLeft = 0;
                }
                if (keyed & bit) {
                    if (newVoices & bit) {
                        *keyOn |= 1 << i;
                        track->unkF4.unk00 = i;
                    } else {
                        env = D_80073CB0;
                        voice = 0;
                        do {
                            if (env->unk04 == 0) {
                                *keyOn |= 1 << voice;
                                track->unkF4.unk00 = voice;
                                env->unk04 = 0x7FFF;
                                break;
                            }
                            voice++;
                            env++;
                            if (voice == 24) {
                                g_sndSeqState->unk00 |= 2;
                                voice = sndAllocVoice();
                                if (voice != 24) {
                                    *keyOn |= 1 << voice;
                                    track->unkF4.unk00 = voice;
                                    D_80073CB0[voice].unk04 = 0x7FFF;
                                } else {
                                    track->unkF4.unk00 = voice;
                                    g_sndSeqState->unk00 |= 1;
                                }
                                break;
                            }
                        } while (voice < 24);
                    }
                    voice = track->unkF4.unk00;
                    if (voice < 24) {
                        func_80014FFC(voice, &track->unkF4, track->unkF4.sweep);
                        D_800772F8[track->unkF4.unk00] = g_sndSeqState;
                        D_80077288[2] |= 0x100;
                    }
                } else {
                    voice = track->unkF4.unk00;
                    if (voice < 24) {
                        func_800150A8(voice, &track->unkF4, track->flags);
                    }
                }
            }
            active &= ~bit;
        }
        bit <<= 1;
        track++;
        i++;
    } while (active != 0);
}

/**
 * @brief Releases a voice from all active sequence tracks.
 *
 * Iterates over 32 track entries in the D_80070D60 array (stride 0x110,
 * voice assignment at offset +0xF4). Any track assigned to voice @p a0
 * is reassigned to voice 24 (invalid/none). Also checks the secondary
 * sequence array D_80073C34 if D_80073CA8 is set.
 *
 * @param a0 SPU voice index to release (0-23).
 */
void sndReleaseVoice(s32 a0) {
    s32 base = (s32)D_80070D60;
    s32 i = 32;
    s32 replacement = 24;
    s32 ptr = base + 0xF4;
    do {
        if (a0 == *(s32 *)ptr) {
            *(s32 *)ptr = replacement;
        }
        i--;
        ptr += 0x110;
    } while (i != 0);
    if (D_80073CA8 != NULL) {
        base = (s32)D_80073C34;
        i = 32;
        replacement = 24;
        ptr = base + 0xF4;
        do {
            if (a0 == *(s32 *)ptr) {
                *(s32 *)ptr = replacement;
            }
            i--;
            ptr += 0x110;
        } while (i != 0);
    }
}

/**
 * @brief Checks if a voice is assigned to any active track matching a bitmask.
 *
 * Iterates over 32 track entries (stride 0x110). For each track whose voice
 * assignment (offset +0xF4) equals @p a2, checks whether the corresponding
 * bit in @p a1 is set.
 *
 * @param a0 Base pointer to the sequence track array.
 * @param a1 Bitmask of tracks to check (bit N = track N).
 * @param a2 SPU voice index to search for.
 * @return 1 if any matching track has its bit set in @p a1, 0 otherwise.
 */
s32 sndCheckVoiceInMask(SoundSeqTrack *tracks, s32 mask, s32 voice) {
    u32 i = 0;
    s32 bit = 1;
    do {
        if (voice == tracks->unkF4.unk00) {
            if (mask & (bit << i)) {
                return 1;
            }
        }
        i++;
        tracks++;
    } while (i < 0x20);
    return 0;
}

void func_80016344(u32 mask) {
    UnkStruct80073CB0 *entry;
    u32 used = (g_sndSeqState->unk04 & g_sndSeqState->unk0C) | mask;
    u32 i;

    if (D_80073CA8 != NULL) {
        used |= D_80073CA8->unk04 & D_80073CA8->unk0C;
    }
    entry = D_80073CB0;
    for (i = 0; i < 24; entry++, i++) {
        if (used & (1 << i)) {
            entry->unk04 = 0x7FFF;
        } else {
            SpuGetVoiceEnvelope(i, &entry->unk04);
            if (entry->unk04 == 0) {
                sndReleaseVoice(i);
            } else if (g_sndSeqState->unk08 != 0) {
                if (sndCheckVoiceInMask(D_80070D60, g_sndSeqState->unk08, i)) {
                    entry->unk04 = 0x7FFF;
                }
                if (D_80073CA8 != NULL && sndCheckVoiceInMask(D_80073C34, D_80073CA8->unk08, i)) {
                    entry->unk04 = 0x7FFF;
                }
            }
        }
    }
}

INCLUDE_ASM("asm/nonmatchings/snd_voice", func_80016478);

extern s32 D_80074F1C;
/**
 * @brief Stores the current global tempo/timing value into a track structure.
 *
 * Copies D_80074F1C (a global timing or tick counter) into the track
 * structure field at offset 0x50.
 *
 * @param a0 Pointer to a sequence track structure.
 */
void sndStoreTrackTiming(u8 *a0) {
    ((SoundSeqTrack *)a0)->timing = D_80074F1C;
}

INCLUDE_ASM("asm/nonmatchings/snd_voice", func_800164D8);

/**
 * @brief Builds a voice bitmask from active tracks matching a track bitmask.
 *
 * For each set bit in @p trackMask, reads the corresponding track's voice
 * assignment (offset +0xF4). If the voice index is valid (< 24), sets that
 * voice's bit in *resultPtr. After processing, masks the result with @p finalMask.
 *
 * @param tracks Base pointer to the track array (stride 0x110).
 * @param resultPtr Pointer to the output bitmask (accumulated via OR).
 * @param trackMask Bitmask of tracks to check.
 * @param finalMask AND mask applied to the result after accumulation.
 */
void func_80016A1C(u8 *tracks, s32 *resultPtr, s32 trackMask, s32 finalMask) {
    s32 bit = 1;
    s32 one = 1;

    do {
        if (trackMask & bit) {
            s32 voice = ((SoundSeqTrack *)tracks)->unkF4.unk00;
            if ((u32)voice < 0x18) {
                *resultPtr |= one << voice;
            }
        }
        trackMask &= ~bit;
        tracks += 0x110;
        bit <<= 1;
    } while (trackMask != 0);

    *resultPtr &= finalMask;
}

INCLUDE_ASM("asm/nonmatchings/snd_voice", func_80016A7C);

INCLUDE_ASM("asm/nonmatchings/snd_voice", func_80016C28);
