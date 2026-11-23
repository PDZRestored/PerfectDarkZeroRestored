// Walls make sounds quieter for the player (Fri 20 Nov 2026; pdz_snd_wall_cut, on, to try by ear). TU3 addresses.
// Needs pdz_restored_sounds.
//
// RARE'S RULE (sound radius audit, Thu 19 Nov): behind walls a sound is only MUFFLED (a low-pass filter from the span
//   between the first and last surface a ray crosses, sub_821FD928); its loudness is never cut, so one or two walls
//   change almost nothing. That filter stays exactly as it is.
// WHERE THE LOUDNESS IS APPLIED (research research notes: RESEARCH_walls_Fri20Nov.md, read again here):
//   every game frame the sound manager copies each sound's loudness (+332) into its sound GROUP (+96); the 3D pass
//   sub_821FCFE0 loads it into f2 (821FD0F0: lfs f2,96(r31), r31 = the group) and calls sub_8275E708 (X3DAudio), which
//   multiplies every output coefficient by f2. Only groups of sounds placed in the world (flag 0x10) get there:
//   music, the HUD, the background beds, radio speech and Jo's "in her hands" sounds never do.
// HERE: the hook of sub_8275E708 multiplies f2 by the group's wall factor (a table look-up and a multiply: no rays,
//   no syscalls). The factor is worked out on the game thread (Tick, from the physics manager's update):
//   - the walls and floors between the group's place (+52) and its listener (table 82D3EBD4, +128) are counted with
//     the SAME test the stealth layer uses for the guards' hearing (lab_stealth::SoundWallsBetween: Rare's
//     block-AI-sound geometry, mask 0x40, crossings 50 cm apart count once, both directions, at most 4);
//   - factor = (1 - pdz_snd_wall_cut_per_wall)^walls, never below pdz_snd_wall_cut_floor: 25 % per surface, 40 % at
//     least: one wall 0.75, two 0.56, three 0.42, four 0.40;
//   - never for: a group within 1.5 m of the listener or of a human player's body (her own steps, gun, clothes, also
//     in third person), and, by a pool walk every 0.5 s (its pointers checked: the pool's +20 POINTS to the array of
//     80; free sounds are skipped by the free list from +4), a group holding a sound owned by a human player, the
//     Nightclub's two placed music beds (kinds 12 and 15), any MUSIC_ cue, radio / not-placed kinds. The groups
//     themselves come from the gain hook (only groups Rare's 3D pass plays: no stale pointer is ever followed);
//   - one group is re-counted at most every 0.4 s and at most 2 counts are made per update (the rest keep their last
//     count); the factor moves to its new value by at most 0.08 an update, so a door opening fades rather than jumps.
//   Cost: a pdz_snd_wall_cut_log line every 30 s gives the counts made, their mean time and the time per update.
#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/memory/utils.h>
#include <rex/ppc/func.h>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <fmt/format.h>
#include "guest_readable_Sat21Nov.h" // Sat 21 Nov: pdz_fix_readable_cache

