// The one way the restored sounds start and stop a sound (Wed 18 Nov 2026, job sound3). TU3 addresses.
//
// WHAT WENT WRONG (log 019: on Subway every start of ours was refused, then all sound stopped).
//   Rare's sounds are DarkAudioSound objects from a pool of 80 (sub_82206C90: 80 objects of 372 bytes; the pool at
//   82E6915C, +4 the free list, +16 the number in use; sub_82205B28 takes one, sub_82205BD0 gives it back). A sound
//   is made by sub_821FF428 (object, cue reference, position, Prime), started by sub_822061C8 and belongs to its
//   caller: sub_821FF428 CLEARS flag bit 0 of +336. The sound manager's update sub_822030A8 stops and frees a sound
//   only when bit 0 is set and it has ended (state +340 == 6 or 8). Rare's own fire-and-forget helper sub_821FF3C8
//   (the body fall, impacts, ...) is sub_821FF428 + sub_822061C8 + "flags |= 1". Our one-off sounds (sparks, pigeon
//   flaps, the audioscope zoom, the locked door, the vent grate, the camera sounds) called sub_821FF428 and
//   sub_822061C8 and never set the bit and never freed the object: each start kept one of the 80 for the rest of
//   the session, level changes included (the level's "free everything" sub_82203780 also frees bit 0 sounds only).
//   After about 80 starts sub_82205B28 returned nothing: no sound of ours, then none of the game's.
// THE RULE NOW.
//   Once(): a one-off sound is started exactly as sub_821FF3C8 does it (bit 0 set: the manager frees it when it
//   has ended).
//   Start() / Stop(): a sound we keep (a loop, or one we may have to cut) is remembered here and is always stopped
//   and freed by us: by its owner when its moment ends or its object goes, and all of them at a level end (the
//   audio idle check sub_821F9290, which the level exit waits on), at a level start / restart (the stealth
//   layer's clear) and at quit (a quit leaves the level first). A kept sound the game frees itself is forgotten
//   (the release hook).
//   Budget: ours never take the last sound objects (Once keeps 8 free for the game, Start its `reserve`).
//   Rare's own limits apply to ours as to its own: Prime (sub_82205C68) asks DarkAudioSoundManager::RequestToPlay
//   (sub_82203328), which refuses a sound past 80 active (manager +328), past its cue's range (+324, see below)
//   or past its kind's instance limit (count manager +24 + 4 * kind against the limit +124 + 4 * kind); XACT
//   applies the cue's own instance limit when the cue is made.
//   pdz_snd_voices: (log only) every pdz_snd_voice_log_s seconds: the sound objects in use, the active sounds,
//   ours kept, and our starts / refusals since the last line.
// RANGE. Rare's range picker sub_82206888(sound, &range) gives every positioned sound its hearing range in cm
//   (sound +324), by kind (+312: footsteps 1200, "_RUN" 1500; gunshots 10000; shell drops 500; ...), by a table of
//   12 names (sub_82205428) and by parts of the name ("_SUBSTATION_HUM" 500, "_PIGEON_COO" 500, "_BODYFALL_" 400,
//   "_DOOR_" 1000, "_CAMSPY_" 500, "_FLIES_BUZZING" 200, ...), else 5000. Cues nothing ever played have no rule and
//   got the 50 m default. For those, a hook of the picker gives the range of the closest comparable Rare sound:
//     STOCK_FUSEBOX_HUM_LP  5 m: Rare's "_SUBSTATION_HUM" (a small electrical hum loop)
//     STOCK_FLUOR_BUZZ      5 m: the same rule (an electrical hum loop)
//     STOCK_PIGEON_FLAPS   12 m: Rare's footsteps (a creature's own movement; the coo's own rule is 5 m)
//     STOCK_PIGEON_COO     10 m (Wed 18 Nov): Rare's own "_PIGEON_COO" 5 m lies inside the distance at which
//                          Jo scares the flock off, so a coo was never heard (pdz_snd_pigeon_coo_range_m)
//     STOCK_METAL_SWING_OPEN / _CLOSE  10 m: Rare's "_DOOR_" rule (their names lack the word)
//     STOCK_SPARK_SMALL / STOCK_SPARKING  20 m: Rare's bullet-hit kinds (5 and 13), heard at the same lamp
//   Rare's manager then refuses and fades them by that range as it does its own.
// RANGES OF RARE'S OWN CUES (Fri 20 Nov, the tester's decisions on the sound radius audit). Each is its own switch;
//   each only replaces Rare's picker answer when that answer is the figure named below (so a cue Rare's picker has a
//   rule of its own for is never touched):
//     pdz_snd_sweeping_range     STOCK_SWEEPING_UP: Rare's 50 m default -> 12 m (Rare's footstep figure, kind 3 walk)
//     pdz_snd_body_fall_rare_range  "_BODYFALL_": Rare's 4 m -> pdz_snd_body_fall_range_m (12 m since Fri 20 Nov, Rare's
//                                footstep figure; our own body falls use the same figure)
//     pdz_snd_group_ranges       ONE switch for two groups, only over Rare's 50 m default:
//                                impacts and breakage 20 m (Rare's bullet-hit figure, kinds 5 / 13: the ricochets on
//                                the same hit): bullet impacts, flesh hits, fly-bys, oil drums, boxes, bottles, crates,
//                                metal crate hits, telephones, monitors, chairs, glass, the camera's smash and fall;
//                                machine hums 5 m (Rare's "_SUBSTATION_HUM"): the chemical analyser, the gas cylinder
//                                hiss, the camera motor. Water, waterfalls, fountains, the beds and the gunshots stay
//                                Rare's. The lists: kImpactParts / kMachineNames below.
//   NextRangeM(m): the range of the next restored STOCK_ cue started (the hums of hums_Fri20Nov.cpp: their height above
//   the player's floor + a few metres, as the fluorescent buzz).
#include "restored_voice_Wed18Nov.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>

