// Guards' aim at Jo (Wed 18 Nov 2026, job aim120). TU3 addresses. Log only: nothing here changes the game.
//
// pdz_jo_hit_log (off): for the bench "do guards miss Jo more at 120 fps than at 60?".
//   - Every NPC shot: Rare's per-shot aim routine sub_82539698 (r3 the shooter, r4 his weapon) takes the gun's muzzle
//     and Rare's aim point on the target (vfunc +0x2F0, sub_82157618), adds the shooter's aim error (a random offset in
//     cm, its size from the difficulty profile and his accuracy byte) and fires the bullet towards it (weapon vfunc
//     +0x274). Its target is char +0x316C, else +0x36D0. One line per shot at Jo: the shooter, his gun, the distance,
//     where the aim point lands next to her (on the ground plan, and its height above her feet), and how far his body
//     faces away from her (yaw +0x1670 against the way to her). The aim point is read back from the routine's frame
//     (0xD0 below r1: origin at +0x68, aim point at +0x58) after it returned; a value that is not near the shooter /
//     near Jo is not used.
//   - Every hit on Jo: the player damage handler sub_825AAF50 (r3 victim, r4 attacker, f1 damage, as applied to her
//     health), before the testing tool pdz_test_god_mode zeroes it.
//   - Facing: Rare's per-character target step sub_82580508 runs for each character each update; for each living NPC
//     whose target (+0x316C, else +0x3154) is Jo, his body yaw is followed with the stealth layer's FACING SWINGS rule
//     (a turn back of over 6 deg is a reversal; reversals within 0.6 s of each other are one swinging; four over at
//     least 1 s is one swinging episode), so it counts the same with the layer on or off.
//   - A SUMMARY line every 10 s of the AI clock (totals since the level start): shots, hits, hit rate, damage per
//     minute, Jo's health and how long it would last, the facing counts, AI updates a guard-second.
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>
#include <map>
#include <unordered_map>

#include <fmt/format.h>

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/ppc/func.h>

#include "aim120_Wed18Nov.h"

REXCVAR_DEFINE_BOOL(pdz_jo_hit_log, false, "Diagnostics",
                    "Log every shot a guard fires at Jo (where it was aimed, how far he faced away), every hit on her "
                    "and a summary every 10 s (hit rate, damage a minute, how long she would last, guards' facing "
                    "swings). Log only; off by default")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);

namespace
{
  constexpr uint32_t kPlayerOne = 0x82E3C3E4; // player 1's character
  constexpr uint32_t kAiClock = 0x82E2F334;   // float seconds, per level
  constexpr uint32_t kGame = 0x82D6BFD8;      // -> game info: +4 difficulty (0 Agent .. 3 Dark Agent), +25 level
  constexpr uint32_t kClass = 12;             // character class: 33 NPC, 2 player, 43 dead
  constexpr uint32_t kPos = 0x30;             // position (cm)
  constexpr uint32_t kYaw = 0x1670;           // body yaw (deg)
  constexpr uint32_t kData = 0x54;            // -> setup data; +0x34 -> setup id string
  constexpr uint32_t kWeaponRecord = 0x35C, kRecordGunType = 0x478;
  constexpr float kSwingDeg = 6.0f, kSwingGapS = 0.6f, kSwingS = 1.0f;
  constexpr int kSwingRevs = 4;

  uint32_t Load32(const uint8_t *base, uint32_t a)
  {
    const uint8_t *p = base + a;
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
  }
  float LoadF(const uint8_t *base, uint32_t a) { return std::bit_cast<float>(Load32(base, a)); }
  bool Ptr(uint32_t a) { return a >= 0x10000u && a < 0xF0000000u; }
  bool Finite(float v) { return v > -1e7f && v < 1e7f; }
  float Wrap(float d) { return d > 180.0f ? d - 360.0f : d < -180.0f ? d + 360.0f : d; }

