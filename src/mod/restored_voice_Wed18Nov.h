// The one way the restored sounds start and stop a sound (Wed 18 Nov 2026, job sound3). See
// restored_voice_Wed18Nov.cpp for what Rare's sound manager does and why this exists (log 019: every sound we
// started kept its sound object for ever, and the game has 80).
#pragma once

#include <cstdint>
#include <string>

#include <rex/ppc/context.h>

namespace pdz::snd
{
  enum class Result : uint8_t
  {
    kStarted,
    kNoCue,   // the name is not in the loaded banks
    kRefused, // Rare's sound manager gave no sound: out of the cue's range, its kind's limit reached, or no object free
    kBudget,  // not asked: too few sound objects are free (ours never take the game's last ones)
  };
  const char *Text(Result r);

  // A sound that plays once and frees itself when it has ended (Rare's own fire-and-forget, sub_821FF3C8: the
  // "free when finished" flag is set). at: x, y, z in cm, or nullptr for an unpositioned sound. volume below 0:
  // the cue's own loudness (1.0, as every sound of Rare's starts).
  Result Once(PPCContext &ctx, uint8_t *base, const char *cue, const float *at, float volume = -1.0f, float delay = 0.0f);

  // A one-off sound an older file made itself (sub_821FF428 + sub_822061C8), just started: it frees itself when it
  // has ended, as Once does it.
  void FreeWhenEnded(uint8_t *base, uint32_t sound);

  // A sound we keep (a loop, or one we may have to stop): the sound, or 0 (why says why). It is ours until Stop;
  // every kept sound is stopped at a level end / restart / quit. reserve: how many sound objects must stay free
  // for the game after this one (ambience loops ask for more than a short one-off).
  uint32_t Start(PPCContext &ctx, uint8_t *base, const char *cue, const float *at, float volume, Result &why,
                 int reserve = 16);
  // Stops and frees a kept sound (0, or one the game or a level end has already freed, is fine). Always leaves 0.
  void Stop(PPCContext &ctx, uint8_t *base, uint32_t &sound);
  // The kept sound is still ours and has not ended (a one-shot cue that has played out is "not alive").
  bool Alive(const uint8_t *base, uint32_t sound);

  // From the release hook (sub_82205BD0, any thread): a kept sound the game freed itself is forgotten.
  void OnRelease(uint32_t sound);
  // A level start / restart seen by the stealth layer's clear (any thread): everything kept is stopped at the
  // next Tick.
  void LevelReset();
  // The level's audio is being left (the audio idle check, the game thread): everything kept is stopped now.
  void LevelEnd(PPCContext &ctx, uint8_t *base);
  // Once a frame (the physics update hook): the pending stop, and the "pdz_snd_voices:" log line.
  void Tick(PPCContext &ctx, uint8_t *base);

  // Sound objects in use now (of 80), and the nearest human player's distance (cm) to a place (-1: no player).
  int InUse(const uint8_t *base);
  float NearestPlayerCm(const uint8_t *base, const float at[3], float *where = nullptr);
  // The range (m) the range picker gives the next STOCK_PIGEON_COO (0 = pdz_snd_pigeon_coo_range_m).
  void CooRangeM(float m);
  // Thu 19 Nov: the range (m) the range picker gives the next STOCK_FLUOR_BUZZ (0 = pdz_snd_fluor_buzz_range_m).
  void BuzzRangeM(float m);
  // Thu 19 Nov: the range (m) the range picker gives the next body-fall cue (any "BODYFALL" name; 0 = Rare's own).
  void FallRangeM(float m);
  // Fri 20 Nov: the range of the next restored STOCK_ cue started (metres; 0 = none), e.g. a hum of hums_Fri20Nov.cpp.
  void NextRangeM(float m);

  // Fri 20 Nov night (sound fixes): the distance (cm) from a place to the nearest ACTIVE LISTENER of Rare's sound
  // manager (the camera: the six listener slots at 82D3EBD4, +160 active, +128 x y z; sub_821FEE48 measures a start
  // against these), the listener's place in `where`. With no active listener: the nearest player's body point and
  // 90 cm up (the camera's usual height above it). -1: neither.
  float ListenerCm(const uint8_t *base, const float at[3], float *where = nullptr);
  // The state word (+340) of the last Once() sound, read right after its play call (6 = stopped / refused, 8 = ended
  // or no cue: cut at once by the game's sound manager); -1 = the last Once() made no sound.
  int LastOnceState();
  // Log text: the sound manager's load for a kind (category): "kind 10: 7 playing of a limit of 16 (no limit: -1);
  // voices 31 of 80; the manager's active 30".
  std::string Load(const uint8_t *base, int kind);
  // Fri 20 Nov night: the range and loudness of a buzz / hum heard from the listener: range = the source's height
  // above (or below) the listener + extra_cm (at least min_cm); loudness such that at_ear of it reaches a listener
  // straight under / over it (the game's straight-line fade), at most max_volume.
  // NOTE (Fri 20 Nov night, the meter helper's reading): a placed sound's loudness (+332) is applied twice (the send,
  // sub_8275E420, and the 3D pass, group +96), so a volume v is heard as v * v; UnderPlan allows for it.
  void UnderPlan(float source_y, float listener_y, float extra_cm, float min_cm, float at_ear, float max_volume, float &range_cm,
                 float &volume);
  // Fri 20 Nov night: a one-off of a cue that LOOPS for ever in the bank (STOCK_PIGEON_COO): a kept sound that Tick
  // stops after `seconds` (one take). Returns as Once does; the stop is logged by the caller's tag at most every 5 s.
  Result Timed(PPCContext &ctx, uint8_t *base, const char *cue, const float *at, float volume, float seconds);
} // namespace pdz::snd