#include <fmt/format.h>

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/ppc/func.h>
#include <rex/ppc/stack.h>
#include <rex/system/kernel_state.h>

REXCVAR_DECLARE(bool, pdz_restored_sounds);
REXCVAR_DEFINE_DOUBLE(pdz_snd_voice_log_s, 30.0, "Debug",
                      "Log only: seconds between 'pdz_snd_voices:' lines (the game's sound objects in use, of 80, "
                      "and the restored sounds' own starts and kept sounds). 0 = off")
    .range(0.0, 3600.0)
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_DOUBLE(pdz_snd_fusebox_hum_range_m, 1.0, "Gameplay",
                      "Restored sound: how far the fuse box hum is heard (metres). 1 = right beside it (Thu 19 Nov; "
                      "the game's own range for its substation hum is 5)")
    .range(1.0, 50.0)
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_DOUBLE(pdz_snd_fluor_buzz_range_m, 5.0, "Gameplay",
                      "Restored sound: how far a fluorescent light's buzz is heard (metres). 5 = the game's own "
                      "range for its substation hum")
    .range(1.0, 50.0)
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
namespace
{
  std::atomic<float> g_coo_range_m{0.0f}; // (Wed 18 Nov night: the next coo's own range; 0 = the switch below)
  std::atomic<float> g_buzz_range_m{0.0f}; // (Thu 19 Nov: the next buzz's own range; 0 = pdz_snd_fluor_buzz_range_m)
  std::atomic<float> g_fall_range_m{0.0f}; // (Thu 19 Nov: our body fall's range; 0 = Rare's own)
  std::atomic<float> g_next_range_m{0.0f}; // (Fri 20 Nov: the next restored STOCK_ cue's own range; 0 = none)
}
REXCVAR_DECLARE(double, pdz_snd_body_fall_range_m);
REXCVAR_DEFINE_DOUBLE(pdz_snd_pigeon_coo_range_m, 10.0, "Gameplay",
                      "Restored sound: how far a waiting flock's coo is heard (metres). The game's own 5 m lies "
                      "inside the distance at which Jo scares the birds off")
    .range(1.0, 50.0)
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_snd_sweeping_range, true, "Gameplay",
                    "Restored sound (Fri 20 Nov): the Nightclub sweepers' broom is heard to 12 m (the game's footstep "
                    "range) instead of the game's 50 m default, which carried it through walls to the docks. Needs "
                    "pdz_restored_sounds")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_snd_body_fall_rare_range, true, "Gameplay",
                    "Restored sound (Fri 20 Nov): the game's own body falls (its death animations) are heard to "
                    "pdz_snd_body_fall_range_m (12 m) instead of the game's 4 m. Needs pdz_restored_sounds")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_snd_group_ranges, true, "Gameplay",
                    "Restored sound (Fri 20 Nov, to try by ear): sounds the game gives its 50 m default get the game's "
                    "own shorter figures by group: bullet impacts, fly-bys and props breaking or knocked (drums, boxes, "
                    "bottles, crates, phones, monitors, chairs, glass, cameras) 20 m like its ricochets; machine hums "
                    "(chemical analyser, gas hiss, camera motor) 5 m like its substation hum. Gunshots, water and the "
                    "background stay the game's. Needs pdz_restored_sounds")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_DOUBLE(pdz_snd_pigeon_flaps_range_m, 12.0, "Gameplay",
                      "Restored sound: how far pigeons' wing flaps are heard (metres). 12 = the game's own range "
                      "for footsteps")
    .range(1.0, 50.0)
    .lifecycle(rex::cvar::Lifecycle::kHotReload);

REX_EXTERN(sub_82202C78);        // the name resolver (r3 = name): the cue, or -1
REX_EXTERN(sub_82202D20);        // cue reference (r3 = ref {char name[256]; cue}, r4 = name)
REX_EXTERN(sub_821FF428);        // sound from a reference (r3 = ref, r4 = position or 0, r5, r6): the sound, or 0
REX_EXTERN(sub_822061C8);        // play (r3 = sound, f1 = delay in seconds)
REX_EXTERN(sub_822055C0);        // stop (r3 = sound)
REX_EXTERN(sub_82205BD0);        // release (r3 = sound): back to the pool
REX_EXTERN(__imp__sub_82206888); // the range picker (r3 = sound, r4 = &range)

namespace
{
  constexpr uint32_t kGame = 0x82D6BFD8;     // +32 -> the audio block, +0 -> the sound manager
  constexpr uint32_t kPool = 0x82E6915C;     // the DarkAudioSound pool: +16 the number in use
  constexpr int kPoolSize = 80;
  constexpr uint32_t kManagerActive = 328;   // the manager's active sounds
  constexpr uint32_t kSoundName = 52, kSoundKind = 312, kSoundRange = 324, kSoundVolume = 332, kSoundFlags = 336,
                     kSoundState = 340;
  constexpr uint32_t kFreeWhenEnded = 1;     // flag bit 0 (sub_821FF3C8 sets it, sub_822030A8 reads it)
  constexpr uint32_t kHumans = 0x82E3C3E4;   // Rare's human players: the first; +6792 the next
  constexpr uint32_t kHumanNext = 6792, kPosition = 48;
  constexpr int kOnceReserve = 8;
  // Fri 20 Nov night: Rare's listeners (sub_821FEE48: six slots at 82D40000 - 5164; +160 active, +128 x y z).
  constexpr uint32_t kListeners = 0x82D3EBD4;
  constexpr int kListenerSlots = 6;
  constexpr uint32_t kListenerActive = 160, kListenerPos = 128;
  // The manager's per-kind counts (+24 + 4 k) and limits (+124 + 4 k) (RequestToPlay sub_82203328).
  constexpr uint32_t kKindCount = 24, kKindLimit = 124;
  std::atomic<int> g_last_once_state{-1};