  std::string Id(const uint8_t *base, uint32_t c)
  {
    const uint32_t data = Ptr(c) ? Load32(base, c + kData) : 0;
    const uint32_t text = Ptr(data) ? Load32(base, data + 0x34) : 0;
    if (!Ptr(text))
    {
      return fmt::format("{:08X}", c);
    }
    std::string s;
    for (int i = 0; i < 23 && base[text + i]; ++i)
    {
      s += char(base[text + i]);
    }
    return s;
  }
  void Pos(const uint8_t *base, uint32_t c, float out[3])
  {
    for (int i = 0; i < 3; ++i)
    {
      out[i] = LoadF(base, c + kPos + 4 * i);
    }
  }
  float JoHealth(const uint8_t *base, uint32_t jo)
  {
    const uint32_t p = Load32(base, jo + 0x19A0); // participant; +0x3D4 a human's health (counter_op.cpp's reading)
    return Ptr(p) ? LoadF(base, p + 0x3D4) : LoadF(base, jo + 120);
  }

  struct Guard
  {
    std::string id;
    int shots = 0, hits = 0;
    double damage = 0.0;
    float last_shot = -1e9f;
    // facing (the layer's swing rule)
    float yaw_ext = 0.0f;
    int yaw_dir = 0;
    bool yaw_seen = false;
    float rev_at = -1e9f, rev_from = -1e9f;
    int revs = 0;
    bool episode = false;
    float last_tick = -1e9f;
    float prev_yaw = 0.0f;
  };

  struct Totals
  {
    int shots = 0, hits = 0, shots_other = 0, aim_read = 0;
    double damage = 0.0, miss_sum = 0.0, face_sum = 0.0;
    int face_n = 0, face_over15 = 0;
    float first_shot = -1.0f;
    double fight_s = 0.0; // guard-seconds with Jo as target
    long long ticks = 0;  // their AI updates
    int revs = 0, episodes = 0;
    float widest = 0.0f;
    double turn_sum = 0.0;
    struct Writer
    {
      int sets = 0, adds = 0;
      double moved = 0.0; // |change| it made (deg)
      int big = 0;        // changes over 3 deg
    };
    std::map<uint32_t, Writer> writers; // yaw writes to guards holding Jo, by caller
  };

  std::mutex g_lock;
  std::unordered_map<uint32_t, Guard> g_guards;
  Totals g_t;
  float g_clock = -1.0f, g_next_summary = 10.0f, g_health0 = -1.0f;
  uint32_t g_level = 0xFFFFFFFF;
  uint32_t g_shot_npc = 0;   // the shot in flight (sub_82539698 runs nested? not seen: one at a time)
  uint32_t g_shot_target = 0;

  bool On() { return REXCVAR_GET(pdz_jo_hit_log); }

  Guard &G(const uint8_t *base, uint32_t c)
  {
    auto it = g_guards.find(c);
    if (it == g_guards.end())
    {
      if (g_guards.size() > 256)
      {
        g_guards.clear();
      }
      it = g_guards.emplace(c, Guard()).first;
      it->second.id = Id(base, c);
    }
    return it->second;
  }