REXCVAR_DECLARE(bool, pdz_restored_sounds);
REXCVAR_DEFINE_BOOL(pdz_snd_wall_cut, true, "Gameplay",
                    "Restored sound (Fri 20 Nov, to try by ear): each wall or floor between a sound and the player "
                    "makes it a quarter quieter (never below 40 %), on top of the game's own muffling. Not for the "
                    "player's own sounds, radio voices, music, the HUD or the background. Needs pdz_restored_sounds")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_DOUBLE(pdz_snd_wall_cut_per_wall, 0.25, "Gameplay",
                      "pdz_snd_wall_cut: the part of the loudness each wall or floor takes off (0.25 = a quarter)")
    .range(0.0, 0.9)
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_DOUBLE(pdz_snd_wall_cut_floor, 0.4, "Gameplay",
                      "pdz_snd_wall_cut: walls never make a sound quieter than this part of its loudness")
    .range(0.0, 1.0)
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_DOUBLE(pdz_snd_wall_cut_skip_cm, 60.0, "Gameplay",
                      "pdz_snd_wall_cut (Fri 20 Nov night): surfaces this close to a sound (cm) are the object it sits on "
                      "or in (a pigeon on a lamp head, a hum inside its box) and do not count as walls. 0 = every surface "
                      "counts (before)")
    .range(0.0, 200.0)
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
// Sat 21 Nov evening (log 034: Rare's indoor fountain heard by the vans outside the Nightclub through "1 wall" at
// -13.8 dB, the water laps through 4+ walls at x0.42, our Laboratory strip light ~10 m above it through 4+ floors
// at -16.6 dB): the old rule bottomed out at x0.40 (-8 dB) and counted at most 4 surfaces of one ray kind.
REXCVAR_DEFINE_BOOL(pdz_snd_wall_cut_deep, true, "Gameplay",
                    "Restored sound (Sat 21 Nov evening): walls and floors cut properly: each wall or floor between a "
                    "sound and the player takes pdz_snd_wall_cut_deep_db off (6 dB: half as loud), up to "
                    "pdz_snd_wall_cut_deep_max of them, with no 40 % floor, so a sound shut inside or below a building "
                    "is nearly silent from outside or above; walls are found with Rare's sight and sound blockers, and "
                    "the floors between different storeys are counted too. Off = the Fri 20 Nov rule (a quarter per "
                    "wall, at most 4, never below 40 %)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_DOUBLE(pdz_snd_wall_cut_deep_db, 6.0, "Gameplay",
                      "pdz_snd_wall_cut_deep: decibels each wall or floor takes off (6 = half as loud)")
    .range(0.0, 20.0)
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_INT32(pdz_snd_wall_cut_deep_max, 6, "Gameplay",
                     "pdz_snd_wall_cut_deep: walls and floors counted at most (6 x 6 dB = 36 dB)")
    .range(1, 12)
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_INT32(pdz_snd_wall_cut_deep_mask, 0x60, "Gameplay",
                     "pdz_snd_wall_cut_deep: the physics ray's collision mask for walls (0x20 Rare's AI sight blockers, "
                     "0x40 its AI sound blockers, 0x60 either)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_snd_wall_cut_storeys, true, "Gameplay",
                    "pdz_snd_wall_cut_deep: a sound on another storey (2.5 m or more above or below the player) also "
                    "counts the floors / ceilings straight above or below it that the player's own column crosses too "
                    "(the fewer of the two), when that is more than the straight line counts")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_INT32(pdz_snd_wall_cut_inside_walls, 3, "Gameplay",
                     "pdz_snd_wall_cut_deep (Sat 21 Nov evening; log 034: the Nightclub's indoor fountain, heard by the vans "
                     "outside through what the ray counts as one wall): when one of the sound and the player is enclosed "
                     "(walls, floor or ceiling within 15 m in at least 7 of 9 directions) and the other is in the open "
                     "(4 of 9 at most), and at least one wall is between them, the building's shell counts as this many "
                     "more walls. 0 = off")
    .range(0, 6)
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_snd_wall_cut_log, true, "Debug",
                    "Log only: every 30 s a 'pdz_snd_wall_cut:' line (groups cut, wall counts made, their cost)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);

REX_EXTERN(__imp__sub_8275E708); // the 3D pass's X3DAudio step (f2 = the group's loudness; r31 = the group in the caller)

namespace lab_stealth
{
  int SoundWallsBetween(PPCContext &ctx, uint8_t *base, const float src[3], const float ears[3]); // lab_stealth.cpp
  int SurfacesBetween(PPCContext &ctx, uint8_t *base, const float a[3], const float b[3], int cap, uint32_t mask);
  bool SegmentBlocked(PPCContext &ctx, uint8_t *base, const float a[3], const float b[3], uint32_t mask);
}
namespace pdz::walls
{
  void Tick(PPCContext &ctx, uint8_t *base);
  int Count(PPCContext &ctx, uint8_t *base, const float src[3], const float ears[3], int *straight, int *storeys);
  float Factor(int walls);
  std::string Probe(PPCContext &ctx, uint8_t *base, const float src[3], const float ears[3]);
}