  uint32_t Load32(const uint8_t *base, uint32_t a)
  {
    const uint8_t *p = base + a;
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
  }
  void Store32(uint8_t *base, uint32_t a, uint32_t v)
  {
    uint8_t *p = base + a;
    p[0] = uint8_t(v >> 24), p[1] = uint8_t(v >> 16), p[2] = uint8_t(v >> 8), p[3] = uint8_t(v);
  }
  float LoadF(const uint8_t *base, uint32_t a) { return std::bit_cast<float>(Load32(base, a)); }
  void StoreF(uint8_t *base, uint32_t a, float f) { Store32(base, a, std::bit_cast<uint32_t>(f)); }
  bool Guest(uint32_t a) { return a >= 0x10000u && a < 0xF0000000u && (a & 3u) == 0; }

  // Guest copies of the cue names (made once, kept for the session).
  std::mutex g_names_mutex;
  std::unordered_map<std::string, uint32_t> g_names;
  uint32_t NameOf(uint8_t *base, const char *cue)
  {
    std::lock_guard<std::mutex> lock(g_names_mutex);
    uint32_t &at = g_names[cue];
    if (!at)
    {
      const uint32_t n = uint32_t(std::strlen(cue) + 1);
      at = REX_KERNEL_MEMORY()->SystemHeapAlloc(n < 96 ? 96 : n);
      if (at)
      {
        std::memset(base + at, 0, n < 96 ? 96 : n);
        std::memcpy(base + at, cue, n);
      }
    }
    return at;
  }

  // The kept sounds.
  std::array<std::atomic<uint32_t>, 48> g_kept{};
  std::atomic<bool> g_reset{false};
  // While the level's audio is being left (the exit waits for silence), and for 3 s after, nothing is kept anew.
  std::atomic<int64_t> g_leaving_until{0};
  int64_t NowMs()
  {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
  }
  // Fri 20 Nov night: the timed one-offs (Timed): the kept sound and when Tick stops it.
  std::array<std::atomic<uint32_t>, 8> g_timed{};
  std::array<std::atomic<int64_t>, 8> g_timed_until{};
  // Counters since the last log line (the game thread; a torn read in the log is harmless).
  std::atomic<uint32_t> g_once_started{0}, g_once_refused{0}, g_once_budget{0}, g_kept_started{0}, g_kept_refused{0},
      g_kept_stopped{0}, g_level_stops{0};
  int g_peak = 0;
  std::chrono::steady_clock::time_point g_last_log{};

  // The cues whose first start has been described (name -> done), for the loudness / range table.
  std::mutex g_first_mutex;
  std::unordered_map<std::string, bool> g_first;

  uint32_t Make(PPCContext &ctx, uint8_t *base, const char *cue, const float *at, pdz::snd::Result &why)
  {
    why = pdz::snd::Result::kNoCue;
    const uint32_t name = NameOf(base, cue);
    if (!name)
    {
      return 0;
    }
    {
      rex::CallFrame frame(ctx);
      frame.ctx.r3.u64 = name;
      sub_82202C78(frame.ctx, base);
      if (int32_t(frame.ctx.r3.u32) == -1)
      {
        return 0;
      }
    }
    why = pdz::snd::Result::kRefused;
    rex::CallFrame frame(ctx);
    const std::string zeros(260, 0);
    const uint32_t ref = rex::ppc::stack_push(frame.ctx, base, zeros.data(), 260);
    uint32_t where = 0;
    if (at)
    {
      uint8_t pos[16] = {};
      for (int k = 0; k < 3; ++k)
      {
        const uint32_t bits = std::bit_cast<uint32_t>(at[k]);
        pos[4 * k] = uint8_t(bits >> 24), pos[4 * k + 1] = uint8_t(bits >> 16), pos[4 * k + 2] = uint8_t(bits >> 8),
                pos[4 * k + 3] = uint8_t(bits);
      }
      where = rex::ppc::stack_push(frame.ctx, base, pos, 16);
    }
    frame.ctx.r3.u64 = ref;
    frame.ctx.r4.u64 = name;
    sub_82202D20(frame.ctx, base);
    frame.ctx.r3.u64 = ref;
    frame.ctx.r4.u64 = where;
    frame.ctx.r5.u64 = 0;
    frame.ctx.r6.u64 = 0;
    sub_821FF428(frame.ctx, base);
    const uint32_t sound = frame.ctx.r3.u32;
    if (!Guest(sound))
    {
      return 0;
    }
    why = pdz::snd::Result::kStarted;
    return sound;
  }

  void Play(PPCContext &ctx, uint8_t *base, uint32_t sound, float volume, float delay)
  {
    if (volume >= 0.0f)
    {
      StoreF(base, sound + kSoundVolume, volume);
    }
    rex::CallFrame frame(ctx);
    frame.ctx.r3.u64 = sound;
    frame.ctx.f1.f64 = double(delay);
    sub_822061C8(frame.ctx, base);
  }

