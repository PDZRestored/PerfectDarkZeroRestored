// Restored sounds, round 1 (Tue 17 Nov 2026). TU3 addresses. Master switch pdz_restored_sounds (on); each sound has
// its own switch pdz_snd_<name> (on).
//
// The rule for everything in this file: (1) the recording is Rare's own, made for that thing; (2) the moment exists
// in Rare's game; (3) it is proven silent today. Only the sound is added: no noise for the AI, no perception write.
//
// a. BODY FALLS (pdz_snd_body_fall).
//    Rare's body-fall sound is an ANIMATION EVENT: the event dispatcher sub_821EDD40 calls sub_821EDA18(r4 = the
//    character). In water (sub_82506C18) that plays STOCK_BODYFALL_WATER with a splash; otherwise it calls
//    sub_821FFD48(character): the surface under him (character +228, its virtual +116, the surface record of
//    sub_824F4E68) gives the cue in its body-fall slot (+136: STOCK_BODYFALL_GENERIC / _WOOD / _METAL), started at the
//    character's place (+48) with the cue's own loudness and range; then sub_8218AD40 makes a noise for the AI.
//    A character killed into a ragdoll plays no such event: no ragdoll or corpse code asks for a sound (the sound
//    audit), and the play census of Tue 17 Nov has kills on the Nightclub docks and no body-fall cue at all.
//    Here: the ragdoll controllers (the list at 82E3C484, updated by sub_825EA370) are watched once a frame. An
//    active ragdoll that is fully physical or at least half way there (not one that still follows a death animation:
//    such a death plays Rare's own event, seen in a check run) whose first body was falling faster than pdz_snd_body_fall_speed and has just stopped falling has
//    hit the ground: the nearest dead human figure within 3 m (noted by the character tick below) gets Rare's own
//    surface look-up and cue through sub_821FFD48 (in water: sub_821EDA18, whose water branch makes no AI noise),
//    once per death. The AI noise call is NOT made: guards do not hear it. A figure whose own animation event has
//    played a body fall in the last 3 s is left alone, so nothing is doubled.
//
// b. SPARKS AFTER A LIGHT IS SHOT OUT (pdz_snd_light_sparks).
//    A shot on a light surface runs sub_825F3978(r3 = &hit place, r4 = the prop hit or 0). When the light it takes
//    (the prop's linked light, prop +812, or the pick of sub_825F3CD8) is on, it puts the light out (sub_82602648) and
//    makes the spark effect at the hit (sub_82612330, effect 13). It asks for no sound: the surface's bullet-hit
//    sound (the glass break) is heard and the sparks are silent (heard so in play). The stock bank has
//    STOCK_SPARKING (two takes of sparking) and STOCK_SPARK_SMALL (four single sparks); nothing names either.
//    Here: when Rare makes those sparks, the cue of pdz_snd_light_sparks_cue is started at the hit place with the
//    cue's own loudness and Rare's distance fade (sub_82202D20, sub_821FF428, sub_822061C8), at most once per
//    0.25 s. The glass break is untouched. No noise for the AI.
//
// Round 2 (Tue 17 Nov night, job fix2):
// a. Log 017 / 018: ten ragdolls landed 1.4 m from the kill and "no dead figure ... within 300 cm": the dead-figure
//    list is filled from the NPC sight update, which Rare does not run for a dying character, so it stayed empty. The
//    ragdoll's own character is now taken directly: its controller sits at character +864 (seen in the check run),
//    an NPC (class 33: the round 1 test for 2 never matched one) with no health left. The list stays as the fallback. Once per death; the animation's own
//    body fall still wins (no doubling).
// b. The spark sound now follows Rare's spark effect itself: sub_82612330 with effect 13 (electrical sparks), from
//    any caller. From the light code (the shot 825F3978 and the blast 825F3A10) it is pdz_snd_light_sparks as before
//    (the guess in the light hook is gone); from anything else (a shot TV or wall monitor, an electrical prop) it is
//    pdz_snd_effect_sparks. Default cue STOCK_SPARK_SMALL (the tester's choice, Tue 17 Nov). A "could not be started"
//    line now gives the distance to Jo (the sound manager refuses a sound beyond its cue's range).
//    pdz_snd_effect_census (log only): one line per effect number and caller the first time Rare makes it, so a
//    spark that is not effect 13 can be named from a play log.
//
// Thu 19 Nov night (log 025: 3 of 8 deaths got a body fall; the bouncer s02b, killed from 2.9 m, slumped through
// his death animation at 0.0-0.3 m/s and no fast landing was ever counted): every death that is seen to happen (the
// NPC was alive in the last ~3 s: never a figure placed dead) is followed once a frame (FollowDeaths): one body fall
// when the body has settled (from 2.5 s after the death, his ragdoll's first body still for 0.5 s), or at the latest
// 6 s after the death; whatever the speed. The fast landing above still plays first when it happens. Once per death;
// a death whose own animation plays Rare's body-fall event gets nothing of ours (a death by Rare's Kill played it
// 1.7 s on in the night's smoke run: hence the 2.5 s). Same cue look-up, loudness and range as before. A refusal
// because Jo is past the range is said so.
//
// Fri 20 Nov night (log 032, Datacore): (a) 22:07:55 ours played 3.9 s after the death ("settled"), (b) three fast
// landings at 22:06:47-49 (5-6.8 m/s) played nothing: their controllers (00DB6960, 00DC1AE0, 00852360) are not inside a
// character (character +864), so "his own" was not found and no dead figure was within 3 m, (c) the game's own
// animation body falls came 1.7-3.9 s after ours, up to four for one death (22:13:37-39).
//   pdz_snd_body_fall_owner: a ragdoll controller's owner is its +36 (sub_825EA370 calls the owner's vfunc +232 and
//     reads its animation, +72, through it): the landing is matched to that character (an NPC, class 33, no health).
//   pdz_snd_body_fall_first_floor: a followed death plays at the body's FIRST floor contact: its ragdoll's first body
//     has come down at least 25 cm since the death, was falling at 0.5 m/s or more, and has stopped falling (under a
//     quarter of its fastest, or 0.25 m/s) for two frames. No 2.5 s wait and no "settled" / 6 s late fall while a body
//     is followed (a death with no body to follow keeps the settled rule). The sound is made at the body.
//   pdz_snd_body_fall_once: one per death: a game's animation body-fall event for a dead NPC whose death already had a
//     body fall (ours or the game's) in the last 720 frames (6 s at 120) is made silent (its range 1 cm, so the game's
//     sound manager refuses it; the event's noise for the guards is unchanged).
//   The game's own body falls get 12 m (pdz_snd_body_fall_rare_range, restored_voice_Wed18Nov.cpp: logged now).
#include <algorithm>
#include <atomic>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/memory/utils.h>
#include <rex/ppc/func.h>
#include <rex/ppc/stack.h>
#include <rex/system/kernel_state.h>

#include "../game_cvars.h" // pdz_sound_log
#include "guest_function.h"
#include "restored_voice_Wed18Nov.h"
#include "guest_readable_Sat21Nov.h" // Sat 21 Nov: pdz_fix_readable_cache

