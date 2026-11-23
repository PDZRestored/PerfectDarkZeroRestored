// Round 3c, Sun 15 Nov night, helper A (fix 1): THE LAYER'S PER-LEVEL MEMORY IS CLEARED AT EVERY LEVEL START.
// Included at file scope in lab_stealth.cpp, after every record of the layer is defined.
//
// The bug (play_mansion PDZRestored_005.log): the mission was restarted at 20:46:49. A mission restart keeps the AI
// clock running (122.3 s at that moment; it only starts at 0 on a load from the menu) and the level's row is the same,
// so LabActive's two tests ("another run key", "the AI clock went back") never fired: no "Mansion run" line was
// written and g_dead_lamps, g_kills, g_guards, the radio count and everything else lived on. At 20:47:37 pat2#2 "SEES
// A LAMP THAT IS OUT ... (it went out 156 s ago)": the record of 20:44:34, of a light the game had relit.
//
// A level start is now any of:
//   - Rare's level music setup ran (sub_82200738, src/music/music_restore.cpp: once at every level start, a mission
//     restart included: 15 of 15 level starts in the four levels' play logs of Sun 15 Nov);
//   - the Laboratory script's setup ran (sub_8231B778, lab_restorations.cpp);
//   - the run key changed (another level, another Laboratory script) or the AI clock went back (as before);
//   - in a solo game, player one's character is a new one (pdz_stealth_restart_by_player): the fallback for a reload
//     that would not run the music setup (a checkpoint, if Rare reloads one that way: not seen in a log yet).
// At each one R3cClearLayerMemory drops everything below and writes ONE line (always, not behind a log switch):
//   pdz_stealth_r3c: layer memory cleared (<why>): <level>: dropped ...
// A second signal of the same start (the Laboratory's script setup and the music setup come one after the other)
// does nothing (within 5 s of real time of the clear: the same level start; follow-up 1).
//
// What is cleared, and where (the full list is in the job folder's r3c\A_notes_Sun15Nov.md):
//   lab_stealth.cpp + lab_stealth_*.inc: every record below, under g_lock;
//   R3cBReset / R3cCReset / R3cDReset: round 3c's own new records (helpers B, C, D);
//   restorations.cpp (restorations::LevelReset), lab_dark_patches.cpp (lab_dark::LevelReset), lab_grate_sound.cpp
//   (lab_grate_sound::LevelReset), lab_cut_content.cpp (lab_cut::LevelReset): a flag each, applied on their own
//   thread at their next use;
//   lab_restorations.cpp: its state already starts over in the Laboratory script's setup hook.
// What is NOT cleared, on purpose: guest buffers of ours (cue references: g_cue_ref, g_siren_mem, g_swap_guest), data
// learned from the game's files (g_take_cache, g_cue_n, g_event_index), the music duck (DuckStep follows the real
// audio state), log throttles, and the test tools' "this value was already run" latches (a test command runs once per
// new value, as before; it is not run again by a restart).
#include "lab_grate_sound.h"
#include "bymodel_sounds_Wed18Nov.h"
#include "restored_voice_Wed18Nov.h"
namespace lab_cut // lab_cut_content.cpp
{
  void LevelReset();
}

REXCVAR_DEFINE_BOOL(pdz_stealth_restart_clear, true, "Gameplay",
                    "Stealth layer: everything the layer remembers of a level (lamps shot out, bodies, witnessed kills, "
                    "guard states, alarm and radio counts, searches, waiting lines) is forgotten at every level start: a "
                    "first load, a mission restart, a reload. One log line 'pdz_stealth_r3c: layer memory cleared' each "
                    "time. Off: only a load from the menu or another level clears it (the behaviour before)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_stealth_restart_by_player, true, "Gameplay",
                    "Stealth layer: in a solo game a new player-one character also counts as a level start (the fallback "
                    "for a reload that does not run Rare's level music setup). Needs pdz_stealth_restart_clear")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_INT32(pdz_test_stealth_level_start, 0, "Testing",
                     "TESTING TOOL: changing it to a value other than 0 makes the stealth layer treat this moment as a "
                     "level start: its whole per-level memory is cleared and the 'pdz_stealth_r3c: layer memory cleared' "
                     "line is written (the game itself is not restarted: guards alive are tracked again as calm). Once "
                     "per new value")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DECLARE(bool, pdz_test_god_mode); // (game_cvars.cpp)