  // Once per cue a session: what the sound was started with (for the loudness / range table).
  void FirstStart(const uint8_t *base, const char *cue, uint32_t sound, bool kept, const float *at)
  {
    {
      std::lock_guard<std::mutex> lock(g_first_mutex);
      if (g_first.size() > 256 || !g_first.emplace(cue, true).second)
      {
        return;
      }
    }
    const std::string_view name(cue);
    REXLOG_INFO("pdz_snd_voices: first start of {}: kind {}, loudness {:.2f}, range {} ({}; {}; Jo {:.1f} m away)",
                name.size() > 15 ? name.substr(15) : name, int32_t(Load32(base, sound + kSoundKind)),
                LoadF(base, sound + kSoundVolume),
                at ? fmt::format("{:.0f} m", LoadF(base, sound + kSoundRange) / 100.0f) : std::string("none"),
                at ? "placed in the world" : "not placed (heard everywhere)",
                kept ? "kept by us until we stop it" : "frees itself when it ends",
                at ? pdz::snd::NearestPlayerCm(base, at) / 100.0f : 0.0f);
  }

  void StopNow(PPCContext &ctx, uint8_t *base, uint32_t sound)
  {
    {
      rex::CallFrame frame(ctx);
      frame.ctx.r3.u64 = sound;
      sub_822055C0(frame.ctx, base);
    }
    rex::CallFrame frame(ctx);
    frame.ctx.r3.u64 = sound;
    sub_82205BD0(frame.ctx, base);
  }

  int StopAll(PPCContext &ctx, uint8_t *base)
  {
    int n = 0;
    for (auto &k : g_kept)
    {
      const uint32_t sound = k.exchange(0);
      if (sound)
      {
        StopNow(ctx, base, sound);
        ++n;
      }
    }
    return n;
  }

  int KeptNow()
  {
    int n = 0;
    for (const auto &k : g_kept)
    {
      n += k.load(std::memory_order_relaxed) != 0;
    }
    return n;
  }

  struct RangeRule
  {
    const char *name; // the full cue name
    int which;
  };
  constexpr RangeRule kRanges[] = {
      {"XACT_SOUNDBANK_STOCK_FUSEBOX_HUM_LP", 0},
      {"XACT_SOUNDBANK_STOCK_FLUOR_BUZZ", 1},
      {"XACT_SOUNDBANK_STOCK_PIGEON_FLAPS", 2},
      {"XACT_SOUNDBANK_STOCK_PIGEON_COO", 5}, // (replaces Rare's 5 m: Wed 18 Nov)
      // Fixed: the metal doors' swing sounds have no "_DOOR_" in their names, so they missed Rare's 10 m door rule
      // that the wooden set and METAL_DOOR_* get (3); the sparks get the 20 m of Rare's bullet-hit sounds, which
      // play at the same lamp at the same moment (4), unless Rare's picker has a rule of its own for them.
      {"XACT_SOUNDBANK_STOCK_METAL_SWING_OPEN", 3},
      {"XACT_SOUNDBANK_STOCK_METAL_SWING_CLOSE", 3},
      {"XACT_SOUNDBANK_STOCK_SPARK_SMALL", 4},
      {"XACT_SOUNDBANK_STOCK_SPARKING", 4},
  };

  // Fri 20 Nov (pdz_snd_group_ranges): the impacts-and-breakage group (20 m), by the start of the name after
  // "XACT_SOUNDBANK_", and the machine hums (5 m), by full name after it.
  constexpr const char *kImpactParts[] = {
      "GUNS_BULLETHIT_",  "GUNS_BULLEHIT_", // (Rare's own typo: GUNS_BULLEHIT_BULLETPROOF_GLASS)
      "GUNS_XACT_SOGUNS_BULLETHIT_",        // (the typo'd metal-table hit)
      "GUNS_HIT_FLESH",   "GUNS_HITFLESH",   "GUNS_BULLET_FLYBYS",
      "STOCK_OILDRUM_",   "STOCK_BOX_",      "STOCK_BOTTLE_",       "STOCK_CRATE_",   "STOCK_TELEPHONE_",
      "STOCK_MONITOR_",   "STOCK_SWIVEL_CHAIR_", "STOCK_CHAIR_",    "STOCK_SECCAMERA_",
  };
  constexpr const char *kMachineNames[] = {"STOCK_CHEMICAL_ANALYSER", "STOCK_GAS_CYLINDER_HISS", "STOCK_SEC_CAM_MOTOR_LP"};
  // 0 none, 1 impacts / breakage (20 m), 2 machine hums (5 m).
  int GroupOf(const char *name)
  {
    if (std::strncmp(name, "XACT_SOUNDBANK_", 15) != 0)
    {
      return 0;
    }
    const char *rest = name + 15;
    for (const char *m : kMachineNames)
    {
      if (std::strncmp(rest, m, 64) == 0)
      {
        return 2;
      }
    }
    for (const char *part : kImpactParts)
    {
      if (std::strncmp(rest, part, std::strlen(part)) == 0)
      {
        return 1;
      }
    }
    return 0;
  }
} // namespace