REXCVAR_DEFINE_BOOL(pdz_restored_sounds, true, "Gameplay",
                    "Restored sounds: recordings the game has for a thing and a moment that exist in it, where "
                    "nothing is played today. Each has its own switch (pdz_snd_...). Off = all of them off")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_fix_body_fall_dead_controller, true, "Gameplay",
                    "Fix (Sun 22 Nov, default on): the body-fall sound only asks the floor of a dead guard whose movement "
                    "controller is still the game's own; a guard killed on a jetpack (shot, or the jetpack destroyed) has a "
                    "torn-down one, and asking it closed the game (the kernel bug check after a pure-virtual call). His "
                    "body fall uses the default floor instead. Off = as before (the crash comes back)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_snd_body_fall, true, "Gameplay",
                    "Restored sound: a body that falls as a ragdoll makes the game's body-fall sound for the floor "
                    "it lands on (plain, wood, metal, water) when it first hits the ground. Sound only: guards do "
                    "not hear it. Needs pdz_restored_sounds")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_DOUBLE(pdz_snd_body_fall_speed, 1.2, "Gameplay",
                      "pdz_snd_body_fall: the falling speed (m/s) a ragdoll must have reached for its landing to "
                      "count as a fall")
    .range(0.2, 20.0)
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_DOUBLE(pdz_snd_body_fall_volume, 1.0, "Gameplay",
                      "pdz_snd_body_fall: the loudness of the body-fall sound (0 to 1; 1 = the game's own)")
    .range(0.0, 1.0)
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_DOUBLE(pdz_snd_body_fall_range_m, 12.0, "Gameplay",
                      "pdz_snd_body_fall: how far the body-fall sound is heard (metres; 12 since Fri 20 Nov = the game's "
                      "footstep range). The game's own rule gives body falls 4 m, and its sound fades to nothing at the "
                      "range, so a fall 1.5 m away was quiet. Also used for the game's own falls (pdz_snd_body_fall_rare_range)")
    .range(1.0, 30.0)
    .lifecycle(rex::cvar::Lifecycle::kHotReload);

// Fri 20 Nov night (log 032): see the header, "Fri 20 Nov night".
REXCVAR_DEFINE_BOOL(pdz_snd_body_fall_first_floor, true, "Gameplay",
                    "Restored sound (Fri 20 Nov night): a body fall plays the moment the body first hits the floor (its "
                    "ragdoll body stops falling), not when it has settled seconds later. Off = when settled (2.5 s on at "
                    "the earliest), as before. Needs pdz_snd_body_fall")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_snd_body_fall_owner, true, "Gameplay",
                    "Restored sound (Fri 20 Nov night): a ragdoll that lands is matched to its own character (the ragdoll "
                    "controller's owner), never to the nearest dead figure. Off = by distance (3 m), as before. Needs "
                    "pdz_snd_body_fall")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_snd_body_fall_once, true, "Gameplay",
                    "Restored sound (Fri 20 Nov night): exactly one body fall per death: once one has played (ours or the "
                    "game's own), the game's further body-fall events for the same dead character are silent (the guards "
                    "still hear them). Off = every event plays, as before. Needs pdz_snd_body_fall")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_snd_light_sparks, true, "Gameplay",
                    "Restored sound: the sparks the game makes when a light is shot out have their own sparking "
                    "sound, at the light. The glass break plays as before. Sound only: guards do not hear it. "
                    "Needs pdz_restored_sounds")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_STRING(pdz_snd_light_sparks_cue, "XACT_SOUNDBANK_STOCK_SPARK_SMALL", "Gameplay",
                      "pdz_snd_light_sparks and pdz_snd_effect_sparks: the cue (full name). "
                      "XACT_SOUNDBANK_STOCK_SPARKING gives the longer sparking instead of a single small spark")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);

REXCVAR_DEFINE_BOOL(pdz_snd_effect_sparks, true, "Gameplay",
                    "Restored sound: wherever else the game makes its electrical sparks (a shot TV or wall monitor, "
                    "an electrical prop), they make the same small spark sound, at the sparks. Sound only. Needs "
                    "pdz_restored_sounds")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_snd_effect_census, true, "Debug",
                    "Log only: one 'pdz_snd_effects:' line per effect number and calling routine the first time the "
                    "game makes that effect in a session (to name the effect behind a silent spark or splash)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);

REX_EXTERN(sub_82202C78);        // the name resolver (r3 = name): the cue, or -1
REX_EXTERN(sub_82202D20);        // cue reference (r3 = ref {char name[256]; cue}, r4 = name)
REX_EXTERN(sub_821FF428);        // sound from a reference (r3 = ref, r4 = position or 0, r5, r6): the sound, or 0
REX_EXTERN(sub_822061C8);        // play (r3 = sound, f1 = delay in seconds)
REX_EXTERN(__imp__sub_821EDA18); // Rare's body-fall event: r4 = the character
REX_EXTERN(sub_821FFD48);        // the surface's body-fall cue at the character: r3 = the character
REX_EXTERN(sub_824F4E68);        // surface number -> its record (+136 the body-fall cue name) (Thu 19 Nov)
REX_EXTERN(sub_82506C18);        // is the character in water: r3 = the character -> r3 (byte)
REX_EXTERN(__imp__sub_82612330); // Rare's effect at a point: r3 &pos, r4 1, r5 the effect (13 electrical sparks), r6, r7

namespace pdz::restored_sounds
{
  bool LightSparksOn();                                               // from the light-shot hook (lab_light_shot_log.cpp)
  void LightSparks(PPCContext &ctx, uint8_t *base, const float at[3]);
  void SparkSound(PPCContext &ctx, uint8_t *base, const float at[3], bool light, uint32_t caller);
  void CharacterTick(const uint8_t *base, uint32_t character);        // from the NPC sight update hook (lab_stealth.cpp)
  void RagdollTick(PPCContext &ctx, uint8_t *base, double frame_dt);  // from the ragdoll update hook
  void LevelReset(); // Sun 22 Nov night (audit): from the level music setup (music_restore.cpp), any level start / restart
} // namespace pdz::restored_sounds

namespace
{
  constexpr uint32_t kGame = 0x82D6BFD8;
  constexpr uint32_t kFrameCounter = kGame + 136; // one per Swap
  constexpr uint32_t kType = 12;                  // character: its class number
  constexpr uint32_t kNpcType = 33;               // (fix2: every NPC in a play log; 2 was the player's branch, never an NPC)
  constexpr uint32_t kPosition = 48;              // character: x, y, z (cm)
  constexpr uint32_t kHealth = 120;               // character: float
  // The ragdoll controllers (ragdoll_step_fix_Mon16Nov.cpp).
  constexpr uint32_t kControllerList = 0x82E3C484;
  constexpr uint32_t kControllerNext = 44, kControllerRagdoll = 48, kControllerActive = 84, kControllerPhysical = 85;
  constexpr uint32_t kControllerMode = 60, kControllerWeight = 64; // mode 0 animation, 1-3 ragdoll; weight 0 -> 1
  constexpr uint32_t kControllerInCharacter = 0x360;               // a character's own controller sits at character +864
  constexpr float kRagdollWeight = 0.5f;                           // from here the body is more ragdoll than animation
  constexpr uint32_t kConstraintBodyA = 16, kBodyMotion = 80, kMotionPosition = 64; // metres
  constexpr float kNearCm = 300.0f;
  constexpr uint32_t kRealHoldFrames = 360; // an animation body fall this lately: nothing of ours
  constexpr uint32_t kControllerOwner = 36;  // (Fri 20 Nov night: a ragdoll controller's owner, sub_825EA370)
  constexpr uint32_t kOnceFrames = 720;      // (Fri 20 Nov night: pdz_snd_body_fall_once's window)

