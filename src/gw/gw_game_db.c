/* SPDX-License-Identifier: GPL-3.0-or-later */

#include "gw_game_db.h"

#include <string.h>

#define GAME(title_, file_, machine_, signature_, cpu_, profile_, size_, crc_, enabled_) \
    {title_, file_, machine_, signature_, cpu_, profile_,                       \
     GW_DISPLAY_SINGLE_320X240, size_, UINT32_C(crc_), enabled_}

/*
 * Machine/input identifiers and program checksums are from MAME hh_sm510.cpp
 * at aaef28cd47db02b2b66359a49ca50c4ffaed464c, the revision consumed by
 * LCD-Game-Shrinker's format parser. Package signatures are the last eight
 * bytes of the machine identifier, as written by shrink_it.py.
 */
static const GWGameInfo games[] = {
    /* Silver */
    GAME("Ball", "Game & Watch Ball.gw", "gnw_ball", "gnw_ball", "SM5A___", GW_INPUT_PROFILE_BALL, 0x740, 0xac94e6e4, 1),
    GAME("Flagman", "Game & Watch Flagman.gw", "gnw_flagman", "_flagman", "SM5A___", GW_INPUT_PROFILE_FLAGMAN, 0x740, 0xcc7a99e4, 1),
    GAME("Vermin", "Game & Watch Vermin.gw", "gnw_vermin", "w_vermin", "SM5A___", GW_INPUT_PROFILE_UNSUPPORTED, 0x740, 0xf8493177, 0),
    GAME("Fire (Silver)", "Game & Watch Fire (Silver).gw", "gnw_fires", "nw_fires", "SM5A___", GW_INPUT_PROFILE_UNSUPPORTED, 0x740, 0x154ef27d, 0),
    GAME("Judge", "Game & Watch Judge (green version).gw", "gnw_judge", "nw_judge", "SM5A___", GW_INPUT_PROFILE_UNSUPPORTED, 0x740, 0x1b28a834, 0),

    /* Gold */
    GAME("Manhole (Gold)", "Game & Watch Manhole (Gold).gw", "gnw_manholeg", "manholeg", "SM5A___", GW_INPUT_PROFILE_UNSUPPORTED, 0x740, 0xae52c425, 0),
    GAME("Helmet", "Game & Watch Helmet (CN-17 version).gw", "gnw_helmet", "w_helmet", "SM5A___", GW_INPUT_PROFILE_UNSUPPORTED, 0x740, 0x6d251e2e, 0),
    GAME("Lion", "Game & Watch Lion.gw", "gnw_lion", "gnw_lion", "SM5A___", GW_INPUT_PROFILE_UNSUPPORTED, 0x740, 0x9677681d, 0),

    /* Wide Screen */
    GAME("Parachute", "Game & Watch Parachute.gw", "gnw_pchute", "w_pchute", "SM5A___", GW_INPUT_PROFILE_UNSUPPORTED, 0x740, 0x392b545e, 0),
    GAME("Octopus", "Game & Watch Octopus.gw", "gnw_octopus", "_octopus", "SM5A___", GW_INPUT_PROFILE_UNSUPPORTED, 0x740, 0xbd27781d, 0),
    GAME("Popeye", "Game & Watch Popeye (Wide Screen).gw", "gnw_popeye", "w_popeye", "SM5A___", GW_INPUT_PROFILE_UNSUPPORTED, 0x740, 0x49987769, 0),
    GAME("Chef", "Game & Watch Chef.gw", "gnw_chef", "gnw_chef", "SM5A___", GW_INPUT_PROFILE_UNSUPPORTED, 0x740, 0x2806ab39, 0),
    GAME("Mickey Mouse", "Game & Watch Mickey Mouse (Wide Screen).gw", "gnw_mmouse", "w_mmouse", "SM5A___", GW_INPUT_PROFILE_UNSUPPORTED, 0x740, 0xcb820c32, 0),
    GAME("Fire (Wide Screen)", "Game & Watch Fire (Wide Screen).gw", "gnw_fire", "gnw_fire", "SM5A___", GW_INPUT_PROFILE_UNSUPPORTED, 0x740, 0xf4c53ef0, 0),
    GAME("Turtle Bridge", "Game & Watch Turtle Bridge.gw", "gnw_tbridge", "_tbridge", "SM510__", GW_INPUT_PROFILE_UNSUPPORTED, 0x1000, 0x284e7224, 0),
    GAME("Fire Attack", "Game & Watch Fire Attack.gw", "gnw_fireatk", "_fireatk", "SM510__", GW_INPUT_PROFILE_UNSUPPORTED, 0x1000, 0x5f6e8042, 0),
    GAME("Snoopy Tennis", "Game & Watch Snoopy Tennis.gw", "gnw_stennis", "_stennis", "SM510__", GW_INPUT_PROFILE_UNSUPPORTED, 0x1000, 0xba1d9504, 0),

    /* New Wide Screen */
    GAME("Donkey Kong Jr.", "Game & Watch Donkey Kong Jr. (New Wide Screen).gw", "gnw_dkjr", "gnw_dkjr", "SM510__", GW_INPUT_PROFILE_UNSUPPORTED, 0x1000, 0x8dcfb5d1, 0),
    GAME("Mario's Cement Factory", "Game & Watch Mario's Cement Factory (New Wide Screen).gw", "gnw_mariocm", "_mariocm", "SM510__", GW_INPUT_PROFILE_UNSUPPORTED, 0x1000, 0xc1128dea, 0),
    GAME("Manhole (New Wide Screen)", "Game & Watch Manhole (New Wide Screen).gw", "gnw_manhole", "_manhole", "SM510__", GW_INPUT_PROFILE_UNSUPPORTED, 0x1000, 0xec03acf7, 0),
    GAME("Tropical Fish", "Game & Watch Tropical Fish.gw", "gnw_tfish", "nw_tfish", "SM510__", GW_INPUT_PROFILE_UNSUPPORTED, 0x1000, 0x53cde918, 0),
    GAME("Super Mario Bros.", "Game & Watch Super Mario Bros. (New Wide Screen).gw", "gnw_smbn", "gnw_smbn", "SM511__", GW_INPUT_PROFILE_UNSUPPORTED, 0x1000, 0x0dff3b12, 0),
    GAME("Climber", "Game & Watch Climber (New Wide Screen).gw", "gnw_climbern", "climbern", "SM511__", GW_INPUT_PROFILE_UNSUPPORTED, 0x1000, 0x2adcbd6d, 0),
    GAME("Balloon Fight", "Game & Watch Balloon Fight (New Wide Screen).gw", "gnw_bfightn", "_bfightn", "SM511__", GW_INPUT_PROFILE_UNSUPPORTED, 0x1000, 0x4c8d07ed, 0),
    GAME("Mario the Juggler", "Game & Watch Mario The Juggler.gw", "gnw_mariotj", "_mariotj", "SM511__", GW_INPUT_PROFILE_UNSUPPORTED, 0x1000, 0xf7118bb4, 0)
};

#undef GAME

const GWGameInfo *gw_game_db_find(const char signature[8],
                                  const char cpu_name[8])
{
    size_t i;
    for (i = 0; i < sizeof(games) / sizeof(games[0]); ++i) {
        if (memcmp(games[i].package_signature, signature, 8) == 0 &&
            memcmp(games[i].cpu_name, cpu_name, 8) == 0)
            return &games[i];
    }
    return NULL;
}

size_t gw_game_db_count(void)
{
    return sizeof(games) / sizeof(games[0]);
}

const GWGameInfo *gw_game_db_at(size_t index)
{
    return index < gw_game_db_count() ? &games[index] : NULL;
}