  void Summary(const uint8_t *base, float now, const char *why)
  {
    const uint32_t jo = Load32(base, kPlayerOne);
    const uint32_t game = Load32(base, kGame);
    const int difficulty = Ptr(game) ? int(Load32(base, game + 4)) : -1;
    const float health = Ptr(jo) ? JoHealth(base, jo) : -1.0f;
    const double since = g_t.first_shot >= 0.0f ? std::max(1.0, double(now - g_t.first_shot)) : 0.0;
    const double per_min = since > 0.0 ? g_t.damage * 60.0 / since : 0.0;
    std::string last = "never";
    if (per_min > 0.0 && g_health0 > 0.0f)
    {
      last = fmt::format("{:.1f} min", g_health0 / per_min);
    }
    float jp[3] = {};
    if (Ptr(jo))
    {
      Pos(base, jo, jp);
    }
    std::string per;
    int n = 0;
    for (const auto &[c, g] : g_guards)
    {
      if (g.shots > 0 && n < 12)
      {
        per += fmt::format(" [{} shots {} hits {} damage {:.1f}]", g.id, g.shots, g.hits, g.damage);
        ++n;
      }
    }
    std::string wr;
    {
      std::vector<std::pair<uint32_t, Totals::Writer>> v(g_t.writers.begin(), g_t.writers.end());
      std::sort(v.begin(), v.end(), [](const auto &a, const auto &b) { return a.second.moved > b.second.moved; });
      for (size_t i = 0; i < v.size() && i < 8; ++i)
      {
        wr += fmt::format(" [{:08X}: {} sets {} adds, moved {:.0f} deg, {} over 3 deg]", v[i].first, v[i].second.sets,
                          v[i].second.adds, v[i].second.moved, v[i].second.big);
      }
    }
    REXLOG_INFO("pdz_jo_hit_log: SUMMARY ({}) at {:.1f} s, level {}, difficulty {}: shots at Jo {}, hits {} ({:.1f} %), "
                "damage {:.2f} ({:.2f} a minute since the first shot at {:.1f} s); Jo's health {:.2f} (at the first shot "
                "{:.2f}: it would last {}); her place ({:.0f} {:.0f} {:.0f}) | aim: {} aim points read, {:.0f} cm from her "
                "on average; facing off {:.1f} deg on average, {} shots facing over 15 deg away | guards fighting her (holding her as "
                "target and shot at her in the last 5 s): {:.0f} guard-seconds, {:.0f} AI updates a guard-second; yaw reversals over 6 deg {} ({:.2f} a "
                "guard-second), swinging episodes {} ({:.2f} a guard-minute), widest {:.0f} deg, mean turn {:.2f} deg an "
                "update | yaw writers (caller: count, how far they moved him){} | shots by NPCs at others {} |{}",
                why, now, Ptr(game) ? int(base[game + 25]) : -1, difficulty, g_t.shots, g_t.hits,
                g_t.shots ? 100.0 * g_t.hits / g_t.shots : 0.0, g_t.damage, per_min, g_t.first_shot, health, g_health0,
                last, jp[0], jp[1], jp[2], g_t.aim_read, g_t.aim_read ? g_t.miss_sum / g_t.aim_read : 0.0,
                g_t.face_n ? g_t.face_sum / g_t.face_n : 0.0, g_t.face_over15, g_t.fight_s,
                g_t.fight_s > 0.0 ? g_t.ticks / g_t.fight_s : 0.0, g_t.revs, g_t.fight_s > 0.0 ? g_t.revs / g_t.fight_s : 0.0,
                g_t.episodes, g_t.fight_s > 0.0 ? g_t.episodes * 60.0 / g_t.fight_s : 0.0, g_t.widest,
                g_t.ticks ? g_t.turn_sum / g_t.ticks : 0.0, wr, g_t.shots_other, per);
  }

  // The clock, a level start, and the summary. Under g_lock.
  void Clock(const uint8_t *base)
  {
    const float now = LoadF(base, kAiClock);
    const uint32_t game = Load32(base, kGame);
    const uint32_t level = Ptr(game) ? base[game + 25] : 0xFFu;
    if (!Finite(now))
    {
      return;
    }
    if (now + 0.5f < g_clock || level != g_level)
    {
      if (g_t.shots || g_t.fight_s > 0.0)
      {
        Summary(base, g_clock, "level ends");
      }
      g_guards.clear();
      g_t = Totals();
      g_health0 = -1.0f;
      g_next_summary = now + 10.0f;
      g_level = level;
    }
    g_clock = now;
    if (now >= g_next_summary)
    {
      g_next_summary = now + 10.0f;
      if (g_t.shots || g_t.fight_s > 0.0)
      {
        Summary(base, now, "every 10 s");
      }
    }
  }
} // namespace

namespace pdz::aim120
{
  void NpcShotBefore(uint8_t *base, uint32_t npc, uint32_t weapon)
  {
    (void)weapon;
    if (!On() || !Ptr(npc))
    {
      return;
    }
    const uint32_t t = Load32(base, npc + 0x316C);
    g_shot_npc = npc;
    g_shot_target = t ? t : Load32(base, npc + 0x36D0);
  }