  uint32_t Load32(const uint8_t *base, uint32_t a)
  {
    const uint8_t *p = base + a;
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
  }
  float LoadF(const uint8_t *base, uint32_t a) { return std::bit_cast<float>(Load32(base, a)); }
  bool Plausible(uint32_t p) { return p >= 0x10000u && p < 0xF0000000u && (p & 3u) == 0; }

  // The dead human figures seen lately (the game thread only).
  struct Dead
  {
    uint32_t c = 0;
    uint32_t seen = 0;      // the frame he was last seen dead
    uint32_t real = 0;      // the frame of his own animation body fall (0: none)
    bool played = false;    // this death is done (ours played, or the game's own came first)
    bool ours = false;      // ours was really started for this death
    // Thu 19 Nov night (log 025: 3 of 8 deaths had a body fall; the bouncer s02b slumped through his death animation
    // at 0.0-0.3 m/s and no "landing" was ever counted): a death seen to happen (he was alive a moment before) is
    // followed until the body is on the floor and still, or has settled, whatever the speed.
    bool dying = false;     // followed now
    uint32_t death_frame = 0;
    float t = 0.0f;         // seconds since the death was first seen
    float floor_cm = 0.0f;  // his feet's height when he died (cm)
    bool have_y = false;
    float start_y = 0.0f, last_y = 0.0f; // the ragdoll's first body (m)
    float still_t = 0.0f;   // seconds the body has been still
    // Fri 20 Nov night: the fastest fall of the body since the death (m/s), frames it has been stopped after it, the
    // frame of this death's first body fall (ours or the game's; 0: none yet).
    float peak_down = 0.0f;
    int stopped_frames = 0;
    uint32_t fall_frame = 0;
    uint32_t controller = 0; // (his ragdoll controller found by its owner pointer; 0: character +864)
  };
  Dead g_dead[48];
  int g_dead_next = 0;
  // The NPCs seen alive lately (the game thread only): a death is followed only when it was seen to happen, never
  // for a figure placed dead or dead since long ago.
  struct AliveSeen
  {
    uint32_t c = 0, frame = 0;
  };
  AliveSeen g_alive[256];
  constexpr uint32_t kFreshDeathFrames = 360; // alive this lately (frames, ~3 s at 120): a death that just happened
  // The last animation body-fall events (any character, alive or dying).
  struct Real
  {
    uint32_t c = 0, frame = 0;
  };
  Real g_real[16];
  int g_real_next = 0;
  // The ragdolls.
  struct Rag
  {
    uint32_t controller = 0;
    bool active = false, have = false, done = false;
    float pos[3] = {};
    float fastest = 0.0f; // the fastest fall so far (m/s, positive down)
  };
  Rag g_rag[64];
  uint64_t g_played = 0, g_unmatched = 0, g_skipped_real = 0;
  uint64_t g_ground_refused = 0; // Sun 22 Nov: body falls whose movement controller was not the game's own
  std::atomic<bool> g_level_reset{false}; // Sun 22 Nov night (audit): the remembered figures are dropped at the next use
  // Sun 22 Nov: the game's movement controller classes (the three vtables whose slot +116 is the real 825DD500).
  bool MoveController(uint32_t vt) { return vt == 0x820CB320u || vt == 0x820CB478u || vt == 0x820CB4F8u; }

  bool On() { return REXCVAR_GET(pdz_restored_sounds) && REXCVAR_GET(pdz_snd_body_fall); }

  Dead *FindDead(uint32_t c)
  {
    for (Dead &d : g_dead)
    {
      if (d.c == c)
      {
        return &d;
      }
    }
    return nullptr;
  }

  void MarkAlive(uint32_t c, uint32_t frame)
  {
    const uint32_t h = (c >> 4) & 255u;
    int empty = -1, oldest = -1;
    for (uint32_t k = 0; k < 8; ++k)
    {
      AliveSeen &a = g_alive[(h + k) & 255u];
      if (a.c == c)
      {
        a.frame = frame;
        return;
      }
      if (!a.c && empty < 0)
      {
        empty = int((h + k) & 255u);
      }
      if (oldest < 0 || frame - a.frame > frame - g_alive[oldest].frame)
      {
        oldest = int((h + k) & 255u);
      }
    }
    g_alive[empty >= 0 ? empty : oldest] = {c, frame};
  }

  // The frame he was last seen alive, or 0 (never, or forgotten).
  uint32_t AliveFrame(uint32_t c)
  {
    const uint32_t h = (c >> 4) & 255u;
    for (uint32_t k = 0; k < 8; ++k)
    {
      if (g_alive[(h + k) & 255u].c == c)
      {
        return g_alive[(h + k) & 255u].frame;
      }
    }
    return 0;
  }

  // A dead NPC seen (by the character tick or his own ragdoll controller): his death is followed from here if it
  // just happened. The record of an earlier death at the same address is renewed.
  void NoteDeath(const uint8_t *base, uint32_t c, uint32_t frame)
  {
    const uint32_t alive = AliveFrame(c);
    if (!alive || frame - alive > kFreshDeathFrames)
    {
      return; // (not seen alive just before: placed dead, or dead for long; nothing of ours)
    }
    Dead *d = FindDead(c);
    if (d && (d->dying || d->played) && d->death_frame && int32_t(alive - d->death_frame) <= 0)
    {
      return; // (this death is already followed or done)
    }
    if (!d)
    {
      d = &g_dead[g_dead_next];
      g_dead_next = (g_dead_next + 1) % int(sizeof(g_dead) / sizeof(g_dead[0]));
    }
    *d = Dead();
    d->c = c;
    d->seen = frame;
    d->dying = true;
    d->death_frame = frame;
    d->floor_cm = LoadF(base, c + kPosition + 4);
  }

  bool BodyPosition(const uint8_t *base, uint32_t controller, float pos[3])
  {
    // Sun 22 Nov night (audit): every hop of the chain is read only where it is mapped (a cached or torn-down
    // controller's words can be floats: 3F800000 passes Plausible).
    if (!pdz::guestmem::Ok(base, controller, kControllerRagdoll + 4))
    {
      return false;
    }
    const uint32_t ragdoll = Load32(base, controller + kControllerRagdoll);
    if (!Plausible(ragdoll) || !pdz::guestmem::Ok(base, ragdoll, 28))
    {
      return false;
    }
    const uint32_t array = Load32(base, ragdoll + 20), count = Load32(base, ragdoll + 24);
    if (!Plausible(array) || !count || count > 64 || !pdz::guestmem::Ok(base, array, 4))
    {
      return false;
    }
    const uint32_t constraint = Load32(base, array);
    const uint32_t body = Plausible(constraint) && pdz::guestmem::Ok(base, constraint, kConstraintBodyA + 4)
                              ? Load32(base, constraint + kConstraintBodyA)
                              : 0;
    const uint32_t motion = Plausible(body) && pdz::guestmem::Ok(base, body, kBodyMotion + 4) ? Load32(base, body + kBodyMotion) : 0;
    if (!Plausible(motion) || !pdz::guestmem::Ok(base, motion, kMotionPosition + 12))
    {
      return false;
    }
    for (int i = 0; i < 3; ++i)
    {
      pos[i] = LoadF(base, motion + kMotionPosition + 4 * i);
    }
    return std::isfinite(pos[0]) && std::isfinite(pos[1]) && std::isfinite(pos[2]);
  }

  void PlayFall(PPCContext &ctx, uint8_t *base, Dead *best, const std::string &how, const float *at_cm = nullptr);