REXCVAR_DEFINE_INT32(pdz_test_stealth_kill_jo, 0, "Testing",
                     "TESTING TOOL (for a scripted mission restart): changing it to a value other than 0 holds player "
                     "one's health at 0.5 for 30 s, so the next hit of any kind is her death through the game's own hit "
                     "and death path (the death line, the death screen, the restart). Run with pdz_test_god_mode off and "
                     "put her where a guard shoots (pdz_test_stealth_behind <id>,-150,0 = 1.5 m in front of him). Once "
                     "per new value; never acts unless set")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);

namespace
{
  int32_t g_r3c_test_done = 0;                               // pdz_test_stealth_level_start, as last run
  uint32_t g_r3c_jo = 0;                                    // player one's character at the last look
  std::chrono::steady_clock::time_point g_r3c_line_at{};     // real time of the last "cleared" line
  bool g_r3c_line_any = false;
  uint32_t g_r3c_clears = 0;                                 // clears in this session (the log line's number)
  constexpr double kR3cSameStartS = 5.0;                     // signals this close (real time) to a clear are the same level start

  // TESTING (follow-up 1): pdz_test_stealth_kill_jo. No routine of Rare's is known to kill a human safely from
  // outside a hit (guards have Kill at vtable +0x2E4 = 8252DE88; the human class has another function there; a call
  // of the player damage handler 825AAF50 leaves the health alone: counter_op.cpp's notes). What is proven there
  // (pdz_counterop_test_kill_agent) is used: the human's health (her participant, character +6560, its +980) is
  // set to 0.5, so the next real hit kills her through Rare's own hit handler and death path. Held for 30 s (a
  // health that grows back is put down again); nothing else of hers is touched.
  int32_t g_r3c_kill_jo_done = 0;
  float g_r3c_kill_jo_until = -1e9f;
  void R3cATestKillJo(uint8_t *base, uint32_t jo, float now)
  {
    if (const int32_t want = REXCVAR_GET(pdz_test_stealth_kill_jo); want != g_r3c_kill_jo_done)
    {
      g_r3c_kill_jo_done = want;
      g_r3c_kill_jo_until = want != 0 ? now + 30.0f : -1e9f;
      if (want != 0)
      {
        REXLOG_INFO("pdz_lab_stealth: test kill Jo (value {}): her health is held at 0.5 for 30 s: the next hit is her "
                    "death (Rare's own hit and death path){}",
                    want, REXCVAR_GET(pdz_test_god_mode) ? "; WARNING: pdz_test_god_mode is on: no hit will hurt her" : "");
      }
    }
    if (!(now < g_r3c_kill_jo_until))
    {
      return;
    }
    const uint32_t part = Ptr(jo) && Load32(base, jo) == 0x820C7DE0 ? Load32(base, jo + 6560) : 0; // (the human class)
    if (!Ptr(part))
    {
      REXLOG_INFO("pdz_lab_stealth: test kill Jo: player one {:08X} is not a human with a participant: nothing done", jo);
      g_r3c_kill_jo_until = -1e9f;
      return;
    }
    const float health = LoadF(base, part + 980);
    if (!(health > 0.0f))
    {
      REXLOG_INFO("pdz_lab_stealth: test kill Jo: her health is {:.2f}: she is dead; the hold ends", health);
      g_r3c_kill_jo_until = -1e9f;
      return;
    }
    if (health > 0.5f)
    {
      const float low = 0.5f;
      uint32_t bits;
      std::memcpy(&bits, &low, 4);
      Store32(base, part + 980, bits);
      REXLOG_INFO("pdz_lab_stealth: test kill Jo: her health {:.2f} -> 0.50 (participant {:08X})", health, part);
    }
  }