  void NpcShotAfter(uint8_t *base, uint32_t npc, uint32_t weapon, uint32_t sp)
  {
    if (!On() || !Ptr(npc) || npc != g_shot_npc)
    {
      return;
    }
    const uint32_t target = g_shot_target;
    g_shot_npc = 0;
    std::lock_guard<std::mutex> hold(g_lock);
    Clock(base);
    const uint32_t jo = Load32(base, kPlayerOne);
    if (!Ptr(jo) || target != jo || Load32(base, npc + kClass) == 2)
    {
      ++g_t.shots_other;
      return;
    }
    const float now = g_clock;
    Guard &g = G(base, npc);
    ++g.shots;
    g.last_shot = now;
    ++g_t.shots;
    if (g_t.first_shot < 0.0f)
    {
      g_t.first_shot = now;
      g_health0 = JoHealth(base, jo);
    }
    float me[3], her[3];
    Pos(base, npc, me);
    Pos(base, jo, her);
    const float dx = her[0] - me[0], dz = her[2] - me[2];
    const float dist = std::sqrt(dx * dx + dz * dz + (her[1] - me[1]) * (her[1] - me[1]));
    const float yaw = LoadF(base, npc + kYaw);
    const float face = std::fabs(Wrap(yaw - std::atan2(dx, dz) * 57.29578f));
    if (Finite(yaw))
    {
      g_t.face_sum += face;
      ++g_t.face_n;
      g_t.face_over15 += face > 15.0f ? 1 : 0;
    }
    // The routine's frame (stwu r1, -0xD0): origin at +0x68, aim point at +0x58.
    const uint32_t frame = sp - 0xD0;
    float origin[3], aim[3];
    for (int i = 0; i < 3; ++i)
    {
      origin[i] = LoadF(base, frame + 0x68 + 4 * i);
      aim[i] = LoadF(base, frame + 0x58 + 4 * i);
    }
    const auto d3 = [](const float a[3], const float b[3]) {
      return std::sqrt((a[0] - b[0]) * (a[0] - b[0]) + (a[1] - b[1]) * (a[1] - b[1]) + (a[2] - b[2]) * (a[2] - b[2]));
    };
    std::string aimed = "aim point not read";
    if (Finite(origin[0]) && Finite(aim[0]) && d3(origin, me) < 400.0f && d3(aim, her) < 1500.0f)
    {
      const float side = std::sqrt((aim[0] - her[0]) * (aim[0] - her[0]) + (aim[2] - her[2]) * (aim[2] - her[2]));
      ++g_t.aim_read;
      g_t.miss_sum += side;
      aimed = fmt::format("aimed {:.0f} cm beside her, {:.0f} cm above her feet", side, aim[1] - her[1]);
    }
    const uint32_t record = Ptr(weapon) ? Load32(base, weapon + kWeaponRecord) : 0;
    REXLOG_INFO("pdz_jo_hit_log: SHOT at {:.2f} s: {} (gun type {}) at Jo, {:.1f} m: {}; he faces {:.0f} deg away from her "
                "(shot {} of his, {} at her in all)",
                now, g.id, Ptr(record) ? int(Load32(base, record + kRecordGunType)) : -1, dist / 100.0f, aimed, face,
                g.shots, g_t.shots);
  }