  // Fri 20 Nov night (pdz_snd_body_fall_owner): the dead NPC a ragdoll controller belongs to (its owner, +36), or 0.
  // The owner word is proven readable once per controller and value (a page query: only when it changes), never read
  // through before that.
  struct OwnerSeen
  {
    uint32_t controller = 0, owner = 0;
    bool ok = false;
  };
  OwnerSeen g_owner_seen[64];
  // Sun 22 Nov night (audit): a level start / restart: every remembered character, controller and ragdoll is forgotten
  // (the game thread, at the first use after the signal).
  void ApplyLevelReset()
  {
    if (!g_level_reset.exchange(false, std::memory_order_relaxed))
    {
      return;
    }
    for (Dead &d : g_dead)
    {
      d = Dead();
    }
    g_dead_next = 0;
    for (AliveSeen &a : g_alive)
    {
      a = AliveSeen();
    }
    for (Real &r : g_real)
    {
      r = Real();
    }
    g_real_next = 0;
    for (Rag &r : g_rag)
    {
      r = Rag();
    }
    for (OwnerSeen &s : g_owner_seen)
    {
      s = OwnerSeen();
    }
  }
  bool Readable(const uint8_t *base, uint32_t address, uint32_t size)
  {
    // Sat 21 Nov (pdz_fix_readable_cache): the cached check (guest_readable_Sat21Nov.cpp); below = off.
    if (pdz::guestmem::CacheOn())
    {
      return pdz::guestmem::Readable(const_cast<uint8_t *>(base), address, size);
    }
    if (address < 0x10000u || uint64_t(address) + size > 0xFFFF0000ull)
    {
      return false;
    }
    for (uint32_t at = address; at < address + size; at = (at & ~0xFFFu) + 0x1000u)
    {
      size_t length = 1;
      rex::memory::PageAccess access = rex::memory::PageAccess::kNoAccess;
      if (!rex::memory::QueryProtect(const_cast<uint8_t *>(base) + at, length, access) ||
          (uint32_t(access) & uint32_t(rex::memory::PageAccess::kReadOnly)) == 0)
      {
        return false;
      }
    }
    return true;
  }
  uint32_t OwnerOf(const uint8_t *base, uint32_t controller)
  {
    if (!pdz::guestmem::Ok(base, controller, kControllerOwner + 4))
    {
      return 0; // (Sun 22 Nov night (audit): the controller itself must be mapped)
    }
    const uint32_t o = Load32(base, controller + kControllerOwner);
    if (!Plausible(o))
    {
      return 0;
    }
    OwnerSeen &s = g_owner_seen[(controller >> 4) & 63u];
    if (s.controller != controller || s.owner != o)
    {
      s.controller = controller;
      s.owner = o;
      s.ok = Readable(base, o, kHealth + 4);
    }
    // (Sun 22 Nov night (audit): the cached answer is tested again: the owner can be freed while the pair stays.)
    if (!s.ok || !pdz::guestmem::Ok(base, o, kHealth + 4) || Load32(base, o + kType) != kNpcType || LoadF(base, o + kHealth) > 0.0f)
    {
      return 0;
    }
    return o;
  }

  // A ragdoll landed at `at` (metres) after falling at `speed` m/s.
  void Landed(PPCContext &ctx, uint8_t *base, uint32_t controller, const float at[3], float speed)
  {
    const uint32_t frame = Load32(base, kFrameCounter);
    Dead *best = nullptr;
    float best_d = kNearCm;
    const bool by_owner = REXCVAR_GET(pdz_snd_body_fall_owner);
    // His own controller (character +864: controller 40E39FE0 for character 40E39C80 in the check run). Round 2 (fix2):
    // taken directly, a dead human figure, whether or not the NPC update ever noted him (it does not run for a
    // dying character: logs 017 / 018, ten landings and an empty list). Fri 20 Nov night: by its owner pointer.
    const uint32_t owner = by_owner ? OwnerOf(base, controller) : 0;
    if (owner || controller > kControllerInCharacter)
    {
      const uint32_t own_c = owner ? owner : controller - kControllerInCharacter;
      // (Sun 22 Nov night (audit): own_c may be 0x360 bytes before a controller that is not inside a character.)
      if (Plausible(own_c) && pdz::guestmem::Ok(base, own_c, kHealth + 4) && Load32(base, own_c + kType) == kNpcType &&
          !(LoadF(base, own_c + kHealth) > 0.0f))
      {
        Dead *own = FindDead(own_c);
        if (!own)
        {
          own = &g_dead[g_dead_next];
          g_dead_next = (g_dead_next + 1) % int(sizeof(g_dead) / sizeof(g_dead[0]));
          *own = Dead();
          own->c = own_c;
        }
        else if (own->played && frame - own->seen > 600)
        {
          own->played = false; // (the same address a long time later: another death)
        }
        own->seen = frame;
        if (own->played)
        {
          return; // (his body fall has been played for this death)
        }
        best = own;
        const float dx = LoadF(base, own_c + kPosition) - at[0] * 100.0f, dy = LoadF(base, own_c + kPosition + 4) - at[1] * 100.0f,
                    dz = LoadF(base, own_c + kPosition + 8) - at[2] * 100.0f;
        best_d = std::sqrt(dx * dx + dy * dy + dz * dz);
      }
    }
    for (Dead &d : g_dead)
    {
      if (best || by_owner)
      {
        break; // (Fri 20 Nov night, pdz_snd_body_fall_owner: never the nearest figure)
      }
      if (!d.c || d.played || frame - d.seen > 30 || !Plausible(d.c) || !pdz::guestmem::Ok(base, d.c, kPosition + 12))
      {
        continue;
      }
      const float dx = LoadF(base, d.c + kPosition) - at[0] * 100.0f, dy = LoadF(base, d.c + kPosition + 4) - at[1] * 100.0f,
                  dz = LoadF(base, d.c + kPosition + 8) - at[2] * 100.0f;
      const float dist = std::sqrt(dx * dx + dy * dy + dz * dz);
      if (dist < best_d)
      {
        best_d = dist;
        best = &d;
      }
    }
    const bool log = REXCVAR_GET(pdz_sound_log);
    if (!best)
    {
      ++g_unmatched;
      if (log || (g_unmatched & (g_unmatched - 1)) == 0 || (by_owner && g_unmatched <= 24))
      {
        const uint32_t o = Load32(base, controller + kControllerOwner);
        const bool o_ok = Plausible(o) && Readable(base, o, kHealth + 4);
        REXLOG_INFO("pdz_snd_body_fall: a ragdoll (controller {:08X}) landed at ({:.0f} {:.0f} {:.0f}) cm after falling at "
                    "{:.1f} m/s, but {}: nothing played ({} so far)",
                    controller, at[0] * 100.0f, at[1] * 100.0f, at[2] * 100.0f, speed,
                    by_owner ? fmt::format("its owner (+36 = {:08X}{}) is not a dead NPC without a body fall", o,
                                           o_ok ? fmt::format(", class {}, health {:.1f}", Load32(base, o + kType), LoadF(base, o + kHealth))
                                                : std::string(", not a readable object"))
                             : fmt::format("no dead figure without a body fall is within {:.0f} cm of it", kNearCm),
                    g_unmatched);
      }
      return;
    }
    const float at_cm[3] = {at[0] * 100.0f, at[1] * 100.0f, at[2] * 100.0f};
    PlayFall(ctx, base, best,
             owner ? fmt::format("his own ragdoll (controller {:08X}, owner +36) landed after falling at {:.1f} m/s", controller, speed)
                   : fmt::format("his ragdoll landed {:.0f} cm from that place after falling at {:.1f} m/s", best_d, speed),
             by_owner ? at_cm : nullptr);
  }