// Fri 20 Nov night (SOUND_PROOF.md): each restored cue's loudness trim, set from the heard-level meter
// (sound_heard_Fri20Nov.cpp) so that standing at the thing it is at least as loud as a guard's footstep at that
// distance (bank volume, the loudness counted twice, the recording's own level). A trim t multiplies the volume a
// restored sound is started with (heard as t squared). pdz_snd_trim off = 1.0 for all (before).
REXCVAR_DEFINE_BOOL(pdz_snd_trim, true, "Gameplay",
                    "Restored sounds (Fri 20 Nov night): each restored sound's loudness trim, measured so it is clearly "
                    "heard standing at the thing (pdz_snd_trim_*). Off = the loudness before tonight")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_DOUBLE(pdz_snd_trim_computer, 6.0, "Gameplay",
                      "pdz_snd_trim: the volume factor of the mainframe / computer hum (the bank keeps it 21 dB down) (heard as its square; 1 = before)")
    .range(0.1, 8.0)
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_DOUBLE(pdz_snd_trim_fusebox, 1.7, "Gameplay",
                      "pdz_snd_trim: the volume factor of the fuse box hum (heard as its square; 1 = before)")
    .range(0.1, 8.0)
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_DOUBLE(pdz_snd_trim_flies, 1.5, "Gameplay",
                      "pdz_snd_trim: the volume factor of the flies over bins (heard as its square; 1 = before)")
    .range(0.1, 8.0)
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_DOUBLE(pdz_snd_trim_buzz, 1.6, "Gameplay",
                      "pdz_snd_trim: the volume factor of the strip-light buzz (heard as its square; 1 = before)")
    .range(0.1, 8.0)
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_DOUBLE(pdz_snd_trim_tank, 7.0, "Gameplay",
                      "pdz_snd_trim: the volume factor of the jellyfish tank bubbles (the bank keeps them 16 dB down) (heard as its square; 1 = before)")
    .range(0.1, 8.0)
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_DOUBLE(pdz_snd_trim_vending, 2.2, "Gameplay",
                      "pdz_snd_trim: the volume factor of the drinks machine hum (heard as its square; 1 = before)")
    .range(0.1, 8.0)
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_DOUBLE(pdz_snd_trim_pipe, 3.5, "Gameplay",
                      "pdz_snd_trim: the volume factor of the Rooftops pipe flow (heard as its square; 1 = before)")
    .range(0.1, 8.0)
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_DOUBLE(pdz_snd_trim_neon, 2.4, "Gameplay",
                      "pdz_snd_trim: the volume factor of the neon sign hum (the bank keeps it 13 dB down) (heard as its square; 1 = before)")
    .range(0.1, 8.0)
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_DOUBLE(pdz_snd_trim_coo, 1.3, "Gameplay",
                      "pdz_snd_trim: the volume factor of the pigeon coo (heard as its square; 1 = before)")
    .range(0.1, 8.0)
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_DOUBLE(pdz_snd_trim_bullet_water, 1.6, "Gameplay",
                      "pdz_snd_trim: the volume factor of a bullet into water (Rare's GUNS_BULLETHIT_WATER: the same "
                      "takes as the puddle step, about 9 dB quieter than a bullet on stone, at the moment of the "
                      "gunshot) (heard as its square: 1.6 = +8 dB; 1 = the game's own)")
    .range(0.1, 8.0)
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_DOUBLE(pdz_snd_trim_bodyfall, 1.0, "Gameplay",
                      "pdz_snd_trim: the volume factor of our body falls (heard as its square; 1 = before)")
    .range(0.1, 8.0)
    .lifecycle(rex::cvar::Lifecycle::kHotReload);

namespace pdz::snd
{
  const char *Text(Result r)
  {
    switch (r)
    {
    case Result::kStarted: return "started";
    case Result::kNoCue: return "is not in the loaded banks";
    case Result::kRefused:
      return "was refused by the game's sound manager (out of the cue's range, its kind's limit reached, or no sound "
             "object free)";
    default: return "was not asked for (too few of the game's 80 sound objects are free)";
    }
  }

  int InUse(const uint8_t *base) { return int(Load32(base, kPool + 16)); }

  // Wed 18 Nov night: the next coo's own range (its spawner's scare distance and a little more), for the range
  // picker hook; 0 = pdz_snd_pigeon_coo_range_m.
  void CooRangeM(float m)
  {
    g_coo_range_m.store(m, std::memory_order_relaxed);
  }
  // Thu 19 Nov: the next buzz's own range (the fitting's height above the player's floor + 2 m); 0 = the switch.
  void BuzzRangeM(float m)
  {
    g_buzz_range_m.store(m, std::memory_order_relaxed);
  }
  void FallRangeM(float m)
  {
    g_fall_range_m.store(m, std::memory_order_relaxed);
  }
  void NextRangeM(float m)
  {
    g_next_range_m.store(m, std::memory_order_relaxed);
  }

  float NearestPlayerCm(const uint8_t *base, const float at[3], float *where)
  {
    float best = -1.0f;
    uint32_t h = Load32(base, kHumans);
    for (int n = 0; Guest(h) && n < 4; ++n, h = Load32(base, h + kHumanNext))
    {
      const float dx = LoadF(base, h + kPosition) - at[0], dy = LoadF(base, h + kPosition + 4) - at[1],
                  dz = LoadF(base, h + kPosition + 8) - at[2];
      const float d = std::sqrt(dx * dx + dy * dy + dz * dz);
      if (best < 0.0f || d < best)
      {
        best = d;
        if (where)
        {
          where[0] = LoadF(base, h + kPosition), where[1] = LoadF(base, h + kPosition + 4), where[2] = LoadF(base, h + kPosition + 8);
        }
      }
    }
    return best;
  }

  float ListenerCm(const uint8_t *base, const float at[3], float *where)
  {
    float best = -1.0f;
    for (int i = 0; i < kListenerSlots; ++i)
    {
      const uint32_t l = Load32(base, kListeners + 4 * uint32_t(i));
      if (!Guest(l) || base[l + kListenerActive] == 0)
      {
        continue;
      }
      const float p[3] = {LoadF(base, l + kListenerPos), LoadF(base, l + kListenerPos + 4), LoadF(base, l + kListenerPos + 8)};
      if (!(std::isfinite(p[0]) && std::isfinite(p[1]) && std::isfinite(p[2])))
      {
        continue;
      }
      const float dx = p[0] - at[0], dy = p[1] - at[1], dz = p[2] - at[2];
      const float d = std::sqrt(dx * dx + dy * dy + dz * dz);
      if (best < 0.0f || d < best)
      {
        best = d;
        if (where)
        {
          where[0] = p[0], where[1] = p[1], where[2] = p[2];
        }
      }
    }
    if (best >= 0.0f)
    {
      return best;
    }
    // (No active listener: the nearest player's body point, 90 cm up.)
    float body[3] = {};
    if (NearestPlayerCm(base, at, body) < 0.0f)
    {
      return -1.0f;
    }
    body[1] += 90.0f;
    if (where)
    {
      where[0] = body[0], where[1] = body[1], where[2] = body[2];
    }
    const float dx = body[0] - at[0], dy = body[1] - at[1], dz = body[2] - at[2];
    return std::sqrt(dx * dx + dy * dy + dz * dz);
  }