namespace
{
  constexpr uint32_t kGame = 0x82D6BFD8;     // +0 -> the session (+8 == 21 in play)
  constexpr uint32_t kPool = 0x82E6915C;     // +4 the free list, +20 -> the array of 80 sounds x 372
  constexpr int kPoolSize = 80;
  constexpr uint32_t kStride = 372;
  constexpr uint32_t kListeners = 0x82D3EBD4; // 6 listener pointers (+128 position)
  constexpr uint32_t kPhysicsWorld = 0x82E3C2F4;
  constexpr uint32_t kHumans = 0x82E3C3E4, kHumanNext = 6792;
  constexpr uint32_t kGroupListener = 40, kGroupPlace = 52;

  uint32_t Load32(const uint8_t *base, uint32_t a)
  {
    const uint8_t *p = base + a;
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
  }
  float LoadF(const uint8_t *base, uint32_t a) { return std::bit_cast<float>(Load32(base, a)); }
  bool Guest(uint32_t a) { return a >= 0x10000u && a < 0xF0000000u && (a & 3u) == 0; }
  bool Finite(float f) { return f == f && f > -1.0e7f && f < 1.0e7f; }

  // The groups' factors: written by Tick (the game thread), read by the hook. Key = the group's address.
  constexpr int kSlots = 256;
  struct Slot
  {
    std::atomic<uint32_t> group{0};
    std::atomic<float> applied{1.0f};
    float target = 1.0f;
    int walls = 0;
    int64_t next_count_ms = 0;
    uint32_t seen_tick = 0;
  };
  std::array<Slot, kSlots> g_slots;
  uint32_t g_tick = 0;

  int Home(uint32_t group) { return int((group >> 3) * 2654435761u >> 24) & (kSlots - 1); }
  Slot *Find(uint32_t group)
  {
    for (int n = 0, i = Home(group); n < 16; ++n, i = (i + 1) & (kSlots - 1))
    {
      const uint32_t g = g_slots[i].group.load(std::memory_order_relaxed);
      if (g == group)
      {
        return &g_slots[i];
      }
      if (g == 0)
      {
        return nullptr;
      }
    }
    return nullptr;
  }
  Slot *FindOrAdd(uint32_t group)
  {
    for (int n = 0, i = Home(group); n < 16; ++n, i = (i + 1) & (kSlots - 1))
    {
      Slot &s = g_slots[i];
      const uint32_t g = s.group.load(std::memory_order_relaxed);
      if (g == group)
      {
        return &s;
      }
      if (g == 0)
      {
        s.applied.store(1.0f, std::memory_order_relaxed);
        s.target = 1.0f;
        s.walls = 0;
        s.next_count_ms = 0;
        s.group.store(group, std::memory_order_relaxed);
        return &s;
      }
    }
    return nullptr;
  }
  void ClearAll()
  {
    for (Slot &s : g_slots)
    {
      s.group.store(0, std::memory_order_relaxed);
      s.applied.store(1.0f, std::memory_order_relaxed);
    }
  }

  int64_t NowMs()
  {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
  }
  // Log figures since the last line.
  uint64_t g_counts = 0, g_count_ns = 0, g_ticks = 0, g_tick_ns = 0, g_group_samples = 0, g_cut_samples = 0;
  int g_max_walls = 0;
  uint64_t g_why[6] = {}; // groups not counted: 0 no place / listener, 1 unknown to the pool walk, 2 kept by kind / owner, 3 within 1.5 m, 4 at a player, 5 counted
  int64_t g_log_ms = 0, g_classify_ms = 0;
  bool g_was_on = false;
  std::unordered_set<uint32_t> g_keep, g_known_groups;
  // The groups the gain hook saw since the last Tick.
  constexpr int kSeen = 192;
  std::array<std::atomic<uint32_t>, kSeen> g_seen{};
  std::atomic<int> g_seen_n{0};
  bool Readable(uint8_t *base, uint32_t address, uint32_t size)
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
      if (!rex::memory::QueryProtect(base + at, length, access) ||
          (uint32_t(access) & uint32_t(rex::memory::PageAccess::kReadOnly)) == 0)
      {
        return false;
      }
    }
    return true;
  }
} // namespace