  // His body fall, once per death (whatever found it: a fast landing, the floor reached, or the body settled).
  // at_cm (Fri 20 Nov night): where his body is (his ragdoll's), or nullptr: his place (+48).
  void PlayFall(PPCContext &ctx, uint8_t *base, Dead *best, const std::string &how, const float *at_cm)
  {
    const uint32_t frame = Load32(base, kFrameCounter);
    const bool log = REXCVAR_GET(pdz_sound_log);
    best->played = true; // once per death, whatever follows
    best->dying = false;
    if (!best->death_frame)
    {
      best->death_frame = frame;
    }
    if (best->fall_frame && frame - best->fall_frame < kOnceFrames && REXCVAR_GET(pdz_snd_body_fall_once))
    {
      // (Fri 20 Nov night: the game's own body fall has already played for this death: one per death.)
      REXLOG_INFO("pdz_snd_body_fall: character {:08X}: {}: the game's own body fall already played {} frames ago: nothing of "
                  "ours (one per death)",
                  best->c, how, frame - best->fall_frame);
      return;
    }
    for (const Real &r : g_real)
    {
      if (r.c == best->c && frame - r.frame < kRealHoldFrames)
      {
        ++g_skipped_real;
        if (log)
        {
          REXLOG_INFO("pdz_snd_body_fall: character {:08X}: his own animation played a body fall {} frames ago: nothing "
                      "of ours",
                      best->c, frame - r.frame);
        }
        return;
      }
    }
    // Sun 22 Nov night (audit): best->c is a remembered character: read (and handed to the game) only while his memory
    // is mapped; and a dead rider whose movement controller is refused below is not asked the water test either (it
    // follows the same torn-down parts).
    if (!pdz::guestmem::Ok(base, best->c, 232))
    {
      static int said = 0;
      if (said < 4)
      {
        ++said;
        REXLOG_INFO("pdz_snd_body_fall: character {:08X}: his memory is no longer readable: nothing played", best->c);
      }
      return;
    }
    bool water = false;
    {
      const uint32_t g = Load32(base, best->c + 228);
      const bool g_ok = Plausible(g) && pdz::guestmem::Ok(base, g, 4);
      const bool refused = Plausible(g) && (!g_ok || !MoveController(Load32(base, g))) &&
                           REXCVAR_GET(pdz_fix_body_fall_dead_controller);
      if (!refused)
      {
        rex::CallFrame f(ctx);
        f.ctx.r3.u64 = best->c;
        sub_82506C18(f.ctx, base);
        water = (f.ctx.r3.u32 & 0xFFu) != 0;
      }
    }
    // Thu 19 Nov (log 022: 16 body falls asked for, three of them melee kills 1-1.6 m from Jo, none ever heard; the
    // log only said "asked"): the floor's cue is found as Rare's sub_821FFD48 finds it (the floor surface of his
    // ground contact, [c+228] vfunc +116 -> surface record sub_824F4E68 -> its body-fall cue name at +136) and
    // started by us at his place (c+48) with pdz_snd_body_fall_volume and a range of pdz_snd_body_fall_range_m
    // (Rare's _BODYFALL_ rule gives 4 m), so the result, the loudness, the range and Jo's distance are logged.
    std::string cue;
    pdz::snd::Result result = pdz::snd::Result::kNoCue;
    float fall_at[3] = {LoadF(base, best->c + kPosition), LoadF(base, best->c + kPosition + 4), LoadF(base, best->c + kPosition + 8)};
    if (at_cm && std::isfinite(at_cm[0]) && std::isfinite(at_cm[1]) && std::isfinite(at_cm[2]))
    {
      fall_at[0] = at_cm[0], fall_at[1] = at_cm[1], fall_at[2] = at_cm[2]; // (at his body: Fri 20 Nov night)
    }
    best->fall_frame = frame;
    const float body_cm = pdz::snd::NearestPlayerCm(base, fall_at);
    const float lis_cm = pdz::snd::ListenerCm(base, fall_at);
    // (Fri 20 Nov night: the distance that counts is the listener's: the game's sound manager refuses and fades from it.)
    const float jo_cm = lis_cm >= 0.0f ? lis_cm : body_cm;
    const float volume = float(REXCVAR_GET(pdz_snd_body_fall_volume));
    const float range_m = float(REXCVAR_GET(pdz_snd_body_fall_range_m));
    if (water)
    {
      rex::CallFrame f(ctx);
      f.ctx.r3.u64 = 0;
      f.ctx.r4.u64 = best->c;
      __imp__sub_821EDA18(f.ctx, base); // (the water branch: the splash and STOCK_BODYFALL_WATER; no AI noise in it)
      cue = "the water branch (Rare's splash and STOCK_BODYFALL_WATER, Rare's loudness and range)";
    }
    else
    {
      uint32_t surface = 0;
      const uint32_t ground = Load32(base, best->c + 228);
      // Sun 22 Nov (Counter-Op round 17, the two closes to the desktop): a guard killed on his jetpack (shot, or the
      // jetpack destroyed) keeps a movement controller (+228) that the game has already torn down: its slot +116 is
      // the pure-virtual handler, and calling it ends the game (the kernel bug check). Only the game's own movement
      // controller classes (vtables 820CB320, 820CB478, 820CB4F8: the three that
      // share its real slot 825DD500) are asked; anything else gives surface 0 (Rare's default record), logged.
      // (The controller may be freed memory: it is read only where readable, and its vtable's slot only when that
      // vtable lies in the game image: a freed one held 3F800000, a float, and reading +116 of it faulted.)
      const bool ground_ok = Plausible(ground) && Readable(base, ground, 4);
      const uint32_t ground_vt = ground_ok ? Load32(base, ground) : 0;
      const bool vt_image = ground_vt >= 0x82000000u && ground_vt < 0x83000000u && (ground_vt & 3u) == 0;
      if (Plausible(ground) && (!ground_ok || !MoveController(ground_vt)) && REXCVAR_GET(pdz_fix_body_fall_dead_controller))
      {
        ++g_ground_refused;
        REXLOG_INFO("pdz_snd_body_fall: character {:08X}: his movement controller {:08X} is not the game's own ({}): its surface "
                    "is not asked, the default floor is used (pdz_fix_body_fall_dead_controller; {} so far)",
                    best->c, ground,
                    !ground_ok ? std::string("its memory is not readable: freed")
                    : vt_image ? fmt::format("vtable {:08X}, slot +116 {:08X}: torn down with his jetpack or seat", ground_vt,
                                             Load32(base, ground_vt + 116))
                               : fmt::format("vtable word {:08X} is not in the game image: freed memory", ground_vt),
                    g_ground_refused);
      }
      else if (Plausible(ground) && ground_ok && pdz::guestmem::Ok(base, ground_vt, 120)) // (Sun 22 Nov night (audit))
      {
        if (PPCFunc *fn = pdz::GuestFunction(Load32(base, Load32(base, ground) + 116), "the ground contact's surface slot (+116)"))
        {
          rex::CallFrame f(ctx);
          f.ctx.r3.u64 = ground;
          fn(f.ctx, base);
          surface = f.ctx.r3.u32;
        }
      }
      uint32_t record = 0;
      {
        rex::CallFrame f(ctx);
        f.ctx.r3.u64 = surface;
        sub_824F4E68(f.ctx, base);
        record = Plausible(f.ctx.r3.u32) ? f.ctx.r3.u32 : 0;
      }
      const uint32_t name = record ? Load32(base, record + 136) : 0;
      if (name >= 0x10000u && name < 0xF0000000u)
      {
        const char *t = reinterpret_cast<const char *>(base + name);
        cue.assign(t, strnlen(t, 96));
      }
      if (!cue.empty())
      {
        pdz::snd::FallRangeM(range_m);
        result = pdz::snd::Once(ctx, base, cue.c_str(), fall_at, volume);
        pdz::snd::FallRangeM(0.0f);
      }
      else
      {
        cue = fmt::format("(surface {} has no body-fall cue)", surface);
      }
    }
    ++g_played;
    best->ours = true;
    // (Thu 19 Nov night: log 025 21:19:10, BODYFALL_METAL "refused" with Jo 12.2 m away and a range of 8 m: Rare's
    // sound manager drops any sound whose listener is past its range, as it does its own; at that distance the
    // straight-line fade gives nothing anyway. Said so plainly now, with the loudness Jo would have had.)
    const float gain = jo_cm >= 0.0f ? std::max(0.0f, 1.0f - jo_cm / (range_m * 100.0f)) * volume : -1.0f;
    const std::string what = water ? std::string()
                             : (result == pdz::snd::Result::kRefused && jo_cm > range_m * 100.0f)
                                 ? fmt::format("not made: Jo is past its range ({:.1f} m of {:.1f} m; the game's sound manager drops "
                                               "such a sound, as its own)", jo_cm / 100.0f, range_m)
                                 : std::string(pdz::snd::Text(result));
    const uint32_t since = best->death_frame ? frame - best->death_frame : 0;
    REXLOG_INFO("pdz_snd_body_fall: character {:08X} at ({:.0f} {:.0f} {:.0f}): {}: {} {} (loudness {:.2f}, range {:.1f} m; the listener "
                "{:.1f} m away (the nearest player's body {:.1f} m): about {:.2f} loud at the listener; {} frames after the death "
                "was seen; sound only, no noise for the guards; {} so far, {} left to the animation's own, {} ragdolls with no figure)",
                best->c, fall_at[0], fall_at[1], fall_at[2], how, cue, what, volume, range_m, jo_cm / 100.0f, body_cm / 100.0f, gain,
                since, g_played, g_skipped_real, g_unmatched);
  }