  int LastOnceState() { return g_last_once_state.load(std::memory_order_relaxed); }

  void UnderPlan(float source_y, float listener_y, float extra_cm, float min_cm, float at_ear, float max_volume, float &range_cm,
                 float &volume)
  {
    const float h = std::isfinite(source_y - listener_y) ? std::min(std::fabs(source_y - listener_y), 1200.0f) : 0.0f;
    range_cm = std::max(min_cm, h + extra_cm);
    const float share = std::max(0.05f, (range_cm - h) / range_cm); // (what reaches a listener straight under it)
    volume = std::min(max_volume, std::sqrt(at_ear / share));        // (heard as volume * volume: see the header)
  }

  // pdz_snd_trim: the measured loudness trim of a restored cue (by name part), applied where it starts.
  float Trimmed(const char *cue, float volume)
  {
    if (!REXCVAR_GET(pdz_snd_trim) || !cue)
    {
      return volume;
    }
    struct T
    {
      const char *part;
      double (*get)();
    };
    static const T kTrims[] = {
        {"COMPUTER_LARGE_LP", [] { return double(REXCVAR_GET(pdz_snd_trim_computer)); }},
        {"FUSEBOX_HUM_LP", [] { return double(REXCVAR_GET(pdz_snd_trim_fusebox)); }},
        {"FLIES_BUZZING", [] { return double(REXCVAR_GET(pdz_snd_trim_flies)); }},
        {"FLUOR_BUZZ", [] { return double(REXCVAR_GET(pdz_snd_trim_buzz)); }},
        {"AQUA_BUBBLE_LP", [] { return double(REXCVAR_GET(pdz_snd_trim_tank)); }},
        {"VEND_HUM", [] { return double(REXCVAR_GET(pdz_snd_trim_vending)); }},
        {"PIPE_FLOW", [] { return double(REXCVAR_GET(pdz_snd_trim_pipe)); }},
        {"NEONSIGN_HUM", [] { return double(REXCVAR_GET(pdz_snd_trim_neon)); }},
        {"PIGEON_COO", [] { return double(REXCVAR_GET(pdz_snd_trim_coo)); }},
        {"BODYFALL", [] { return double(REXCVAR_GET(pdz_snd_trim_bodyfall)); }},
        {"BULLETHIT_WATER", [] { return double(REXCVAR_GET(pdz_snd_trim_bullet_water)); }},
    };
    for (const T &t : kTrims)
    {
      if (std::strstr(cue, t.part))
      {
        return float(double(volume < 0.0f ? 1.0f : volume) * t.get());
      }
    }
    return volume;
  }

  Result Timed(PPCContext &ctx, uint8_t *base, const char *cue, const float *at, float volume, float seconds)
  {
    g_last_once_state.store(-1, std::memory_order_relaxed);
    std::atomic<uint32_t> *slot = nullptr;
    for (auto &t : g_timed)
    {
      if (!slot && t.load(std::memory_order_relaxed) == 0)
      {
        slot = &t;
      }
    }
    if (!slot)
    {
      return Result::kBudget;
    }
    Result why;
    const uint32_t sound = Start(ctx, base, cue, at, volume, why, kOnceReserve);
    if (!sound)
    {
      return why;
    }
    g_last_once_state.store(int(Load32(base, sound + kSoundState)), std::memory_order_relaxed);
    slot->store(sound, std::memory_order_relaxed);
    g_timed_until[size_t(slot - g_timed.data())].store(NowMs() + int64_t(std::max(0.05f, seconds) * 1000.0f),
                                                       std::memory_order_relaxed);
    return Result::kStarted;
  }

  std::string Load(const uint8_t *base, int kind)
  {
    const uint32_t audio = Load32(base, kGame + 32);
    const uint32_t manager = Guest(audio) ? Load32(base, audio) : 0;
    if (!Guest(manager) || kind < 0 || kind > 24)
    {
      return fmt::format("kind {}; voices {} of {}", kind, InUse(base), kPoolSize);
    }
    const int32_t count = int32_t(Load32(base, manager + kKindCount + 4 * uint32_t(kind)));
    const int32_t limit = int32_t(Load32(base, manager + kKindLimit + 4 * uint32_t(kind)));
    return fmt::format("kind {}: {} playing of a limit of {}{}; voices {} of {}; the manager's active {}", kind, count,
                       limit, limit == -1 ? " (none)" : (count >= limit ? " (FULL: the game refuses more)" : ""),
                       InUse(base), kPoolSize, int32_t(Load32(base, manager + kManagerActive)));
  }

  Result Once(PPCContext &ctx, uint8_t *base, const char *cue, const float *at, float volume, float delay)
  {
    g_last_once_state.store(-1, std::memory_order_relaxed);
    if (InUse(base) > kPoolSize - kOnceReserve)
    {
      g_once_budget.fetch_add(1, std::memory_order_relaxed);
      return Result::kBudget;
    }
    Result why;
    const uint32_t sound = Make(ctx, base, cue, at, why);
    if (!sound)
    {
      if (why == Result::kRefused)
      {
        g_once_refused.fetch_add(1, std::memory_order_relaxed);
      }
      return why;
    }
    Play(ctx, base, sound, Trimmed(cue, volume), delay);
    // (Fri 20 Nov night: its state right after the play call: 6 / 8 = cut at once, for the callers' log lines.)
    g_last_once_state.store(int(Load32(base, sound + kSoundState)), std::memory_order_relaxed);
    // Rare's fire-and-forget (sub_821FF3C8, 821FF400-821FF408): the manager frees it when it has ended.
    Store32(base, sound + kSoundFlags, Load32(base, sound + kSoundFlags) | kFreeWhenEnded);
    g_once_started.fetch_add(1, std::memory_order_relaxed);
    FirstStart(base, cue, sound, false, at);
    return Result::kStarted;
  }