  // A level start? nullptr = no; else why (for the log line). Called by LabActive once per AI clock step (and at
  // once when a signal is waiting), before its own run test.
  const char *R3cLevelStart(const uint8_t *base, float now, uint32_t run_key, bool lab)
  {
    const int signal = g_r3c_signal.exchange(0, std::memory_order_relaxed);
    const uint32_t jo = Load32(base, kPlayerOne);
    const bool new_jo = Ptr(jo) && g_r3c_jo != 0 && jo != g_r3c_jo;
    if (Ptr(jo))
    {
      g_r3c_jo = jo;
    }
    if (const int32_t test = REXCVAR_GET(pdz_test_stealth_level_start); test != g_r3c_test_done)
    {
      g_r3c_test_done = test;
      if (test != 0)
      {
        g_r3c_line_any = false; // (a test clear always writes its line)
        return "TEST: pdz_test_stealth_level_start (the game was not restarted)";
      }
    }
    if (!REXCVAR_GET(pdz_stealth_restart_clear))
    {
      return nullptr;
    }
    const char *why = signal == 2                           ? "level start: the Laboratory's script was set up"
                      : signal != 0                         ? "level start: Rare's level setup ran (a first load, a mission restart or a reload)"
                      : lab && run_key != g_run_script      ? "level start: another level or another run of its script"
                      : now + 1.0f < g_prev_clock           ? "level start: the AI clock went back"
                      : new_jo && GameType(base) == 0 && REXCVAR_GET(pdz_stealth_restart_by_player)
                          ? "level start: player one's character is a new one (a restart or a reload)"
                          : nullptr;
    // Follow-up 1 (smoke log r3c_mansion3, 00:50:20.995 / 00:50:21.280): ONE level start gives two or three of these
    // signals within a second (a first load: the run key, then the music setup 0.3 s later; the Laboratory: its
    // script's setup, then the music setup; a restart: the music setup, then the new player one). Only the first
    // clears; the others, within kR3cSameStartS of real time of that clear, are the same start and do nothing: no
    // second clear (which was silent, but counted, and could have dropped records made just after the start), so
    // the "clear N of this session" count and the "layer memory cleared" lines agree. No level can be set up twice
    // within that time (a level load alone takes longer).
    if (why && g_r3c_line_any)
    {
      const double since = std::chrono::duration<double>(std::chrono::steady_clock::now() - g_r3c_line_at).count();
      if (since < kR3cSameStartS)
      {
        if (StealthLog())
        {
          REXLOG_INFO("pdz_stealth_r3c: a further signal of the same level start ({}) {:.1f} s after the clear: nothing "
                      "is cleared again",
                      why, since);
        }
        return nullptr;
      }
    }
    return why;
  }

