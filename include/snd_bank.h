#ifndef SND_BANK_H
#define SND_BANK_H

#include "sound.h"

extern void func_80017410(SoundMusicConfig *config, SoundSeqTrack *tracks, s32 id);
extern void func_80017AAC(s32 *cmd, s32 sfx1, s32 sfx2, s32 noStop);
extern void func_80017D14(SoundMusicConfig *seq, SoundSeqTrack *tracks);
extern void func_80017D5C(void);

#endif /* SND_BANK_H */