  void GunShot(uint8_t *base, uint32_t weapon)
  {
    if (!On() || !Ptr(weapon))
    {
      return;
    }
    const uint32_t npc = Load32(base, weapon + 0x5C);
    const uint32_t jo = Load32(base, kPlayerOne);
    if (!Ptr(npc) || !Ptr(jo) || npc == jo || Load32(base, npc + kClass) != 33)
    {
      return;
    }
    const uint32_t t = Load32(base, npc + 0x316C);
    std::lock_guard<std::mutex> hold(g_lock);
    Clock(base);
    if ((t ? t : Load32(base, npc + 0x3154)) != jo)
    {
      ++g_t.shots_other;
      return;
    }
    const float now = g_clock;
    Guard &g = G(base, npc);
    ++g.shots;
    g.last_shot = now;
    ++g_t.shots;
    if (g_t.first_shot < 0.0f)
    {
      g_t.first_shot = now;
      g_health0 = JoHealth(base, jo);
    }
    float me[3], her[3];
    Pos(base, npc, me);
    Pos(base, jo, her);
    const float dx = her[0] - me[0], dz = her[2] - me[2];
    const float dist = std::sqrt(dx * dx + dz * dz + (her[1] - me[1]) * (her[1] - me[1]));
    const float yaw = LoadF(base, npc + kYaw);
    const float face = std::fabs(Wrap(yaw - std::atan2(dx, dz) * 57.29578f));
    if (Finite(yaw))
    {
      g_t.face_sum += face;
      ++g_t.face_n;
      g_t.face_over15 += face > 15.0f ? 1 : 0;
    }
    const uint32_t record = Load32(base, weapon + kWeaponRecord);
    // Rare's per-shot hit counter (sub_8214CF98): char +0x1828 grows by a share per shot, scaled by 1 / +0x1820 /
    // +0x1824 and falling from the gun's range +0x3C0 to +0x3C4 (cm); a hit is forced when it reaches 1.
    REXLOG_INFO("pdz_jo_hit_log: SHOT at {:.2f} s: {} (gun type {}, ranges {:.0f} / {:.0f} m) at Jo, {:.1f} m; he faces "
                "{:.0f} deg away from her; Rare's hit counter {:.2f} (his +0x1820 {:.2f}, +0x1824 {:.2f}) (shot {} of "
                "his, {} at her in all)",
                now, g.id, Ptr(record) ? int(Load32(base, record + kRecordGunType)) : -1,
                Ptr(record) ? LoadF(base, record + 0x3C0) / 100.0f : 0.0f, Ptr(record) ? LoadF(base, record + 0x3C4) / 100.0f : 0.0f,
                dist / 100.0f, face, LoadF(base, npc + 0x1828), LoadF(base, npc + 0x1820), LoadF(base, npc + 0x1824),
                g.shots, g_t.shots);
  }
  void PlayerDamage(uint8_t *base, uint32_t victim, uint32_t attacker, double damage)
  {
    if (!On() || damage <= 0.0)
    {
      return;
    }
    std::lock_guard<std::mutex> hold(g_lock);
    Clock(base);
    const uint32_t jo = Load32(base, kPlayerOne);
    if (!Ptr(jo) || victim != jo)
    {
      return;
    }
    uint32_t who = attacker;
    if (Ptr(who) && Load32(base, who + kClass) != 33 && Ptr(Load32(base, who + 0x5C)))
    {
      who = Load32(base, who + 0x5C); // a weapon: its owner
    }
    const bool npc = Ptr(who) && Load32(base, who + kClass) == 33;
    float me[3] = {}, her[3];
    Pos(base, jo, her);
    if (npc)
    {
      Pos(base, who, me);
    }
    const float dist = npc ? std::sqrt((her[0] - me[0]) * (her[0] - me[0]) + (her[1] - me[1]) * (her[1] - me[1]) +
                                       (her[2] - me[2]) * (her[2] - me[2]))
                           : 0.0f;
    g_t.damage += damage;
    std::string by = npc ? Id(base, who) : fmt::format("{:08X} (not an NPC)", attacker);
    if (npc)
    {
      Guard &g = G(base, who);
      ++g.hits;
      g.damage += damage;
      ++g_t.hits;
      by = fmt::format("{} ({:.1f} m, his shot {} {:.2f} s ago)", g.id, dist / 100.0f, g.shots,
                       g.last_shot > -1e8f ? g_clock - g.last_shot : -1.0f);
    }
    REXLOG_INFO("pdz_jo_hit_log: HIT at {:.2f} s: Jo takes {:.2f} from {}; her health {:.2f} before it; {} hits, {:.2f} damage "
                "in all",
                g_clock, damage, by, JoHealth(base, jo), g_t.hits, g_t.damage);
  }