namespace
{
  // Sat 21 Nov evening (pdz_snd_wall_cut_inside): how enclosed a place is: of 9 rays (straight up and 8 ways level, 15 m,
  // from 20 cm above it), how many hit Rare's sight blockers. Kept per 50 cm cell (a placed sound never moves; the
  // listener's cell changes as it walks), at most 1024 cells, forgotten when a level ends.
  std::unordered_map<uint64_t, int> g_enclosed;
  int Enclosed(PPCContext &ctx, uint8_t *base, const float p[3])
  {
    const uint64_t key = (uint64_t(uint32_t(int32_t(std::floor(p[0] / 50.0f))) & 0x1FFFFF) << 42) |
                         (uint64_t(uint32_t(int32_t(std::floor(p[1] / 50.0f))) & 0x1FFFFF) << 21) |
                         uint64_t(uint32_t(int32_t(std::floor(p[2] / 50.0f))) & 0x1FFFFF);
    if (const auto it = g_enclosed.find(key); it != g_enclosed.end())
    {
      return it->second;
    }
    if (g_enclosed.size() > 1024)
    {
      g_enclosed.clear();
    }
    int hits = 0;
    const float at[3] = {p[0], p[1] + 20.0f, p[2]};
    for (int k = 0; k < 9; ++k)
    {
      const float a = float(k) * 0.785398f;
      const float to[3] = {k == 8 ? at[0] : at[0] + 1500.0f * std::cos(a), k == 8 ? at[1] + 1500.0f : at[1],
                           k == 8 ? at[2] : at[2] + 1500.0f * std::sin(a)};
      hits += lab_stealth::SegmentBlocked(ctx, base, at, to, 0x20) ? 1 : 0;
    }
    g_enclosed[key] = hits;
    return hits;
  }
} // namespace