  // Thu 19 Nov night: once a frame, every death being followed: the body fall when the body is on the floor and
  // still, or has settled, whatever the speed (the fast landing above may come first; once per death either way).
  void FollowDeaths(PPCContext &ctx, uint8_t *base, float dt)
  {
    const uint32_t frame = Load32(base, kFrameCounter);
    for (Dead &d : g_dead)
    {
      if (!d.dying || d.played || !Plausible(d.c))
      {
        continue;
      }
      // (Sun 22 Nov night (audit): a remembered character is read only while mapped; else he is no longer followed.)
      if (!pdz::guestmem::Ok(base, d.c, kControllerInCharacter + kControllerActive + 1) ||
          Load32(base, d.c + kType) != kNpcType || LoadF(base, d.c + kHealth) > 0.0f)
      {
        d.dying = false; // (gone, or alive again: a restart)
        continue;
      }
      d.t += dt;
      d.seen = frame;
      // His own ragdoll's first body, if it has one (character +864, or, Fri 20 Nov night, the controller whose owner he
      // is), whatever its mode or weight.
      float pos[3];
      const uint32_t controller = d.controller ? d.controller : d.c + kControllerInCharacter;
      // (Sun 22 Nov night (audit): a remembered controller only while mapped and still his: owner +36 = him.)
      const bool controller_ok = pdz::guestmem::Ok(base, controller, kControllerActive + 1) &&
                                 (!d.controller || Load32(base, controller + kControllerOwner) == d.c);
      const bool have = controller_ok && base[controller + kControllerActive] != 0 && BodyPosition(base, controller, pos);
      float vy = 0.0f;
      if (have)
      {
        if (d.have_y && dt > 0.0f)
        {
          vy = (pos[1] - d.last_y) / dt;
        }
        if (!d.have_y)
        {
          d.start_y = pos[1];
        }
        d.have_y = true;
        d.last_y = pos[1];
      }
      const bool still = std::fabs(vy) < 0.3f;
      d.still_t = still ? d.still_t + dt : 0.0f;
      const float drop_cm = d.have_y ? (d.start_y - d.last_y) * 100.0f : 0.0f;
      // (Smoke run of Thu 19 Nov night: the ragdoll's first body sits about 20 cm above the feet even while he stands,
      // so its height cannot tell "on the floor"; and a death by Rare's Kill played Rare's own animation body fall 1.7
      // to 3.9 s after the death (four events). So ours waits 2.5 s for Rare's own, then plays once the body has been
      // still for 0.5 s: the end of the slump, whatever its speed.)
      constexpr float wait = 2.5f;
      std::string how;
      // (Fri 20 Nov night, pdz_snd_body_fall_first_floor: log 032 22:07:55, ours 3.9 s after the death. While his body
      // can be followed, the body fall is its FIRST floor contact: down at least 25 cm since the death, having fallen
      // at 0.5 m/s or more, and stopped falling (under a quarter of its fastest, or 0.25 m/s) for two frames. No late
      // "settled" or 6 s fall then: a body that never comes down that far made no fall.)
      const bool first_floor = REXCVAR_GET(pdz_snd_body_fall_first_floor);
      if (first_floor && d.have_y)
      {
        if (!have)
        {
          if (d.t > 10.0f)
          {
            d.dying = false; // (Sun 22 Nov night (audit): his body unreadable this long: no longer followed)
          }
          continue; // (his body is not readable this frame: nothing late either way)
        }
        const float down = -vy; // m/s, positive while falling
        if (down > d.peak_down && down < 60.0f)
        {
          d.peak_down = down;
        }
        const bool stopped = d.peak_down >= 0.5f && down < std::max(0.25f, 0.25f * d.peak_down);
        d.stopped_frames = stopped && drop_cm >= 25.0f ? d.stopped_frames + 1 : 0;
        if (d.stopped_frames >= 2)
        {
          how = fmt::format("his body first hit the floor ({:.2f} s after the death, {:.0f} cm down, falling at {:.1f} m/s before)", d.t,
                            drop_cm, d.peak_down);
          const float body_cm[3] = {pos[0] * 100.0f, pos[1] * 100.0f, pos[2] * 100.0f};
          PlayFall(ctx, base, &d, how, body_cm);
        }
        continue;
      }
      if (d.t >= wait && d.still_t >= 0.5f)
      {
        how = fmt::format("his body settled ({:.1f} s after the death{})", d.t,
                          d.have_y ? fmt::format(", {:.0f} cm down", drop_cm) : std::string(", no ragdoll body to follow"));
      }
      else if (d.t >= 6.0f)
      {
        how = fmt::format("6 s after the death (the body never settled)");
      }
      if (!how.empty())
      {
        PlayFall(ctx, base, &d, how);
      }
    }
  }
} // namespace