  void R3cClearLayerMemory(const uint8_t *base, const char *why)
  {
    // The other files: a flag each (no lock of theirs is taken here).
    restorations::LevelReset();
    lab_dark::LevelReset();
    lab_grate_sound::LevelReset();
    bymodel_sounds::LevelReset();
    pdz::snd::LevelReset();
    lab_cut::LevelReset();
    // The sound followers have locks of their own: never waited for under g_lock (try only; what stays ages out in
    // seconds and is log-only).
    size_t watched = 0, alarm_sounds = 0;
    {
      std::unique_lock<std::mutex> hold(g_watch_lock, std::try_to_lock);
      if (hold.owns_lock())
      {
        watched = g_watched.size();
        g_watched.clear();
        g_watch_n.store(0, std::memory_order_relaxed);
      }
    }
    {
      std::unique_lock<std::mutex> hold(g_alarm_snd_lock, std::try_to_lock);
      if (hold.owns_lock())
      {
        alarm_sounds = g_alarm_sounds.size();
        g_alarm_sounds.clear();
        g_alarm_snd_n.store(0, std::memory_order_relaxed);
        g_siren_known.store(0, std::memory_order_relaxed); // (learned again when the Laboratory asks for its sirens)
        g_siren_script = 0;
        std::memset(g_siren_pos, 0, sizeof(g_siren_pos));
      }
    }
    std::lock_guard<std::recursive_mutex> hold(g_lock);
    // --- what is dropped, for the line -------------------------------------------------------------------------
    size_t guards = 0, staff = 0, not_calm = 0, dead = 0;
    for (const auto &[c, g] : g_guards)
    {
      (g.scientist ? staff : guards) += 1;
      if (g.dead || g.st == St::Dead)
      {
        ++dead;
      }
      else if (g.st != St::Unaware || g.meter > 0.5f)
      {
        ++not_calm;
      }
    }
    const size_t n_chars = g_chars.size(), n_kills = g_kills.size(), n_corpses = g_corpses.size();
    const size_t n_dead_lamps = g_dead_lamps.size(), n_lamps_out = g_lamps_out.size();
    const size_t n_hunts = g_hunts.size(), n_shots = g_shots.size(), n_blasts = g_blasts.size();
    const size_t n_lines = g_lines.size() + g_said_lines.size() + g_take_orders.size() + g_set_plays.size();
    const size_t n_area = g_area.size(), n_heard = g_heard.size(), n_fallback = g_fallback.size();
    const size_t n_radio = g_radio_calls.size(), n_r3b = g_r3b.size(), n_r3h = g_r3h.size(), n_shouts = g_r3h_shouts.size();
    const size_t n_squads = g_squad_deaths.size() + g_squad_escalated.size() + g_squad_hides.size();
    const int radio_sent = g_radio_sent, alarms = g_alarm_no;
    const bool was_loud = g_loud, alarm_was = g_alarm_on || g_alarm_was;

    // --- lab_stealth.cpp: who is who --------------------------------------------------------------------------
    g_members.clear();
    g_survey_seen.clear();
    g_jo_zone_seen = -2;
    g_jo_inside = -1;
    g_chars.clear();
    g_guards.clear();
    g_name_owners.clear();
    g_census.clear();
    g_observed.clear();
    g_stack_deepest = 0;
    g_setting_logged = 0xFFFFFFFFu;
    // --- LOUD (rounds 3 to 11l) -------------------------------------------------------------------------------
    g_loud = g_loud_released = false; // (g_lab follows in LabActive, right after this call)
    g_loud_commit_until = -1e9f;
    // --- Jo -----------------------------------------------------------------------------------------------------
    g_jo_t = -1.0f;
    g_jo_speed = 0.0f;
    g_jo_prev[0] = g_jo_prev[1] = g_jo_prev[2] = 0.0f;
    g_step_value = 0.0f;
    g_step_at = -1e9f;
    g_step_radius = 0.0f;
    g_step_seen = -1e9f;
    g_any_sighting = false;
    g_sight_blocked = 0;
    g_sight_block_log = -1e9f;
    g_combat_note = false;
    g_combat_exempt = 0;
    // --- sounds heard ---------------------------------------------------------------------------------------------
    g_shots.clear();
    g_blasts.clear();
    g_heard.clear();
    g_story_sound = 0;
    g_story_at = -1e9f;
    g_story_name.clear();
    g_story_log = -1e9f;
    // --- kills, bodies, searches --------------------------------------------------------------------------------
    g_kills.clear();
    g_corpses.clear();
    g_squad_deaths.clear();
    g_squad_escalated.clear();
    g_squad_hides.clear();
    g_hunts.clear();
    g_area.clear();
    g_fallback.clear();
    g_give_refused = 0;
    g_give_refused_log = -1e9f;
    g_push_logged = 0;
    g_stack_refused = 0;
    g_stack_refused_log = -1e9f;
    // --- lamps ----------------------------------------------------------------------------------------------------
    g_lamps_out.clear();
    g_dead_lamps.clear();
    g_next_brief = false;
    g_incident = Incident();
    g_talk_check = -1e9f;
    g_mesa_done = false;
    g_moon = Moon(); // (the moon is read from the level's light list again at the next light step)
    // --- alarm, radio, reinforcement waves ----------------------------------------------------------------------
    g_alarm_not_before = -1e9f;
    g_alarm_runner = g_alarm_deferred = 0;
    g_alarm_was = false;
    g_alarm_on = false;
    g_alarm_tick = -1.0f;
    g_alarm_no = 0;
    g_alarms_ended = 0;
    g_alarm_start = 0.0f;
    g_alarm_seen = 0.0f;
    g_alarm_seen_log = -1e9f;
    g_alarm_by_us = g_alarm_by_test = false;
    g_alarm_ended_at = -1e9f;
    g_alarm_snd_summary = g_alarm_snd_tick = -1e9f;
    g_siren_try = -1e9f;
    g_waves_held = g_waves_passed = g_waves_spawned = 0;
    g_waves_log = -1e9f;
    g_radio_calls.clear();
    g_radio_sent = 0;
    // --- lines: waiting, said, the tannoy, the mutters, the cue swap ------------------------------------------
    g_lines.clear();
    g_said_lines.clear();
    g_take_orders.clear();
    g_set_plays.clear();
    g_set_release.clear();
    g_primed.clear();
    g_plays.clear();
    g_play_count = g_play_overlaps = 0;
    g_holds = 0;
    g_not_played = 0;
    g_rare_dropped = 0;
    g_voice_last = -1e9f;
    g_rare_last = -1e9f;
    g_last_line_end = -1e9f;
    g_last_play_end = -1e9f;
    g_mutter_next = -1.0f;
    g_mutter_ev = 0;
    g_mutter_wait = 0;
    g_mutter_wait_at = -1e9f;
    g_mutter_wait_ev = 0;
    g_mutter_skip.clear();
    g_tannoy_want = -1.0f;
    g_tannoy_last = -1e9f;
    g_tannoy_cannot_locate = false;
    g_tannoy_why.clear();
    g_tannoy_by = 0;
    g_tannoy_takes = 0;
    g_tannoy_takes_at = -1e9f;
    g_allclear_at = g_allclear_until = -1e9f;
    g_allclear_by = 0;
    g_allclear_try = -1e9f;
    g_next_swap = nullptr;
    g_next_swap_takes = 0;
    DisarmSwap();
    g_swap_from.clear();
    g_swap_only.clear();
    g_swap_only2.clear();
    g_swap_to = 0;
    g_swap_c = 0;
    g_swap_at = -1e9f;
    // --- rounds 3 and 3b (their own files) ------------------------------------------------------------------------
    g_r3h.clear();
    g_r3h_shouts.clear();
    g_r3h_scan = -1e9f;
    g_r3b.clear();
    g_r3b_kill_at = R3BKillAt(); // (a test kill still waiting for its guard: he is gone)
    g_r3c_kill_jo_until = -1e9f; // (pdz_test_stealth_kill_jo's hold: the new attempt's Jo is left alone)
    // --- round 3c's own records -----------------------------------------------------------------------------------
    R3cBReset();
    R3cCReset();
    R3cDReset();
    R4CReset(); // (round 4 C, Mon 16 Nov)
    R5BReset(); // (round 5 B, Mon 16 Nov)

    ++g_r3c_clears; // (one clear = one line: R3cLevelStart lets only the first signal of a level start through)
    g_r3c_line_any = true;
    g_r3c_line_at = std::chrono::steady_clock::now();
    const StealthLevel *row = RowOfLevel(base);
    const std::string mission = row ? std::string(row->name) : pdz::pace::SpLevelName(base);
    REXLOG_INFO("pdz_stealth_r3c: layer memory cleared ({}): {} at AI clock {:.1f} s (clear {} of this session): dropped {} "
                "guard and {} scientist record(s) ({} not calm, {} dead) of {} character(s) seen, {} kill(s) of Jo's and {} "
                "body record(s), {} lamp(s) on record as shot out (+{} in the spree list), {} search plan(s), {} "
                "investigated spot(s), {} search pad record(s), {} squad record(s), {} shot(s) and {} blast(s) heard, {} "
                "hearing record(s), {} radio call(s) under way and {} sent, {} alarm(s){}{}, {} waiting or said line "
                "record(s), {} sound follower(s), {} heard-reaction, {} body-once and {} shout record(s); the blackout "
                "talk, the light measure and the vent grates start over too. Nothing of the attempt before is known now",
                why, mission.empty() ? std::string("no campaign level") : mission, Clock(base), g_r3c_clears, guards, staff,
                not_calm, dead, n_chars, n_kills, n_corpses, n_dead_lamps, n_lamps_out, n_hunts, n_area, n_fallback, n_squads,
                n_shots, n_blasts, n_heard, n_radio, radio_sent, alarms, alarm_was ? " (one was on)" : "",
                was_loud ? " (Jo had gone loud)" : "", n_lines, watched + alarm_sounds, n_r3h, n_r3b, n_shouts);
  }
} // namespace

namespace lab_stealth
{
  void LevelStartSignal(int what)
  {
    g_r3c_signal.store(what == 2 ? 2 : 1, std::memory_order_relaxed);
    if (!REXCVAR_GET(pdz_lab_guard_stealth))
    {
      // The master switch is off (the Gameplay tab's Stealth switch, or Original): the layer never looks at the level,
      // so nothing takes the signal above. The restorations and fixes that do not belong to the layer (the blackout
      // lines, the vent grates and their sound) still have to forget the attempt before: a flag each, as in
      // R3cClearLayerMemory. And the log says once per level start that the layer is off.
      restorations::LevelReset();
      lab_dark::LevelReset();
      lab_grate_sound::LevelReset();
      bymodel_sounds::LevelReset();
      pdz::snd::LevelReset();
      lab_cut::LevelReset();
      if (what != 2)
      {
        REXLOG_INFO("pdz_stealth: level start: the stealth layer is OFF on every level (pdz_lab_guard_stealth = false, "
                    "the Gameplay tab's Stealth switch): guards see, hear and react as in Rare's game");
      }
    }
  }
} // namespace lab_stealth