  void FreeWhenEnded(uint8_t *base, uint32_t sound)
  {
    if (!Guest(sound))
    {
      return;
    }
    Store32(base, sound + kSoundFlags, Load32(base, sound + kSoundFlags) | kFreeWhenEnded);
    g_once_started.fetch_add(1, std::memory_order_relaxed);
    const char *name = reinterpret_cast<const char *>(base + sound + kSoundName);
    const std::string cue(name, strnlen(name, 96));
    float at[3] = {LoadF(base, sound + 16), LoadF(base, sound + 20), LoadF(base, sound + 24)};
    const bool placed = (Load32(base, sound + kSoundFlags) & 0x40u) != 0; // (set by sub_821FF428 with a position)
    FirstStart(base, cue.c_str(), sound, false, placed ? at : nullptr);
  }

  uint32_t Start(PPCContext &ctx, uint8_t *base, const char *cue, const float *at, float volume, Result &why, int reserve)
  {
    why = Result::kBudget;
    std::atomic<uint32_t> *slot = nullptr;
    for (auto &k : g_kept)
    {
      if (!slot && k.load(std::memory_order_relaxed) == 0)
      {
        slot = &k;
      }
    }
    if (!slot || InUse(base) > kPoolSize - reserve || NowMs() < g_leaving_until.load(std::memory_order_relaxed))
    {
      return 0;
    }
    const uint32_t sound = Make(ctx, base, cue, at, why);
    if (!sound)
    {
      if (why == Result::kRefused)
      {
        g_kept_refused.fetch_add(1, std::memory_order_relaxed);
      }
      return 0;
    }
    slot->store(sound);
    Play(ctx, base, sound, Trimmed(cue, volume), 0.0f);
    g_kept_started.fetch_add(1, std::memory_order_relaxed);
    FirstStart(base, cue, sound, true, at);
    return sound;
  }

  void Stop(PPCContext &ctx, uint8_t *base, uint32_t &sound)
  {
    const uint32_t s = sound;
    sound = 0;
    if (!s)
    {
      return;
    }
    for (auto &k : g_kept)
    {
      uint32_t expect = s;
      if (k.compare_exchange_strong(expect, 0))
      {
        StopNow(ctx, base, s); // (still ours: nobody else has freed it)
        g_kept_stopped.fetch_add(1, std::memory_order_relaxed);
        return;
      }
    }
  }

  bool Alive(const uint8_t *base, uint32_t sound)
  {
    if (!sound)
    {
      return false;
    }
    for (const auto &k : g_kept)
    {
      if (k.load(std::memory_order_relaxed) == sound)
      {
        const uint32_t state = Load32(base, sound + kSoundState);
        return state != 6 && state != 8; // (6 stopped / refused, 8 ended or no cue)
      }
    }
    return false;
  }

  void OnRelease(uint32_t sound)
  {
    if (!sound)
    {
      return;
    }
    for (auto &k : g_kept)
    {
      uint32_t expect = sound;
      k.compare_exchange_strong(expect, 0);
    }
  }

  void LevelReset() { g_reset.store(true, std::memory_order_relaxed); }

  void LevelEnd(PPCContext &ctx, uint8_t *base)
  {
    g_leaving_until.store(NowMs() + 3000, std::memory_order_relaxed);
    const int n = StopAll(ctx, base);
    if (n)
    {
      g_level_stops.fetch_add(uint32_t(n), std::memory_order_relaxed);
      REXLOG_INFO("pdz_snd_voices: the level's audio is being left: {} kept restored sound(s) stopped and freed (sound "
                  "objects in use now {} of {})",
                  n, InUse(base), kPoolSize);
    }
  }

  void Tick(PPCContext &ctx, uint8_t *base)
  {
    if (g_reset.exchange(false, std::memory_order_relaxed))
    {
      const int n = StopAll(ctx, base);
      if (n)
      {
        g_level_stops.fetch_add(uint32_t(n), std::memory_order_relaxed);
        REXLOG_INFO("pdz_snd_voices: a level start / restart: {} kept restored sound(s) of the attempt before stopped and "
                    "freed",
                    n);
      }
    }
    // (Fri 20 Nov night: the timed one-offs whose take is over: stopped and freed. One the game or a level change has
    // freed already is no longer kept: Stop leaves it alone.)
    {
      const int64_t now_ms = NowMs();
      for (size_t i = 0; i < g_timed.size(); ++i)
      {
        if (g_timed[i].load(std::memory_order_relaxed) && now_ms >= g_timed_until[i].load(std::memory_order_relaxed))
        {
          uint32_t sound = g_timed[i].exchange(0);
          Stop(ctx, base, sound);
        }
      }
    }
    const int in_use = InUse(base);
    g_peak = in_use > g_peak ? in_use : g_peak;
    const double every = REXCVAR_GET(pdz_snd_voice_log_s);
    const auto now = std::chrono::steady_clock::now();
    if (!(every > 0.0) || std::chrono::duration<double>(now - g_last_log).count() < every)
    {
      return;
    }
    g_last_log = now;
    const uint32_t audio = Load32(base, kGame + 32);
    const uint32_t manager = Guest(audio) ? Load32(base, audio) : 0;
    REXLOG_INFO("pdz_snd_voices: voices in use {} of {} (highest since the last line {}; the manager's active sounds {}) | "
                "restored sounds: {} kept now; since the last line {} one-off started, {} refused by the game, {} not "
                "asked (budget); {} kept started, {} refused, {} stopped by their owner, {} stopped at a level change",
                in_use, kPoolSize, g_peak, Guest(manager) ? int32_t(Load32(base, manager + kManagerActive)) : -1, KeptNow(),
                g_once_started.exchange(0), g_once_refused.exchange(0), g_once_budget.exchange(0), g_kept_started.exchange(0),
                g_kept_refused.exchange(0), g_kept_stopped.exchange(0), g_level_stops.exchange(0));
    g_peak = in_use;
  }
} // namespace pdz::snd