namespace
{
  // --- b. the sparks -------------------------------------------------------------------------------------------------
  constexpr uint32_t kSoundRefSize = 260, kNameMax = 96;
  uint32_t g_spark_name = 0, g_spark_where = 0; // guest scratch (the game thread only)
  std::chrono::steady_clock::time_point g_spark_last{};
  uint64_t g_sparks = 0, g_sparks_missing = 0;

  void StoreF(uint8_t *base, uint32_t a, float v)
  {
    const uint32_t bits = std::bit_cast<uint32_t>(v);
    uint8_t *p = base + a;
    p[0] = uint8_t(bits >> 24), p[1] = uint8_t(bits >> 16), p[2] = uint8_t(bits >> 8), p[3] = uint8_t(bits);
  }
} // namespace

namespace pdz::restored_sounds
{
  // (Round 2: the light hook no longer calls LightSparks; the effect hook below does, for every effect 13.)
  bool LightSparksOn() { return false; }

  // Rare has just made its sparks at `at`: their sound, there. `light`: from the light code (the shot or the blast).
  void SparkSound(PPCContext &ctx, uint8_t *base, const float at[3], bool light, uint32_t caller)
  {
    if (!REXCVAR_GET(pdz_restored_sounds) || !(light ? REXCVAR_GET(pdz_snd_light_sparks) : REXCVAR_GET(pdz_snd_effect_sparks)) ||
        !(at[0] == at[0] && at[1] == at[1] && at[2] == at[2]))
    {
      return;
    }
    const auto now = std::chrono::steady_clock::now();
    if (now - g_spark_last < std::chrono::milliseconds(250))
    {
      return; // (one shot can report the same lamp twice)
    }
    g_spark_last = now;
    const std::string cue = REXCVAR_GET(pdz_snd_light_sparks_cue);
    if (cue.empty() || cue.size() >= kNameMax)
    {
      return;
    }
    if (!g_spark_name)
    {
      g_spark_name = REX_KERNEL_MEMORY()->SystemHeapAlloc(kNameMax);
      g_spark_where = REX_KERNEL_MEMORY()->SystemHeapAlloc(64);
    }
    if (!g_spark_name || !g_spark_where)
    {
      return;
    }
    std::memset(base + g_spark_name, 0, kNameMax);
    std::memcpy(base + g_spark_name, cue.data(), cue.size());
    bool there = false, started = false;
    {
      rex::CallFrame frame(ctx);
      frame.ctx.r3.u64 = g_spark_name;
      sub_82202C78(frame.ctx, base);
      there = int32_t(frame.ctx.r3.u32) != -1;
    }
    if (there)
    {
      std::memset(base + g_spark_where, 0, 64);
      for (int k = 0; k < 3; ++k)
      {
        StoreF(base, g_spark_where + 4 * k, at[k]);
      }
      uint32_t sound = 0;
      {
        rex::CallFrame frame(ctx);
        const std::string zeros(kSoundRefSize, 0);
        const uint32_t ref = rex::ppc::stack_push(frame.ctx, base, zeros.data(), kSoundRefSize);
        frame.ctx.r3.u64 = ref;
        frame.ctx.r4.u64 = g_spark_name;
        sub_82202D20(frame.ctx, base);
        frame.ctx.r3.u64 = ref;
        frame.ctx.r4.u64 = g_spark_where;
        frame.ctx.r5.u64 = 0;
        frame.ctx.r6.u64 = 0;
        sub_821FF428(frame.ctx, base);
        sound = frame.ctx.r3.u32;
      }
      if (Plausible(sound))
      {
        rex::CallFrame frame(ctx);
        frame.ctx.r3.u64 = sound;
        frame.ctx.f1.f64 = 0.0;
        sub_822061C8(frame.ctx, base);
        pdz::snd::FreeWhenEnded(base, sound); // (Wed 18 Nov: it kept its sound object for ever)
        started = true;
      }
    }
    const uint64_t n = started ? ++g_sparks : ++g_sparks_missing;
    if (REXCVAR_GET(pdz_sound_log) || (n & (n - 1)) == 0 || !started)
    {
      // (Round 2: how far Jo was. Rare's sound manager returns no sound for one beyond its cue's range, as for any
      // of its own sounds: that is not a failure of ours.)
      const uint32_t jo = Load32(base, 0x82E3C3E4);
      float jd = -1.0f;
      if (Plausible(jo))
      {
        const float dx = LoadF(base, jo + kPosition) - at[0], dy = LoadF(base, jo + kPosition + 4) - at[1],
                    dz = LoadF(base, jo + kPosition + 8) - at[2];
        jd = std::sqrt(dx * dx + dy * dy + dz * dz);
      }
      REXLOG_INFO("{}: the game made its electrical sparks at ({:.0f} {:.0f} {:.0f}) (caller {:08X}{}), Jo {:.0f} cm away: {} "
                  "({} started so far, {} not)",
                  light ? "pdz_snd_light_sparks" : "pdz_snd_effect_sparks", at[0], at[1], at[2], caller,
                  light ? ", the light code" : "", jd,
                  started ? cue + " started there (sound only)"
                  : there ? cue + " could not be started (the sound manager gave no sound: out of the cue's range, or no sound object free)"
                          : cue + " is not in the loaded banks: nothing played",
                  g_sparks, g_sparks_missing);
    }
  }

  void LightSparks(PPCContext &ctx, uint8_t *base, const float at[3])
  {
    SparkSound(ctx, base, at, true, 0);
  }

  void LevelReset() { g_level_reset.store(true, std::memory_order_relaxed); }

  // Once per NPC sight update: a dead human figure is noted (a few loads for a live one).
  void CharacterTick(const uint8_t *base, uint32_t c)
  {
    ApplyLevelReset(); // (Sun 22 Nov night (audit))
    if (!Plausible(c) || Load32(base, c + kType) != kNpcType || !On())
    {
      return;
    }
    const uint32_t frame = Load32(base, kFrameCounter);
    if (LoadF(base, c + kHealth) > 0.0f)
    {
      MarkAlive(c, frame); // (Thu 19 Nov night: so that his death can be seen to happen)
      return;
    }
    NoteDeath(base, c, frame);
    if (Dead *d = FindDead(c))
    {
      if (frame - d->seen > 600)
      {
        d->played = false; // (the same address after a long gap: another character, or a restart)
      }
      d->seen = frame;
      return;
    }
    Dead &d = g_dead[g_dead_next];
    g_dead_next = (g_dead_next + 1) % int(sizeof(g_dead) / sizeof(g_dead[0]));
    d = Dead();
    d.c = c;
    d.seen = frame;
  }