  void CharacterTick(uint8_t *base, uint32_t c)
  {
    if (!On() || !Ptr(c))
    {
      return;
    }
    const uint32_t jo = Load32(base, kPlayerOne);
    if (!Ptr(jo) || c == jo || Load32(base, c + kClass) != 33)
    {
      return;
    }
    const uint32_t t = Load32(base, c + 0x316C);
    if ((t ? t : Load32(base, c + 0x3154)) != jo)
    {
      return;
    }
    const float yaw = LoadF(base, c + kYaw);
    if (!Finite(yaw))
    {
      return;
    }
    std::lock_guard<std::mutex> hold(g_lock);
    Clock(base);
    const float now = g_clock;
    Guard &g = G(base, c);
    if (!(g.last_shot > -1e8f && now >= g.last_shot && now - g.last_shot <= 5.0f))
    {
      g.yaw_seen = false; // not engaged: Rare holds Jo as every calm guard's target from the level start
      g.last_tick = now;
      return;
    }
    const float dt = now - g.last_tick;
    g.last_tick = now;
    if (dt > 0.0f && dt < 0.25f)
    {
      g_t.fight_s += dt;
    }
    ++g_t.ticks;
    if (g.yaw_seen)
    {
      g_t.turn_sum += std::fabs(Wrap(yaw - g.prev_yaw));
    }
    g.prev_yaw = yaw;
    if (!g.yaw_seen)
    {
      g.yaw_seen = true;
      g.yaw_ext = yaw;
      g.yaw_dir = 0;
      return;
    }
    float turn = Wrap(yaw - g.yaw_ext);
    if (g.yaw_dir == 0)
    {
      if (std::fabs(turn) > kSwingDeg)
      {
        g.yaw_dir = turn > 0.0f ? 1 : -1;
        g.yaw_ext = yaw;
      }
      return;
    }
    if (turn * float(g.yaw_dir) >= 0.0f)
    {
      g.yaw_ext = yaw;
      return;
    }
    if (turn * float(g.yaw_dir) > -kSwingDeg)
    {
      return;
    }
    const float span = std::fabs(turn);
    g.yaw_dir = -g.yaw_dir;
    g.yaw_ext = yaw;
    ++g_t.revs;
    g_t.widest = std::max(g_t.widest, span);
    if (now >= g.rev_at && now - g.rev_at <= kSwingGapS)
    {
      ++g.revs;
    }
    else
    {
      g.revs = 1;
      g.rev_from = now;
      g.episode = false;
    }
    g.rev_at = now;
    if (!g.episode && g.revs >= kSwingRevs && now - g.rev_from >= kSwingS)
    {
      g.episode = true;
      ++g_t.episodes;
    }
  }
} // namespace pdz::aim120

namespace
{
  // A yaw write to a living NPC whose target is Jo: counted by its caller. Not under the clock (no summary from here).
  void YawWrite(uint8_t *base, uint32_t c, uint32_t lr, float before, bool add)
  {
    const uint32_t jo = Load32(base, kPlayerOne);
    if (!Ptr(c) || !Ptr(jo) || c == jo || Load32(base, c + kClass) != 33)
    {
      return;
    }
    const uint32_t t = Load32(base, c + 0x316C);
    if ((t ? t : Load32(base, c + 0x3154)) != jo)
    {
      return;
    }
    const float after = LoadF(base, c + kYaw);
    if (!Finite(before) || !Finite(after))
    {
      return;
    }
    const float moved = std::fabs(Wrap(after - before));
    std::lock_guard<std::mutex> hold(g_lock);
    if (g_t.writers.size() > 64 && !g_t.writers.count(lr))
    {
      return;
    }
    Totals::Writer &w = g_t.writers[lr];
    (add ? w.adds : w.sets) += 1;
    w.moved += moved;
    w.big += moved > 3.0f ? 1 : 0;
  }
} // namespace

// sub_8254E0F0 (character vtable +0x20): yaw = f1 (degrees). sub_8254E100 (+0x28): yaw += f1. Log only.
REX_EXTERN(__imp__sub_8254E0F0);
REX_HOOK_RAW(sub_8254E0F0)
{
  if (!On())
  {
    __imp__sub_8254E0F0(ctx, base);
    return;
  }
  const uint32_t c = ctx.r3.u32, lr = uint32_t(ctx.lr);
  const float before = Ptr(c) ? LoadF(base, c + kYaw) : 0.0f;
  __imp__sub_8254E0F0(ctx, base);
  YawWrite(base, c, lr, before, false);
}
REX_EXTERN(__imp__sub_8254E100);
REX_HOOK_RAW(sub_8254E100)
{
  if (!On())
  {
    __imp__sub_8254E100(ctx, base);
    return;
  }
  const uint32_t c = ctx.r3.u32, lr = uint32_t(ctx.lr);
  const float before = Ptr(c) ? LoadF(base, c + kYaw) : 0.0f;
  __imp__sub_8254E100(ctx, base);
  YawWrite(base, c, lr, before, true);
}