// Rare's range picker: the cues nothing ever played get the range of the closest comparable Rare sound (see the
// header). Called twice per Prime; three short name tests for a sound whose name starts like ours.
REX_HOOK_RAW(sub_82206888)
{
  const uint32_t sound = ctx.r3.u32, out = ctx.r4.u32;
  __imp__sub_82206888(ctx, base);
  if (!Guest(sound) || !Guest(out) || !REXCVAR_GET(pdz_restored_sounds))
  {
    return;
  }
  const char *name = reinterpret_cast<const char *>(base + sound + kSoundName);
  if (strnlen(name, 96) >= 96)
  {
    return;
  }
  if (const float fall = g_fall_range_m.load(std::memory_order_relaxed); fall > 0.0f && std::strstr(name, "BODYFALL") != nullptr)
  {
    StoreF(base, out, fall * 100.0f); // (Thu 19 Nov: only while our body fall is being started: its own range)
    return;
  }
  // Fri 20 Nov: Rare's own cues whose range the tester changed (each only over the figure Rare's picker gave).
  const float rare = LoadF(base, out);
  if (rare == 400.0f && REXCVAR_GET(pdz_snd_body_fall_rare_range) && std::strstr(name, "_BODYFALL_") != nullptr)
  {
    StoreF(base, out, float(REXCVAR_GET(pdz_snd_body_fall_range_m) * 100.0));
    // (Fri 20 Nov night: logged, so a play log shows the game's own body falls do get the agreed range. Called twice
    // per start: every second call is skipped; powers of two after the first 8.)
    static std::atomic<uint32_t> calls{0};
    const uint32_t n = calls.fetch_add(1, std::memory_order_relaxed) + 1;
    if ((n & 1u) && (n <= 15 || (((n + 1) / 2) & ((n + 1) / 2 - 1)) == 0))
    {
      const float at[3] = {LoadF(base, sound + 16), LoadF(base, sound + 20), LoadF(base, sound + 24)};
      REXLOG_INFO("pdz_snd_body_fall: the game's own body fall {} is given {:.0f} m instead of the game's 4 m "
                  "(pdz_snd_body_fall_rare_range); the listener {:.1f} m from it ({} so far)",
                  std::strlen(name) > 15 ? std::string_view(name).substr(15) : std::string_view(name),
                  REXCVAR_GET(pdz_snd_body_fall_range_m),
                  pdz::snd::ListenerCm(base, at) / 100.0f, (n + 1) / 2);
    }
    return;
  }
  if (rare == 5000.0f)
  {
    if (REXCVAR_GET(pdz_snd_sweeping_range) && std::strncmp(name, "XACT_SOUNDBANK_STOCK_SWEEPING_UP", 64) == 0)
    {
      StoreF(base, out, 1200.0f); // Rare's footstep figure (kind 3, walking)
      return;
    }
    if (REXCVAR_GET(pdz_snd_group_ranges))
    {
      if (const int g = GroupOf(name); g != 0)
      {
        StoreF(base, out, g == 1 ? 2000.0f : 500.0f); // Rare's bullet-hit figure / its substation hum
        return;
      }
    }
  }
  if (const float next = g_next_range_m.load(std::memory_order_relaxed);
      next > 0.0f && std::strncmp(name, "XACT_SOUNDBANK_STOCK_", 21) == 0)
  {
    StoreF(base, out, next * 100.0f); // (Fri 20 Nov: only while a hum of ours is being started: its own range)
    return;
  }
  if (std::strncmp(name, "XACT_SOUNDBANK_STOCK_", 21) != 0 || (name[21] != 'F' && name[21] != 'P' && name[21] != 'M' && name[21] != 'S'))
  {
    return;
  }
  for (const RangeRule &r : kRanges)
  {
    if (std::strncmp(name, r.name, 64) == 0)
    {
      if (r.which == 4 && LoadF(base, out) != 5000.0f)
      {
        return; // (Rare's picker has its own figure for this cue's kind: kept)
      }
      const double m = r.which == 0   ? REXCVAR_GET(pdz_snd_fusebox_hum_range_m)
                       : r.which == 1 ? (g_buzz_range_m.load(std::memory_order_relaxed) > 0.0f
                                             ? double(g_buzz_range_m.load(std::memory_order_relaxed))
                                             : REXCVAR_GET(pdz_snd_fluor_buzz_range_m))
                       : r.which == 2 ? REXCVAR_GET(pdz_snd_pigeon_flaps_range_m)
                       : r.which == 3 ? 10.0
                       : r.which == 5 ? (g_coo_range_m.load(std::memory_order_relaxed) > 0.0f
                                             ? double(g_coo_range_m.load(std::memory_order_relaxed))
                                             : REXCVAR_GET(pdz_snd_pigeon_coo_range_m))
                                      : 20.0;
      StoreF(base, out, float(m * 100.0));
      return;
    }
  }
}