  // Once a frame, after Rare's ragdoll update.
  void RagdollTick(PPCContext &ctx, uint8_t *base, double frame_dt)
  {
    ApplyLevelReset(); // (Sun 22 Nov night (audit))
    if (!On() || !(frame_dt > 0.0005) || frame_dt > 0.2)
    {
      return;
    }
    const float need = float(REXCVAR_GET(pdz_snd_body_fall_speed));
    const uint32_t frame = Load32(base, kFrameCounter);
    int n = 0;
    // (Sun 22 Nov night (audit): each controller of the list is read only where it is mapped.)
    for (uint32_t c = Load32(base, kControllerList); Plausible(c) && pdz::guestmem::Ok(base, c, 88) && n < 64;
         c = Load32(base, c + kControllerNext), ++n)
    {
      // Thu 19 Nov night: a character's own controller (character +864) going active on a fresh death starts
      // following it (the NPC update does not run for a dying character).
      if (REXCVAR_GET(pdz_snd_body_fall_owner) && base[c + kControllerActive] != 0)
      {
        // (Fri 20 Nov night: by the controller's owner pointer, whether or not it sits inside him.)
        if (const uint32_t owner = OwnerOf(base, c))
        {
          NoteDeath(base, owner, frame);
          if (Dead *d = FindDead(owner); d && d->dying)
          {
            d->controller = c;
          }
        }
      }
      else if (c > kControllerInCharacter && base[c + kControllerActive] != 0)
      {
        const uint32_t own_c = c - kControllerInCharacter;
        if (Plausible(own_c) && pdz::guestmem::Ok(base, own_c, kHealth + 4) && Load32(base, own_c + kType) == kNpcType &&
            !(LoadF(base, own_c + kHealth) > 0.0f))
        {
          NoteDeath(base, own_c, frame);
        }
      }
      Rag &r = g_rag[n];
      // (A real ragdoll only: in a ragdoll mode and fully physical, or blended at least half way to it. While the
      // body still follows its death animation, the animation's own body-fall event counts: seen in the check run of
      // Tue 17 Nov, a guard killed by Rare's Kill routine stayed at weight 0 and played Rare's own body fall 2 s on.)
      const bool active = base[c + kControllerActive] != 0 && Load32(base, c + kControllerMode) != 0 &&
                          (base[c + kControllerPhysical] != 0 || LoadF(base, c + kControllerWeight) >= kRagdollWeight);
      if (r.controller != c || active != r.active)
      {
        r = Rag();
        r.controller = c;
        r.active = active;
      }
      if (!active || r.done)
      {
        continue;
      }
      float pos[3];
      if (!BodyPosition(base, c, pos))
      {
        r.have = false;
        continue;
      }
      if (r.have)
      {
        const float down = (r.pos[1] - pos[1]) / float(frame_dt); // m/s, positive while falling
        if (down > r.fastest && down < 60.0f)
        {
          r.fastest = down;
        }
        else if (r.fastest >= need && down < 0.25f * r.fastest)
        {
          r.done = true;
          Landed(ctx, base, c, pos, r.fastest);
        }
      }
      r.have = true;
      std::memcpy(r.pos, pos, sizeof(pos));
    }
    FollowDeaths(ctx, base, float(frame_dt));
  }
} // namespace pdz::restored_sounds

// Rare's animation body-fall event: noted, so that a figure whose animation has one is never given a second.
REX_HOOK_RAW(sub_821EDA18)
{
  const uint32_t c = ctx.r4.u32;
  ApplyLevelReset(); // (Sun 22 Nov night (audit))
  // (Fri 20 Nov night, pdz_snd_body_fall_once: a dead NPC whose death has had its body fall in the last 720 frames,
  // ours or the game's: this event's sound is made silent, its noise for the guards stays.)
  bool mute = false;
  if (Plausible(c) && On() && REXCVAR_GET(pdz_snd_body_fall_once) && Load32(base, c + kType) == kNpcType &&
      !(LoadF(base, c + kHealth) > 0.0f))
  {
    const uint32_t frame = Load32(base, kFrameCounter);
    Dead *d = FindDead(c);
    if (!d)
    {
      d = &g_dead[g_dead_next];
      g_dead_next = (g_dead_next + 1) % int(sizeof(g_dead) / sizeof(g_dead[0]));
      *d = Dead();
      d->c = c;
      d->seen = frame;
      d->death_frame = frame;
      d->played = true; // (the game's own body fall for this death: nothing of ours)
    }
    if (d->fall_frame && frame - d->fall_frame < kOnceFrames)
    {
      mute = true;
      static uint64_t muted = 0;
      ++muted;
      if (muted <= 16 || (muted & (muted - 1)) == 0)
      {
        const float at[3] = {LoadF(base, c + kPosition), LoadF(base, c + kPosition + 4), LoadF(base, c + kPosition + 8)};
        REXLOG_INFO("pdz_snd_body_fall: character {:08X}: another of the game's own body-fall events {} frames after this death's "
                    "first body fall: silent (one per death; the guards' noise kept; the listener {:.1f} m away; {} so far)",
                    c, frame - d->fall_frame, pdz::snd::ListenerCm(base, at) / 100.0f, muted);
      }
    }
    else
    {
      d->fall_frame = frame; // (this death's first body fall: the game's own)
    }
  }
  if (mute)
  {
    pdz::snd::FallRangeM(0.01f); // (the range picker gives its body-fall cue 1 cm: the game's sound manager refuses it)
    __imp__sub_821EDA18(ctx, base);
    pdz::snd::FallRangeM(0.0f);
    return;
  }
  if (Plausible(c))
  {
    g_real[g_real_next] = {c, Load32(base, kFrameCounter)};
    g_real_next = (g_real_next + 1) % int(sizeof(g_real) / sizeof(g_real[0]));
    Dead *d = FindDead(c);
    if (d && d->ours && On())
    {
      // (Thu 19 Nov night: the game's own event after ours for the same death: both are heard. Logged so that a
      // play log shows whether it ever happens.)
      REXLOG_INFO("pdz_snd_body_fall: character {:08X}: the game's own animation body fall came after ours ({} frames after the "
                  "death): both played",
                  c, Load32(base, kFrameCounter) - d->death_frame);
    }
    else if (d && d->dying)
    {
      d->played = true; // (his animation has its own: ours is not wanted for this death)
      d->dying = false;
      if (On())
      {
        REXLOG_INFO("pdz_snd_body_fall: character {:08X}: the game's own animation body fall ({} frames after the death): nothing "
                    "of ours for this death",
                    c, Load32(base, kFrameCounter) - d->death_frame);
      }
    }
  }
  __imp__sub_821EDA18(ctx, base);
}

// Round 2 (Tue 17 Nov night, fix2): Rare's effect at a point. Effect 13 = the electrical sparks: their sound, at the
// sparks (see the header, b). Also the effect census line (log only).
namespace
{
  struct EffectSeen
  {
    uint32_t effect = 0, caller = 0;
  };
  EffectSeen g_effects_seen[128];
  int g_effects_n = 0;
} // namespace

REX_HOOK_RAW(sub_82612330)
{
  const uint32_t effect = ctx.r5.u32, caller = uint32_t(ctx.lr), where = ctx.r3.u32;
  float at[3] = {0.0f, 0.0f, 0.0f};
  const bool have = Plausible(where);
  if (have)
  {
    for (int i = 0; i < 3; ++i)
    {
      at[i] = LoadF(base, where + 4 * i);
    }
  }
  __imp__sub_82612330(ctx, base);
  if (REXCVAR_GET(pdz_snd_effect_census) && g_effects_n < 128)
  {
    bool seen = false;
    for (int i = 0; i < g_effects_n && !seen; ++i)
    {
      seen = g_effects_seen[i].effect == effect && g_effects_seen[i].caller == caller;
    }
    if (!seen)
    {
      g_effects_seen[g_effects_n++] = {effect, caller};
      REXLOG_INFO("pdz_snd_effects: the game made effect {} (caller {:08X}) at ({:.0f} {:.0f} {:.0f}) for the first time in "
                  "this session",
                  effect, caller, at[0], at[1], at[2]);
    }
  }
  if (effect == 13 && have)
  {
    // The light code's own calls: the shot (825F3978) and the blast (825F3A10), both before the pick 825F3CD8.
    const bool light = caller >= 0x825F3978u && caller < 0x825F3CD8u;
    pdz::restored_sounds::SparkSound(ctx, base, at, light, caller);
  }
}
