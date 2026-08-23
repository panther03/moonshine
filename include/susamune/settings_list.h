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
//     count. So ROWS MUST NOT BE REORDERED: append new settings only at the
//     global tail. Appending is always safe -- an older
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
//     separate from the menu display name in src/settings_descs.inc so that
//     relabelling a menu entry never invalidates a user's saved file.
//     Only the launcher expands the keys into strings -- the mod reads
//     values by index and would otherwise carry the table for nothing.
//
// The menu display name, choice set, default and category stay in
// src/settings_descs.inc -- the launcher has no use for them and
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
    X(SETTING_SHOW_BGM_SLOTS,       "show_bgm_slots")                        \
    /* -- Appended settings (keep persisted indices stable) -- */             \
    X(SETTING_DISABLE_THIRD_CHOMPLET_AGGRO, "disable_third_chomplet_aggro")   \
    /* -- Timer -- */                                                         \
    X(SETTING_TIMER_SUNSHINE_VISIBILITY, "timer_sunshine_visibility")         \
    X(SETTING_TIMER_QFT_VISIBILITY,      "timer_qft_visibility")              \
    X(SETTING_TIMER_FREEZE_DURATION,     "timer_freeze_duration")             \
    X(SETTING_TIMER_FREEZE_YELLOW_COIN,  "timer_freeze_yellow_coin")          \
    X(SETTING_TIMER_FREEZE_RED_COIN,     "timer_freeze_red_coin")             \
    X(SETTING_TIMER_FREEZE_BLUE_COIN,    "timer_freeze_blue_coin")            \
    X(SETTING_TIMER_FREEZE_ITEM,         "timer_freeze_item")                 \
    X(SETTING_TIMER_FREEZE_TALK,         "timer_freeze_talk")                 \
    X(SETTING_TIMER_FREEZE_DEMO,         "timer_freeze_demo")                 \
    X(SETTING_TIMER_FREEZE_CLEANED,      "timer_freeze_cleaned")              \
    X(SETTING_TIMER_FREEZE_BOWSER,       "timer_freeze_bowser")               \
    X(SETTING_TIMER_FREEZE_YOSHI,        "timer_freeze_yoshi")                \
    X(SETTING_TIMER_FREEZE_TAKE,         "timer_freeze_take")                 \
    X(SETTING_TIMER_FREEZE_DROP,         "timer_freeze_drop")                 \
    X(SETTING_TIMER_FREEZE_PUT,          "timer_freeze_put")                  \
    X(SETTING_TIMER_FREEZE_TRIPLE_JUMP,  "timer_freeze_triple_jump")          \
    X(SETTING_TIMER_FREEZE_SPIN_JUMP,    "timer_freeze_spin_jump")            \
    X(SETTING_TIMER_FREEZE_LEDGE_GRAB,   "timer_freeze_ledge_grab")           \
    X(SETTING_TIMER_FREEZE_WALL_KICK,    "timer_freeze_wall_kick")            \
    X(SETTING_TIMER_FREEZE_ROPE_JUMP,    "timer_freeze_rope_jump")            \
    X(SETTING_TIMER_FREEZE_BOUNCE,       "timer_freeze_bounce")              \
    /* -- Attempt counter (append-only persisted ids) -- */                  \
    X(SETTING_ATTEMPT_COUNTER,           "attempt_counter")                  \
    X(SETTING_ATTEMPT_IN_STAGE_CONTROLS, "attempt_in_stage_controls")       \
    /* -- Pattern selector (append-only persisted id) -- */                  \
    X(SETTING_PATTERN_SELECTOR,          "pattern_selector")                \
    /* -- ILing (append-only persisted id) -- */                             \
    X(SETTING_ILING_FANFARE,             "iling_fanfare")                    \
    /* -- Visible goop (append-only persisted id) -- */                      \
    X(SETTING_VISIBLE_GOOP,              "visible_goop")                    \
    /* -- Inkstar requests (append-only persisted ids) -- */                 \
    X(SETTING_TIMER_FREEZE_JUMP,         "timer_freeze_jump")               \
    X(SETTING_TIMER_FREEZE_DIVE,         "timer_freeze_dive")               \
    X(SETTING_TIMER_FREEZE_DOUBLE_JUMP,  "timer_freeze_double_jump")        \
    X(SETTING_TIMER_FREEZE_PETEY_WAKEUP, "timer_freeze_petey_wakeup")       \
    X(SETTING_TIMER_FREEZE_EEL_ACTIVATE, "timer_freeze_eel_activate")       \
    X(SETTING_TIMER_FREEZE_EEL_TOOTH,    "timer_freeze_eel_tooth")          \
    X(SETTING_ILING_RECENT,              "iling_recent")                    \
    X(SETTING_TIMER_FREEZE_DIVE_ROLLOUT, "timer_freeze_dive_rollout")       \
    X(SETTING_TIMER_FREEZE_DIVE_GETUP,   "timer_freeze_dive_getup")          \
    /* -- Box game override (append-only persisted id) -- */                  \
    X(SETTING_FORCE_BOX_GAME,            "force_box_game")                  \
    /* -- IL PB controls (append-only persisted ids) -- */                    \
    X(SETTING_ILING_RECORDING,           "iling_recording")                 \
    X(SETTING_ILING_POPUP,               "iling_popup")                     \
    /* -- QFT section history (append-only persisted id) -- */                \
    X(SETTING_TIMER_SECTIONS,            "timer_sections")                  \
    /* -- Recent IL name format (append-only persisted id) -- */              \
    X(SETTING_ILING_SHORT_NAMES,         "iling_short_names")                \
    /* -- Savestate feedback (append-only persisted id) -- */                 \
    X(SETTING_SAVESTATE_FEEDBACK,        "savestate_feedback")               \
    /* -- Wallkick display (append-only persisted id) -- */                   \
    X(SETTING_WALLKICK_DISPLAY,          "wallkick_display")                \
    /* -- Restart queue feedback (append-only persisted id) -- */             \
    X(SETTING_RESTART_QUEUED_FEEDBACK,   "restart_queued_feedback")           \
    /* -- Gameplay presentation (append-only persisted ids) -- */              \
    X(SETTING_YOSHI_NOZZLE_SAVE_PROMPT,  "yoshi_nozzle_save_prompt")          \
    X(SETTING_HIDE_HUD_WHEN_PAUSED,      "hide_hud_when_paused")              \
    /* -- Starred-menu bit storage, seven setting ids per byte -- */           \
    X(SETTING_FAVORITES_0,               "favorites_0")                       \
    X(SETTING_FAVORITES_1,               "favorites_1")                       \
    X(SETTING_FAVORITES_2,               "favorites_2")                       \
    X(SETTING_FAVORITES_3,               "favorites_3")                       \
    X(SETTING_FAVORITES_4,               "favorites_4")                       \
    X(SETTING_FAVORITES_5,               "favorites_5")                       \
    X(SETTING_FAVORITES_6,               "favorites_6")                       \
    X(SETTING_FAVORITES_7,               "favorites_7")                       \
    X(SETTING_FAVORITES_8,               "favorites_8")                       \
    X(SETTING_FAVORITES_9,               "favorites_9")                       \
    X(SETTING_FAVORITES_10,              "favorites_10")                      \
    /* -- Achievement presentation (append-only persisted id) -- */           \
    X(SETTING_ACHIEVEMENT_NOTIFICATIONS, "achievement_notifications")         \
    /* -- Ghost presentation (append-only persisted ids) -- */                 \
    X(SETTING_GHOST_DISPLAY,             "ghost_display")                     \
    X(SETTING_GHOST_OPACITY,             "ghost_opacity")                     \
    /* -- V2 appearance controls (append-only persisted ids) -- */             \
    X(SETTING_HELMET_APPEARANCE,         "helmet_appearance")                 \
    X(SETTING_CAP_APPEARANCE,            "cap_appearance")                    \
    X(SETTING_SHADES_APPEARANCE,         "shades_appearance")                 \
    X(SETTING_SHINE_SHIRT_APPEARANCE,    "shine_shirt_appearance")            \
    /* -- V2 UI/QoL controls (append-only persisted ids) -- */                 \
    X(SETTING_METADATA_HORIZONTAL,       "metadata_horizontal")               \
    X(SETTING_DISABLE_RETAIL_PAUSE,      "disable_retail_pause")              \
    /* -- Ghost model selection (append-only persisted id) -- */               \
    X(SETTING_GHOST_APPEARANCE,          "ghost_appearance")                  \
    /* -- Automatic ghost target policy (append-only persisted id) -- */       \
    X(SETTING_GHOST_LAST_SUCCESS,        "ghost_last_success")                \
    /* PR2 assigned this id before the V2.0.3 merge; never swap the next two. */ \
    X(SETTING_STAGE_SESSION_DISPLAY,     "stage_session_display")             \
    /* -- PB ghost save policy (append-only persisted id) -- */                \
    X(SETTING_PB_GHOST_SAVE_POLICY,      "pb_ghost_save_policy")             \
    /* -- Additional timer event freeze (append-only persisted id) -- */       \
    X(SETTING_TIMER_FREEZE_MOVING_PLATFORM, "timer_freeze_moving_platform")   \
    /* -- V2.1.1 presentation/session controls (append-only persisted ids) -- */ \
    X(SETTING_LEVEL_SPLITS,                 "level_splits")                   \
    X(SETTING_STREAK_AUTO_RESET,            "streak_auto_reset")


#endif  // SUSAMUNE_SETTINGS_LIST_H