namespace pdz::walls
{
  // The walls and floors between a sound and the listener, as the cut counts them (also the F12 listing's count).
  // The surfaces within pdz_snd_wall_cut_skip_cm of the sound (what it sits on or in) never count.
  int Count(PPCContext &ctx, uint8_t *base, const float src[3], const float ears[3], int *straight_out, int *storeys_out)
  {
    const float dx = src[0] - ears[0], dy = src[1] - ears[1], dz = src[2] - ears[2];
    const float d = std::sqrt(dx * dx + dy * dy + dz * dz);
    float from[3] = {src[0], src[1], src[2]};
    const float skip = float(REXCVAR_GET(pdz_snd_wall_cut_skip_cm));
    if (skip > 0.0f && d > skip + 50.0f)
    {
      for (int j = 0; j < 3; ++j)
      {
        from[j] = src[j] + (ears[j] - src[j]) * (skip / d);
      }
    }
    int straight = 0, storeys = 0, walls = 0, shell = 0;
    if (!REXCVAR_GET(pdz_snd_wall_cut_deep))
    {
      walls = straight = lab_stealth::SoundWallsBetween(ctx, base, from, ears); // (Fri 20 Nov: mask 0x40, at most 4)
    }
    else
    {
      const int cap = std::clamp(int(REXCVAR_GET(pdz_snd_wall_cut_deep_max)), 1, 12);
      const uint32_t mask = uint32_t(REXCVAR_GET(pdz_snd_wall_cut_deep_mask)) & 0xFFFFu;
      straight = lab_stealth::SurfacesBetween(ctx, base, from, ears, cap, mask ? mask : 0x60u);
      const float rise = ears[1] - src[1]; // (y is up)
      if (REXCVAR_GET(pdz_snd_wall_cut_storeys) && std::fabs(rise) >= 250.0f && straight < cap)
      {
        const float s = rise > 0.0f ? 1.0f : -1.0f;
        // The sound's column (from past what it sits on to the listener's height), then the listener's own column
        // (to the sound's height): a floor between storeys is crossed by both; an open sky or an atrium by one only.
        const float a0[3] = {src[0], src[1] + s * std::max(skip, 30.0f), src[2]}, a1[3] = {src[0], ears[1], src[2]};
        const int up = lab_stealth::SurfacesBetween(ctx, base, a0, a1, cap, mask ? mask : 0x60u);
        if (up > 0)
        {
          const float b1[3] = {ears[0], src[1], ears[2]};
          storeys = std::min(up, lab_stealth::SurfacesBetween(ctx, base, ears, b1, cap, mask ? mask : 0x60u));
        }
      }
      walls = std::max(straight, storeys);
      // Sat 21 Nov evening: one end shut inside, the other in the open (pdz_snd_wall_cut_inside_walls).
      if (const int extra = REXCVAR_GET(pdz_snd_wall_cut_inside_walls); extra > 0 && walls >= 1)
      {
        const int es = Enclosed(ctx, base, src), el = Enclosed(ctx, base, ears);
        if ((es >= 7 && el <= 4) || (el >= 7 && es <= 4))
        {
          walls += extra;
          shell = extra;
        }
      }
      walls = std::min(cap, walls);
    }
    if (straight_out)
    {
      *straight_out = straight;
    }
    if (storeys_out)
    {
      *storeys_out = storeys + 100 * shell; // (+100 per building-shell wall: read by Probe)
    }
    return walls;
  }
  // The loudness factor for that many walls and floors.
  float Factor(int walls)
  {
    if (walls <= 0)
    {
      return 1.0f;
    }
    if (!REXCVAR_GET(pdz_snd_wall_cut_deep))
    {
      return float(std::max(REXCVAR_GET(pdz_snd_wall_cut_floor), std::pow(1.0 - REXCVAR_GET(pdz_snd_wall_cut_per_wall), double(walls))));
    }
    return float(std::max(0.001, std::pow(10.0, -REXCVAR_GET(pdz_snd_wall_cut_deep_db) * double(walls) / 20.0)));
  }
  // Log only (the F12 listing): the counts by each ray kind, and how enclosed the sound and the listener are (rays
  // up and in 8 directions, 15 m, Rare's sight blockers: how many hit something).
  std::string Probe(PPCContext &ctx, uint8_t *base, const float src[3], const float ears[3])
  {
    int st = 0, sy = 0;
    const int now = Count(ctx, base, src, ears, &st, &sy);
    const float dx = src[0] - ears[0], dy = src[1] - ears[1], dz = src[2] - ears[2];
    const float d = std::sqrt(dx * dx + dy * dy + dz * dz);
    float from[3] = {src[0], src[1], src[2]};
    if (d > 110.0f)
    {
      for (int j = 0; j < 3; ++j)
      {
        from[j] = src[j] + (ears[j] - src[j]) * (60.0f / d);
      }
    }
    const int m20 = lab_stealth::SurfacesBetween(ctx, base, from, ears, 8, 0x20);
    const int m40 = lab_stealth::SurfacesBetween(ctx, base, from, ears, 8, 0x40);
    const int m60 = lab_stealth::SurfacesBetween(ctx, base, from, ears, 8, 0x60);
    auto enclosed = [&](const float p[3]) {
      int hits = 0;
      for (int k = 0; k < 9; ++k)
      {
        const float a = float(k) * 0.785398f;
        const float to[3] = {k == 8 ? p[0] : p[0] + 1500.0f * std::cos(a), k == 8 ? p[1] + 1500.0f : p[1] + 20.0f,
                             k == 8 ? p[2] : p[2] + 1500.0f * std::sin(a)};
        const float at[3] = {p[0], p[1] + 20.0f, p[2]};
        hits += lab_stealth::SurfacesBetween(ctx, base, at, to, 1, 0x20) > 0 ? 1 : 0;
      }
      return hits;
    };
    return fmt::format("walls now {} (straight {}, storeys {}, building shell {}; factor {:.3f}) | by ray kind, 8 at most: sight 0x20 {}, sound 0x40 {}, "
                       "either 0x60 {} | enclosed (of 9 rays, 15 m): the sound {}, the listener {}",
                       now, st, sy % 100, sy / 100, Factor(now), m20, m40, m60, enclosed(src), enclosed(ears));
  }

