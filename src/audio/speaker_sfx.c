/* PC speaker sound effects. The original game played effects on the AdLib only; these are
 * new, short enough not to hide the music for long (speaker mode mutes the music while one
 * plays, since a real speaker has one voice). Ids as in game/events.h. */
#include "audio/speaker.h"

#define END {0, 0, 0}

static const ToneStep enemy_hit[] = {{1400, 300, 70}, END};                         /* 1: falling sweep */
static const ToneStep throw_[] = {{500, 1500, 50}, END};                            /* 3: rising chirp */
static const ToneStep player_hurt[] = {{180, 140, 60}, {0, 0, 30}, {180, 120, 80}, END}; /* 4: double buzz */
static const ToneStep player_hit[] = {{900, 700, 40}, END};                         /* 5: short blip */
static const ToneStep pick_up[] = {{880, 880, 40}, {1320, 1320, 60}, END};          /* 9: two-tone rise */
static const ToneStep bounce[] = {{300, 700, 45}, END};                             /* 12: up-sweep */
static const ToneStep blip[] = {{1000, 1000, 50}, END};                             /* the unused ids */

const ToneStep *speaker_sfx(int n)
{
    switch (n) {
    case 1: return enemy_hit;
    case 3: return throw_;
    case 4: return player_hurt;
    case 5: return player_hit;
    case 9: return pick_up;
    case 12: return bounce;
    default: return blip;
    }
}
