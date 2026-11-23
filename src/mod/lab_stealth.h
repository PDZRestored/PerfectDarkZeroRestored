// Laboratory guard stealth (pdz_lab_guard_stealth): see lab_stealth.cpp.
#pragma once

#include <cstdint>
#include <string>

#include <rex/ppc/func.h>

namespace lab_stealth
{
  // Rare's character hit handler sub_82549040 (r3 victim, r5 attacker), hooked in counter_op.cpp when PDZ_COUNTEROP
  // builds it, else in lab_stealth.cpp. HitBefore (before Rare's handler): the victim's health, or -1 when the stealth
  // core does not watch this hit (switch off, not the Laboratory, the attacker is not player 1). HitAfter (after it):
  // marks a guard Jo hurt, or the squad mates of one she killed, as a confirmed contact.
  float HitBefore(const uint8_t *base, uint32_t victim, uint32_t attacker);
  void HitAfter(const uint8_t *base, uint32_t victim, uint32_t attacker, float before);
  // Rare's melee routine sub_82158590 (r3 the attacker; hooked in cut_scenarios.cpp), round 5 Mon 16 Nov: around
  // Rare's routine, so a melee hit of player 1's is counted like her other hits (its damage does not come through the
  // hit handler above with her as the attacker).
  void MeleeBefore(const uint8_t *base, uint32_t attacker);
  void MeleeAfter(const uint8_t *base, uint32_t attacker);
  // DarkAudioSound::Prime sub_82205C68 (hooked in cue_fixes.cpp): around Rare's Prime, so a tracked guard's line gets
  // the take the stealth code chose (XACT's variation pick runs inside Prime).
  void PrimeBegin(const uint8_t *base, uint32_t sound);
  void PrimeEnd(const uint8_t *base, uint32_t sound);
  // Dark patches: `lamps` lamps went out around `pos` (a shot, lab_dark_patches.cpp). The nearest calm guard whose squad
  // has nobody investigating goes to look alone (Rare's SEARCH, LIGHT_SHOT_OUT); up to two others comment.
  void LightIncident(PPCContext &ctx, uint8_t *base, const float pos[3], int lamps);
  // Stealth rollout (Sun 15 Nov): a zone level (not the Laboratory, which has lab_dark's own path) runs the layer
  // with lamp lures on: a lamp Rare's own shot path puts out (lab_light_shot_log.cpp) is a LightIncident.
  bool LampLureOn(const uint8_t *base);
  // Round 3c (Sun 15 Nov night, fix 2): only a light that really goes out counts. The shot hook reports the light
  // Rare's routine picked with its real state before and after Rare's call; the layer watches it and raises the
  // LightIncident itself once the light is off (never for a light Rare left on, never for the Mansion's candles).
  bool LampShot(PPCContext &ctx, uint8_t *base, uint32_t light, const float hit[3], uint32_t prop, uint32_t caller, uint32_t before,
                uint32_t after, int lamps);
  // Subway helper: lamp props too (a fitting's linked light at 825F3978, a lamp's own light at 825EE9D0): Subway only,
  // so the Nightclub (scene lights) and the Laboratory keep their paths exactly.
  bool LampPropLureOn(const uint8_t *base);
  // Round 5, Mon 16 Nov (fix 5): the layer runs and looks at bullet hits on props (the sub_825EE9D0 hook then reads
  // the prop's health round Rare's handler) ...
  bool PropHitsWatched(const uint8_t *base);
  // ... and one such hit: the prop, its class vtable, its name (a bounded copy), its place, its health before and
  // after Rare's handler (`gone`: no longer that prop after it). A security camera that goes down is a noise for the
  // guards (pdz_stealth_camera_noise); every prop that goes down is logged once.
  // Overnight Mon 16 / Tue 17 Nov: `model` = the prop's model name (its setup +84 -> +140, a bounded copy, "" when not
  // readable): Subway's cameras are plain Static props, so the name alone does not tell.
  void PropBulletHit(const uint8_t *base, uint32_t prop, uint32_t vtable, const char *name, const char *model, const float pos[3],
                     float before, float after, bool gone);
  // Is this prop a security camera (its setup name or its model name holds one of pdz_stealth_camera_models' words)?
  // A plain test with no layer state: any code may ask (the camera's own sounds). Game thread; no system calls.
  bool IsCameraProp(const uint8_t *base, uint32_t prop);
  // Follow-up (Sun 15 Nov): the stealth layer runs on the level being played (the master switch pdz_lab_guard_stealth
  // is on, a supported level, its switch on, not a Counter-Operative game). The Laboratory's dark patches
  // (pdz_lab_dark_patches, on by default since) only act while this is true, so the master switch off, or a
  // Counter-Operative game, is Rare's game exactly.
  bool LayerRuns();
  // Counter-Op round 14 (Tue 17 Nov): the level in play is a Counter-Operative game (as of the last sight update):
  // every part of the stealth layer is off there.
  bool CounterOpGameNow();
  // sub_82202D20's cue name: the lamp-out comment's one-shot swap to Rare's recorded but never raised
  // HEAR_NOISE_NOT_INVESTIGATE_TEAM (it has no speech event); every other name comes back unchanged.
  uint32_t CueName(uint8_t *base, uint32_t name);
  // The Laboratory script's level-message handler sub_823223F0 (hooked in lab_restorations.cpp), r8 = the 4-byte
  // message: true = drop it. Alarm tiers: an s10 reinforcement spawn ({type 7, byte 10}) during the first alarm.
  bool HoldLevelMessage(const uint8_t *base, uint32_t message);
  // Round 8: may a scripted line of the staff start now? False while story speech plays or a guard's line holds the
  // one voice (the stealth code's voice budget). True with the stealth code off or outside the Laboratory.
  bool SpeechFree(const uint8_t *base);
  // ... and such a line starts now and runs about `seconds`: the guards' lines wait for it (the one-voice rule).
  void SpeechBusy(const uint8_t *base, float seconds);
  // Round 8: this scientist is calm (the stealth code tracks him and he has not seen Jo, is not alarmed or dead).
  // True with the stealth code off or for a character it does not track.
  bool CalmStaff(const uint8_t *base, uint32_t character);
  // Round 11b: the next DarkAudioSound::Prime of this cue (its name without XACT_SOUNDBANK_) within 3 s picks one of
  // these takes (bit i = take i + 1): a line played on a speaker with sub_821FF3C8.
  void OrderTake(const uint8_t *base, const std::string &cue, uint32_t takes);
  // Round 11c: a scientist's line through Rare's speech (it waits for the gap, always plays, its real start is
  // logged): `cue` with its set (SCI1_LIGHTS_OUT_NEXT_QUESTION), the takes it may be (0 = any), its longest take
  // in seconds. False when the stealth code does not track him: the caller plays it its own way.
  bool StaffLine(const uint8_t *base, uint32_t character, const std::string &cue, uint32_t takes, float len, const char *what);
  // Round 11d: a line just played on a speaker with sub_821FF3C8 (`cue` without XACT_SOUNDBANK_) is followed to
  // its real start: DarkAudioSound::Play logs "REALLY STARTED" or "FAILED"; WatchTick (called every audio tick) logs
  // "DID NOT START" after 3 s. Works with the stealth code off (after LOUD).
  void WatchLine(const std::string &cue);
  void WatchTick();
  // Round 11l: DarkAudioSound stop (sub_822055C0) was asked for this sound: logged when it is the level alarm sound
  // or one of the six sirens (pdz_lab_stealth_log).
  void SoundStopped(const uint8_t *base, uint32_t sound, uint32_t caller);
  // Round 3, Sun 15 Nov (lab_stealth_r3_L_Sun15Nov.inc): no wall between the two points (cm): Rare's AI sight ray
  // (collision mask 0x20) both ways, the layer's own wall test. Works with the stealth layer off.
  bool SightLineClear(PPCContext &ctx, uint8_t *base, const float a[3], const float b[3]);
  // Round 3c, Sun 15 Nov night (fix 1, lab_stealth_r3c_A2_Sun15Nov.inc): Rare set a level up (a first load, a mission
  // restart, a reload): the stealth layer forgets everything it remembers of the level at its next look. `what`: 1 the
  // level music setup (sub_82200738, every level), 2 the Laboratory script's setup (sub_8231B778). Any thread; a
  // flag only (no lock, no work here).
  void LevelStartSignal(int what);
} // namespace lab_stealth