  void Tick(PPCContext &ctx, uint8_t *base)
  {
    const bool on = REXCVAR_GET(pdz_restored_sounds) && REXCVAR_GET(pdz_snd_wall_cut);
    if (!on)
    {
      if (g_was_on)
      {
        ClearAll();
      }
      g_seen_n.store(0, std::memory_order_relaxed);
      g_was_on = false;
      return;
    }
    g_was_on = true;
    const uint32_t session = Load32(base, kGame);
    if (!Guest(session) || Load32(base, session + 8) != 21 || !Guest(Load32(base, kPhysicsWorld)) ||
        !Guest(Load32(base, kPhysicsWorld - 4)))
    {
      ClearAll(); // (not in play: everything at full loudness, the next level starts clean)
      g_enclosed.clear();
      g_seen_n.store(0, std::memory_order_relaxed);
      return;
    }
    const auto t0 = std::chrono::steady_clock::now();
    const int64_t now = NowMs();
    ++g_tick;
    // Which groups must never be cut: a pool walk every 0.5 s (a free sound keeps stale pointers, so every pointer is
    // checked before it is read: a few syscalls, twice a second, never in the gain hook).
    if (now >= g_classify_ms)
    {
      g_classify_ms = now + 500;
      g_keep.clear();
      g_known_groups.clear();
      // (The pool's +20 points to the array; its free objects are a list from +4, each +0 the next: the audit's walk.)
      const uint32_t array = Load32(base, kPool + 20);
      std::array<bool, kPoolSize> free{};
      for (uint32_t f = Guest(array) ? Load32(base, kPool + 4) : 0, n = 0; Guest(f) && n <= uint32_t(kPoolSize); ++n, f = Load32(base, f))
      {
        const uint32_t i = (f - array) / kStride;
        if (f < array || i >= uint32_t(kPoolSize) || (f - array) % kStride)
        {
          break;
        }
        free[i] = true;
      }
      for (int i = 0; Guest(array) && i < kPoolSize; ++i)
      {
        if (free[size_t(i)])
        {
          continue;
        }
        const uint32_t s = array + uint32_t(i) * kStride;
        const uint32_t flags = Load32(base, s + 336), state = Load32(base, s + 340);
        if ((flags & 0x10u) == 0 || state == 6 || state == 8 || base[s + 52] == 0)
        {
          continue;
        }
        const uint32_t link = Load32(base, s);
        if (!Guest(link) || !Readable(base, link, 16))
        {
          continue;
        }
        const uint32_t group = Load32(base, link + 12);
        if (!Guest(group))
        {
          continue;
        }
        const uint32_t kind = Load32(base, s + 312) & 63u;
        const uint32_t owner = Load32(base, s + 12);
        bool keep = kind == 12 || kind == 15 || kind == 18 || (kind >= 20 && kind <= 24) ||
                    std::strncmp(reinterpret_cast<const char *>(base + s + 52), "XACT_SOUNDBANK_MUSIC_", 21) == 0;
        for (uint32_t h = Load32(base, kHumans), n = 0; !keep && Guest(h) && n < 4; ++n, h = Load32(base, h + kHumanNext))
        {
          keep = owner == h; // (a player's own sound: her steps, her gun, her clothes)
        }
        g_known_groups.insert(group);
        if (keep)
        {
          g_keep.insert(group);
        }
      }
    }
    // The groups Rare's 3D pass played since the last update (noted by the gain hook: live groups only).
    struct G
    {
      uint32_t group;
      bool keep;
    };
    std::array<G, kSeen> groups{};
    int n_groups = 0;
    const int seen = std::min(g_seen_n.exchange(0, std::memory_order_relaxed), kSeen);
    for (int i = 0; i < seen; ++i)
    {
      const uint32_t group = g_seen[i].load(std::memory_order_relaxed);
      int k = 0;
      while (k < n_groups && groups[k].group != group)
      {
        ++k;
      }
      if (k == n_groups && Guest(group))
      {
        // (A group the last pool walk has not classified yet, a short sound such as a shot or an impact, is cut like the
        // rest: a player's own sounds are caught by the at-a-player test below, and the music / beds / radio are long
        // lived or not placed at all.)
        groups[n_groups++] = {group, g_keep.count(group) != 0};
      }
    }
    const double per = REXCVAR_GET(pdz_snd_wall_cut_per_wall), floor = REXCVAR_GET(pdz_snd_wall_cut_floor);
    int counted = 0;
    for (int k = 0; k < n_groups; ++k)
    {
      Slot *slot = FindOrAdd(groups[k].group);
      if (!slot)
      {
        continue;
      }
      slot->seen_tick = g_tick;
      const uint32_t g = groups[k].group;
      if (!pdz::guestmem::Ok(base, g, kGroupPlace + 12)) // Sun 22 Nov night (audit): a group freed since the gain hook saw it
      {
        continue;
      }
      const uint32_t li = Load32(base, g + kGroupListener);
      const uint32_t listener = Load32(base, kListeners + 4 * (li < 6 ? li : 0));
      const float src[3] = {LoadF(base, g + kGroupPlace), LoadF(base, g + kGroupPlace + 4), LoadF(base, g + kGroupPlace + 8)};
      float ears[3] = {};
      bool ok = Guest(listener) && Finite(src[0]) && Finite(src[1]) && Finite(src[2]);
      if (ok)
      {
        for (int j = 0; j < 3; ++j)
        {
          ears[j] = LoadF(base, listener + 128 + 4 * j);
        }
        ok = Finite(ears[0]) && Finite(ears[1]) && Finite(ears[2]);
      }
      const float dx = src[0] - ears[0], dy = src[1] - ears[1], dz = src[2] - ears[2];
      const float d = ok ? std::sqrt(dx * dx + dy * dy + dz * dz) : 0.0f;
      bool near_player = false;
      for (uint32_t h = Load32(base, kHumans), n = 0; ok && !near_player && Guest(h) && n < 4; ++n, h = Load32(base, h + kHumanNext))
      {
        const float px = src[0] - LoadF(base, h + 48), py = src[1] - LoadF(base, h + 52), pz = src[2] - LoadF(base, h + 56);
        near_player = px * px + py * py + pz * pz < 150.0f * 150.0f;
      }
      g_why[!ok ? 0 : groups[k].keep ? 2 : d < 150.0f ? 3 : near_player ? 4 : g_known_groups.count(g) ? 5 : 1]++;
      if (!ok || groups[k].keep || d < 150.0f || near_player)
      {
        slot->target = 1.0f;
        slot->walls = 0;
      }
      else if (now >= slot->next_count_ms && counted < 2)
      {
        ++counted;
        const auto c0 = std::chrono::steady_clock::now();
        // Fri 20 Nov night (the lamp-post pigeon: its coo counted 2 walls, the lamp head it sits on): the surfaces
        // within pdz_snd_wall_cut_skip_cm of the sound (the object it sits on or in) do not count as walls (Count).
        // Sat 21 Nov evening: pdz_snd_wall_cut_deep (Count / Factor).
        slot->walls = pdz::walls::Count(ctx, base, src, ears, nullptr, nullptr);
        g_count_ns += uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - c0).count());
        ++g_counts;
        g_max_walls = std::max(g_max_walls, slot->walls);
        slot->next_count_ms = now + 400;
        slot->target = pdz::walls::Factor(slot->walls);
      }
      // The factor moves towards its target (no jumps).
      const float a = slot->applied.load(std::memory_order_relaxed);
      const float step = std::clamp(slot->target - a, -0.08f, 0.08f);
      slot->applied.store(a + step, std::memory_order_relaxed);
      ++g_group_samples;
      if (slot->target < 0.999f)
      {
        ++g_cut_samples;
      }
    }
    // Groups that no longer play: forgotten (a new group at that address starts at full loudness).
    for (Slot &s : g_slots)
    {
      const uint32_t g = s.group.load(std::memory_order_relaxed);
      if (g && g_tick - s.seen_tick > 30) // (not played for 30 updates)
      {
        s.applied.store(1.0f, std::memory_order_relaxed);
        s.group.store(0, std::memory_order_relaxed);
      }
    }
    // (Removing slots can break a probe chain: rebuild the table when that may have happened. Cheap: <= 80 groups.)
    {
      std::array<std::pair<uint32_t, float>, kSlots> live{};
      std::array<int, kSlots> walls{};
      std::array<float, kSlots> target{};
      std::array<int64_t, kSlots> next{};
      std::array<uint32_t, kSlots> seen_at{};
      int n = 0;
      bool gap = false;
      for (Slot &s : g_slots)
      {
        const uint32_t g = s.group.load(std::memory_order_relaxed);
        if (g)
        {
          walls[n] = s.walls, target[n] = s.target, next[n] = s.next_count_ms, seen_at[n] = s.seen_tick;
          live[n++] = {g, s.applied.load(std::memory_order_relaxed)};
          gap = gap || Find(g) != &s;
        }
      }
      if (gap)
      {
        ClearAll();
        for (int i = 0; i < n; ++i)
        {
          if (Slot *s = FindOrAdd(live[i].first))
          {
            s->applied.store(live[i].second, std::memory_order_relaxed);
            s->walls = walls[i], s->target = target[i], s->next_count_ms = next[i], s->seen_tick = seen_at[i];
          }
        }
      }
    }
    ++g_ticks;
    g_tick_ns += uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - t0).count());
    if (REXCVAR_GET(pdz_snd_wall_cut_log) && now - g_log_ms >= 30000)
    {
      if (g_log_ms != 0 && g_ticks)
      {
        int cut_now = 0;
        for (const Slot &s : g_slots)
        {
          cut_now += s.group.load(std::memory_order_relaxed) && s.target < 0.999f ? 1 : 0;
        }
        REXLOG_INFO("pdz_snd_wall_cut: {} groups playing, {} of them behind walls now; in the last 30 s: {} wall counts "
                    "(mean {:.0f} us each, the most walls {}), {:.1f} us per update on average over {} updates "
                    "({:.0f} % of group-updates cut); {}",
                    n_groups, cut_now, g_counts, g_counts ? double(g_count_ns) / 1000.0 / double(g_counts) : 0.0, g_max_walls,
                    double(g_tick_ns) / 1000.0 / double(g_ticks), g_ticks,
                    100.0 * double(g_cut_samples) / double(std::max<uint64_t>(1, g_group_samples)),
                    REXCVAR_GET(pdz_snd_wall_cut_deep)
                        ? fmt::format("deep rule: {:.1f} dB a wall or floor, at most {} counted, ray mask {:#x}, storeys {}",
                                      REXCVAR_GET(pdz_snd_wall_cut_deep_db), REXCVAR_GET(pdz_snd_wall_cut_deep_max),
                                      REXCVAR_GET(pdz_snd_wall_cut_deep_mask), REXCVAR_GET(pdz_snd_wall_cut_storeys) ? "on" : "off")
                        : fmt::format("{:.0f} % a wall, at least {:.0f} %", per * 100.0, floor * 100.0));
        REXLOG_INFO("pdz_snd_wall_cut: group-updates by reason: no place or listener {}, tested before the pool walk saw them {}, never cut "
                    "(kind / a player's own) {}, within 1.5 m {}, at a player {}, tested {} | pool walk knows {} groups, {} kept",
                    g_why[0], g_why[1], g_why[2], g_why[3], g_why[4], g_why[5], g_known_groups.size(), g_keep.size());
      }
      g_log_ms = now;
      g_counts = g_count_ns = g_ticks = g_tick_ns = g_cut_samples = g_group_samples = 0;
      g_max_walls = 0;
      std::fill(std::begin(g_why), std::end(g_why), 0);
    }
  }
} // namespace pdz::walls

// The 3D pass's X3DAudio step (only caller sub_821FCFE0, once per playing group per audio update): the group's
// loudness in f2 is multiplied by its wall factor. Hot: a table look-up only.
REX_HOOK_RAW(sub_8275E708)
{
  if (REXCVAR_GET(pdz_snd_wall_cut))
  {
    const int n = g_seen_n.load(std::memory_order_relaxed);
    if (n < kSeen)
    {
      g_seen[n].store(ctx.r31.u32, std::memory_order_relaxed); // (the game thread: Tick reads them next update)
      g_seen_n.store(n + 1, std::memory_order_relaxed);
    }
    if (const Slot *s = Find(ctx.r31.u32))
    {
      const float f = s->applied.load(std::memory_order_relaxed);
      if (f < 0.999f && f >= 0.0f)
      {
        ctx.f2.f64 *= double(f);
      }
    }
  }
  __imp__sub_8275E708(ctx, base);
}
