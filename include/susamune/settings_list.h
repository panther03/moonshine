#ifndef SUSAMUNE_SETTINGS_LIST_H
#define SUSAMUNE_SETTINGS_LIST_H

// =====================================================================
// settings_list.h
//
// The canonical list of persisted settings, shared by every toolchain:
// the mod (C++, Kuribo clang), the Nintendont ARM kernel (devkitARM),
// and the PPC loader (devkitPPC). Keep this file plain C.
//
// Each row is (enumerator, ini_key):
//   - the enumerator defines SettingId and therefore the values[] layout
//     of the MEM2 handoff blob, which carries no per-key names -- only a
//     count. So ROWS MUST NOT BE REORDERED: append new settings at the end
//     of their category instead. Appending is always safe -- an older
//     kernel reports a smaller count and the rest keep their defaults.
//
//     REMOVING a row shifts every later index, and the launcher and the
//     mod_<region>.bin files no longer ship as one binary, so a stale
//     launcher would map keys onto the wrong settings. Bump
//     SUSAMUNE_CFG_VERSION (susamune_cfg.h) when you remove one: a
//     mismatched pair then refuses the block instead of scrambling values.
//     susamune.ini itself is keyed by name, so a user only loses the
//     removed key, not the rest of their file.
//   - the ini_key is what appears in susamune.ini. It is deliberately
//     separate from the menu display name in kSettingDescs so that
//     relabelling a menu entry never invalidates a user's saved file.
//     Only the launcher expands the keys into strings -- the mod reads
//     values by index and would otherwise carry the table for nothing.
//
// The menu display name, type, choice labels, default and category stay
// in kSettingDescs (settings.cpp) -- the launcher has no use for them and
// they cannot be expressed in a C-compatible header.
// =====================================================================

#define SUSAMUNE_SETTING_LIST(X)                                              \
    /* -- Quality-of-life -- */                                               \
    X(SETTING_FAST_TEXT,            "fast_text")                              \
    X(SETTING_FLUDD_SECRETS,        "fludd_secrets")                          \
    X(SETTING_AREA_LOCK,            "area_lock")                              \
    X(SETTING_INFINITE_LIVES,       "infinite_lives")                         \
    X(SETTING_DISABLE_BLUE_COIN,    "disable_blue_coin")                      \
    X(SETTING_FMV_SKIPS,            "fmv_skips")                              \
    X(SETTING_UNLOCK_YOSHI,         "unlock_yoshi")                           \
    X(SETTING_UNLOCK_NOZZLES,       "unlock_nozzles")                         \
    X(SETTING_FREE_PAUSE,           "free_pause")                             \
    X(SETTING_EXIT_AREA_EVERYWHERE, "exit_area_everywhere")                   \
    X(SETTING_ANY_FRUIT_YOSHI,      "any_fruit_yoshi")                        \
    X(SETTING_INFINITE_JUICE,       "infinite_juice")                         \
    X(SETTING_INTRO_SKIP,           "intro_skip")                             \
    X(SETTING_RESPAWN_SHINES,       "respawn_shines")                         \
    X(SETTING_FAST_PIANTISSIMO,     "fast_piantissimo")                       \
    /* -- Savestate -- */                                                     \
    X(SETTING_SAVE_RNG_STATE,       "save_rng_state")                         \
    /* -- Misc -- */                                                          \
    X(SETTING_NOZZLE_LOCK,          "nozzle_lock")                            \
    X(SETTING_FORCE_PLAZA_EVENTS,   "force_plaza_events")                     \
    X(SETTING_NEVER_PAUSE_IGT,      "never_pause_igt")                        \
    X(SETTING_SHADOW_MARIO_HP,      "shadow_mario_hp")                        \
    X(SETTING_STAGE_INTRO_SKIP,     "stage_intro_skip")                       \
    X(SETTING_DEATHLESS_BLOOPER,    "deathless_blooper")                      \
    X(SETTING_NO_SHINE_ANIM,        "no_shine_anim")                          \
    X(SETTING_FRUIT_NEVER_TIMEOUT,  "fruit_never_timeout")                    \
    X(SETTING_DISABLE_Z_MENU,       "disable_z_menu")                         \
    X(SETTING_DISABLE_WARPS,        "disable_warps")                          \
    /* -- Cosmetic -- */                                                      \
    X(SETTING_MUTE_BGM,             "mute_bgm")                               \
    X(SETTING_REPLACE_EPISODE_NAMES, "replace_episode_names")                 \
    X(SETTING_SHINE_OUTFIT,         "shine_outfit")                           \
    X(SETTING_SHINY_SHINES,         "shiny_shines")                           \
    /* -- UI -- */                                                            \
    X(SETTING_SHOW_BGM_SLOTS,       "show_bgm_slots")                          \
    /* -- Timer -- */                                                          \
    X(SETTING_QF_TIMER,             "qf_timer")                                \
    X(SETTING_QF_SECTION_TIMER,     "qf_section_timer")                        \
    X(SETTING_QF_SECTION_KEEP,      "qf_section_keep")                         \
    /* -- QF freeze triggers, in the upstream generator's order -- */          \
    X(SETTING_QF_FREEZE_YELLOW,     "qf_freeze_yellow_coin")                   \
    X(SETTING_QF_FREEZE_RED,        "qf_freeze_red_coin")                      \
    X(SETTING_QF_FREEZE_BLUE,       "qf_freeze_blue_coin")                     \
    X(SETTING_QF_FREEZE_ITEM,       "qf_freeze_item")                          \
    X(SETTING_QF_FREEZE_TALK,       "qf_freeze_talk")                          \
    X(SETTING_QF_FREEZE_DEMO,       "qf_freeze_demo")                          \
    X(SETTING_QF_FREEZE_CLEANED,    "qf_freeze_cleaned")                       \
    X(SETTING_QF_FREEZE_BOWSER,     "qf_freeze_bowser")                        \
    X(SETTING_QF_FREEZE_YOSHI,      "qf_freeze_yoshi")                         \
    X(SETTING_QF_FREEZE_TAKE,       "qf_freeze_take")                          \
    X(SETTING_QF_FREEZE_DROP,       "qf_freeze_drop")                          \
    X(SETTING_QF_FREEZE_PUT,        "qf_freeze_put")                           \
    X(SETTING_QF_FREEZE_TRIPLE,     "qf_freeze_triple_jump")                   \
    X(SETTING_QF_FREEZE_SPIN,       "qf_freeze_spin_jump")                     \
    X(SETTING_QF_FREEZE_LEDGE,      "qf_freeze_ledge_grab")                    \
    X(SETTING_QF_FREEZE_WALLKICK,   "qf_freeze_wall_kick")                     \
    X(SETTING_QF_FREEZE_BOUNCE,     "qf_freeze_bounce")                        \
    X(SETTING_QF_FREEZE_ROPE,       "qf_freeze_rope_jump")


#endif  // SUSAMUNE_SETTINGS_LIST_H
