// Player cover diagnostic for the Title Update 3 build (pdz_cover_log, default off). Logging only: every hook calls
// Rare's function unchanged, and costs one cvar read while the switch is off. Lines start with "pdz_cover: ".
//
// TU3 addresses (all read from generated/default, 14 Nov):
//   sub_824FE550(player, f1 = dt)   the cover module's per-frame update (launch 824F11C0). State at player +0x1CA4
//                                   (0 none, 1 entering, 2 transition, 3 in cover, 4 popped out, 5 exiting), the
//                                   previous state at +0x1CA8; a state change is queued in the global 82E3C348 (-1 =
//                                   none) and applied at the end. Out of cover it calls the detection sub_82501E88
//                                   (after the cooldown at +0x1D60 has run out); in states 1-4 it calls sub_825001F0.
//   sub_82501E88(player, f1 = dt)   detection wrapper (launch 824F4AB8): runs the scanner, commits the candidate to
//                                   +0x1D4C (type) / +0x1D50 (point) / +0x1D5C (wall yaw), latches where the scan
//                                   was made (+0x18EC position, +0x18F8 yaw) and raises the prompt (context action
//                                   0x7A through sub_825AAB48) while the player is still near that place and facing.
//   sub_82501A00(player, f1, f2, r5 = &position, r6 = &yaw, r8 = &frame stamp, r9 = &type out, r10 = &point out,
//                stack +84 = &wall yaw out) -> r3 = 1 on the frame a scan cycle completes.
//                                   The scanner: ONE step a frame. 3 probes (index at +0x183C), each up to 5 steps
//                                   (index at +0x1840) done by sub_82502188; probe slots of 48 bytes at +0x1848.
//                                   Also called from sub_825001F0 (return address 82501270) while in cover: a rescan
//                                   from the latched place, whose result can change the cover type / side.
//   sub_82501990(type) -> priority  the choice among the three probes: highest priority, ties to the lower probe.
//   sub_825AAAD0(player, target)    the context action callback (launch 8259B2B8): with target == player and a
//                                   candidate it tail-calls the entry with (&player+0x1D50, f1 = +0x1D5C, r6 = type).
//   sub_824FD738(player, r4 = &point, f1 = wall yaw, r6 = type)   enters cover (launch 824F03A8).
//   sub_82504F68(type, r4, &anim) -> side   the side / animation the entry uses (see PredictSide).
//   sub_824FE9D0(player, r4 = instant)      forces the player out of cover (launch 824F1610).
//
// Cover type bits (set by sub_82503818, the ray pass of a probe):
//   0x04  the wall is low: the rays at the upper height pass over it (fire over the top)
//   0x01 / 0x02  lower-height pass: the wall ends (open side) on side A / side B
//   0x08 / 0x10  upper-height pass: the wall ends on side A / side B
// Which of A / B is left or right was not decoded.
//
// The player's cover fixes (single player, player 1 only; each its own switch, on by default; with a switch off the
// code path is Rare's unchanged). Their actions are logged as "pdz_cover: C1 / C2 / C3 / C5" lines, rate-limited,
// whatever pdz_cover_log says.
//   C0  (no switch) the log printed the look yaw (+0x1670, already in degrees) times 57.3. Printed as it is now.
//   C1  pdz_cover_fix_faced: Rare's choice loop (82501E04-82501E68) takes the best-ranked of the three probes. When
//       that probe's wall is more than kFacedTieDeg further from the scan's yaw than the best-faced probe's wall,
//       the hook takes the best-faced one (ties in that band go to Rare's rank) and rewrites the three outputs
//       (type, point, wall yaw) exactly as the loop would have. Both callers (the out-of-cover detection and the
//       in-cover rescan, which scans from the same latched place and yaw), so the rescan agrees with the entry.
//   C2  pdz_cover_fix_fan: the scanner runs one probe step a call. For the detection caller the hook calls it again
//       until the cycle completes (at most 15 calls = 3 probes x 5 steps), so every frame ends with a full scan
//       from where the player is now.
//   C3  pdz_cover_fix_stale: Rare forgives one empty scan (grace +0x1900) and then scans again FROM THE LATCHED
//       PLACE AND YAW (sub_82501E88, 82501ED0-82501F44). That re-finds the old wall and clears the grace, the next
//       scan from where the player stands finds nothing and sets it again: the old candidate stays for as long as
//       the player stays within 130 cm and 40 degrees of where it was found. The fix: an empty completed scan drops
//       the candidate at once (the same writes as Rare's own drop at 82502008), the grace is never left set, a
//       candidate found more than kStaleCm from where the scan stands now makes the scan run (restarted here when
//       its cycle began elsewhere) to the end in this frame, and the press is refused for a candidate found more
//       than kStaleCm away, older than kStaleFrames frames, or newer than an empty scan.
//   C5  pdz_cover_fix_fresh_push: the in-cover side switch (825007E4-825009C8) flips to the other end when the
//       sideways input is beyond -0.9 / +0.9, also when that push was held from before the entry. The switch only
//       runs for sub_82504E50's answer 0 or 1 (r26, read only by the switch); the hook answers 2 (no switch) while
//       the input has not been back inside kCentreBand since the entry began or since the last change of end.
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <unordered_map>

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/ppc/func.h>
#include "hook_profile.h" // pdz_hook_profile (must follow <rex/hook.h>)

#include "../game_cvars.h"

REX_EXTERN(__imp__sub_824FE550);
REX_EXTERN(__imp__sub_82501A00);
REX_EXTERN(__imp__sub_825AAAD0);
REX_EXTERN(__imp__sub_824FD738);
REX_EXTERN(__imp__sub_824FE9D0);
REX_EXTERN(__imp__sub_82504E50);

namespace
{
  uint32_t LoadBe32(const uint8_t *base, uint32_t address)
  {
    const uint8_t *p = base + address;
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
  }

  float LoadBeFloat(const uint8_t *base, uint32_t address)
  {
    const uint32_t bits = LoadBe32(base, address);
    float value;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
  }

  struct Vec3
  {
    float x = 0.0f, y = 0.0f, z = 0.0f;
  };

  Vec3 LoadVec(const uint8_t *base, uint32_t address)
  {
    return {LoadBeFloat(base, address), LoadBeFloat(base, address + 4), LoadBeFloat(base, address + 8)};
  }

  float Dist2d(const Vec3 &a, const Vec3 &b)
  {
    return std::sqrt((a.x - b.x) * (a.x - b.x) + (a.z - b.z) * (a.z - b.z));
  }

  constexpr float kRadToDeg = 57.2957795f;

  // Yaw of a direction the way Rare's sub_824FF068 does it: acos(z / |xz|), negative for x < 0.
  float YawDeg(const Vec3 &v)
  {
    const float len = std::sqrt(v.x * v.x + v.z * v.z);
    if (len < 1.0e-6f)
    {
      return 0.0f;
    }
    float c = v.z / len;
    c = c > 1.0f ? 1.0f : (c < -1.0f ? -1.0f : c);
    const float a = std::acos(c) * kRadToDeg;
    return v.x < 0.0f ? -a : a;
  }

  constexpr uint32_t kPlayerOne = 0x82E3C3E4; // player 1's character

  // Player fields (cm, radians unless said).
  constexpr uint32_t kPosition = 0x30;
  constexpr uint32_t kViewYaw = 0x1670; // degrees (the scan's yaw from sub_82519A90 is the same angle in radians)
  constexpr uint32_t kEntryFlagFloat = 0x1684; // +5764: < 0 picks the second variant in sub_82504F68 (meaning unknown)
  // Scanner.
  constexpr uint32_t kScanProbe = 0x183C; // +6204: probe being run, 0..2
  constexpr uint32_t kScanStep = 0x1840;  // +6208: step of that probe, 0..4
  constexpr uint32_t kScanSlots = 0x1848; // +6216: 3 slots of 48 bytes
  constexpr uint32_t kSlotSize = 48;
  constexpr uint32_t kSlotPoint = 0;   // vec3: the cover point (where the player would stand)
  constexpr uint32_t kSlotHit = 12;    // vec3: the first ray's hit, pulled back off the wall, at foot height
  constexpr uint32_t kSlotNormal = 24; // vec3: the wall direction; its yaw is the candidate's wall yaw
  constexpr uint32_t kSlotType = 36;   // u32: cover type bits
  constexpr uint32_t kSlotOk = 40;     // u8: the last step run passed
  constexpr uint32_t kSlotWidth = 44;  // float from the edge search sub_82504780 (exact meaning not decoded)
  constexpr uint32_t kScanOrigin = 0x18D8;  // +6360 vec3: where the running scan was started
  constexpr uint32_t kScanYaw = 0x18E4;     // +6372: the yaw it was started with
  constexpr uint32_t kLatchOrigin = 0x18EC; // +6380 vec3: where the committed candidate's scan was made
  constexpr uint32_t kLatchYaw = 0x18F8;    // +6392
  constexpr uint32_t kLatchGrace = 0x1900;  // +6400 u8: one empty scan is forgiven
  // Cover module.
  constexpr uint32_t kCoverState = 0x1CA4;     // +7332
  constexpr uint32_t kCoverTarget = 0x1CAC;    // +7340: target state
  constexpr uint32_t kCoverType = 0x1CB0;      // +7344: the type entered with (the in-cover rescan rewrites it)
  constexpr uint32_t kCoverSide = 0x1CB4;      // +7348: the side in use (one type bit, from sub_82504F68)
  constexpr uint32_t kRescanType = 0x1CC0;     // +7360: the in-cover rescan's type
  constexpr uint32_t kCoverWallYaw = 0x1CE8;   // +7400
  constexpr uint32_t kCoverSnap = 0x1CF0;      // +7408 vec3: the position the cover animation is anchored at
  constexpr uint32_t kCoverEntrySnap = 0x1D14; // +7444 vec3: the entry's snap position
  constexpr uint32_t kCoverBusy = 0x1D2D;      // +7469
  constexpr uint32_t kCandidateType = 0x1D4C;  // +7500
  constexpr uint32_t kCandidatePoint = 0x1D50; // +7504
  constexpr uint32_t kCandidateYaw = 0x1D5C;   // +7516

  constexpr uint32_t kNotInCover = 0, kEntering = 1, kTransition = 2, kInCover = 3, kPoppedOut = 4, kExiting = 5;

  constexpr uint32_t kReturnFromDetection = 0x82501F74; // sub_82501E88's call of the scanner
  constexpr uint32_t kReturnFromInCover = 0x82501270;   // sub_825001F0's call (rescan while in cover)

  using Clock = std::chrono::steady_clock;

  bool Enabled()
  {
    return REXCVAR_GET(pdz_cover_log);
  }

  double Ms(Clock::time_point from, Clock::time_point to)
  {
    return std::chrono::duration<double, std::milli>(to - from).count();
  }

  double Stamp(Clock::time_point now)
  {
    static const Clock::time_point start = now;
    return std::chrono::duration<double>(now - start).count();
  }

  // Rare's sub_82501990: the rank the scanner compares the three probes by.
  int Priority(uint32_t type)
  {
    int p = type ? 1 : 0;
    if (type & 0x04)
    {
      ++p;
    }
    if ((type & 0x12) == 0x12 || (type & 0x09) == 0x09)
    {
      ++p;
    }
    if ((type & 0x12) && (type & 0x09))
    {
      p += 2;
    }
    return p;
  }

  // Rare's sub_82504F68(type, variant): the side bit the entry uses. variant = player +0x1684 < 0.
  uint32_t PredictSide(uint32_t type, bool variant)
  {
    if (!type)
    {
      return 0;
    }
    if (type & 0x08)
    {
      return !variant ? 0x08 : (type & 0x01) ? 0x01 : (type & 0x02) ? 0x02 : 0x08;
    }
    if (type & 0x10)
    {
      return !variant ? 0x10 : (type & 0x02) ? 0x02 : (type & 0x01) ? 0x01 : 0x10;
    }
    if (type & 0x04)
    {
      return 0x04;
    }
    return (type & 0x01) ? 0x01 : (type & 0x02) ? 0x02 : 0;
  }

  std::string TypeName(uint32_t type)
  {
    if (!type)
    {
      return "none";
    }
    std::string s;
    const auto add = [&](uint32_t bit, const char *name) {
      if (type & bit)
      {
        s += s.empty() ? "" : "+";
        s += name;
      }
    };
    add(0x04, "LOW(over the top)");
    add(0x01, "low-pass side A");
    add(0x02, "low-pass side B");
    add(0x08, "HIGH side A");
    add(0x10, "HIGH side B");
    if (type & ~0x1Fu)
    {
      s += "+other";
    }
    return s;
  }

  const char *HeightName(uint32_t type)
  {
    return (type & 0x04) ? "low" : (type & 0x18) ? "high" : type ? "undecided (no top, no upper side)" : "-";
  }

  const char *ProbeName(int probe)
  {
    return probe == 0 ? "ahead" : probe == 1 ? "diagonal 1" : "diagonal 2";
  }

  const char *StepName(uint32_t step)
  {
    switch (step)
    {
    case 0:
      return "ray at the lower height";
    case 1:
      return "ray at the upper height";
    case 2:
      return "edge search, lower height (no wall found when the type is 0)";
    case 3:
      return "edge search, upper height";
    case 4:
      return "final checks (room to stand, ground, clear line from the player, wall height, width)";
    default:
      return "?";
    }
  }

  struct ProbeResult
  {
    bool valid = false;
    uint32_t type = 0;
    bool ok = false;
    uint32_t steps = 0; // steps run; when !ok the last one (steps - 1) is the one that failed
    Vec3 point, hit, normal;
    float width = 0.0f;

    bool Differs(const ProbeResult &o) const
    {
      if (valid != o.valid || type != o.type || ok != o.ok || steps != o.steps)
      {
        return true;
      }
      if (!ok)
      {
        return false;
      }
      return Dist2d(point, o.point) > 10.0f || std::fabs(point.y - o.point.y) > 10.0f ||
             std::fabs(YawDeg(normal) - YawDeg(o.normal)) > 5.0f;
    }
  };

  struct ScanLog
  {
    ProbeResult work[3]; // the cycle being run
    ProbeResult last[3]; // the last completed cycle (what a press is answered from)
    ProbeResult logged[3];
    bool have_last = false, have_logged = false;
    uint32_t last_type = 0, logged_type = 0;
    int last_chosen = -1;
    int rare_chosen = -1; // Rare's rank's choice; differs from last_chosen when C1 took another probe
    Vec3 last_point, last_origin;
    float last_yaw_deg = 0.0f, last_origin_yaw_deg = 0.0f;
    uint32_t last_caller = 0;
    uint64_t cycles = 0, restarts = 0;
    Clock::time_point last_time;
  };

  struct PlayerLog
  {
    bool valid = false;
    uint32_t state = 0, target = 0, type = 0, side = 0, candidate = 0, rescan = 0;
    uint8_t busy = 0;
    Clock::time_point state_since, enter_request, exit_request, cover_since;
    bool enter_pending = false, exit_pending = false;
    Vec3 press_position, press_point;
    ScanLog scan;
  };
  std::unordered_map<uint32_t, PlayerLog> g_logs;

  std::string Tag(const uint8_t *base, uint32_t player)
  {
    return player == LoadBe32(base, kPlayerOne) ? std::string("P1") : fmt::format("{:08X}", player);
  }

  std::string V(const Vec3 &v)
  {
    return fmt::format("({:.0f} {:.0f} {:.0f})", v.x, v.y, v.z);
  }

  // The scanner's numbers, read from the game's data once (the values are not in the code).
  void LogConstantsOnce(const uint8_t *base)
  {
    static bool done = false;
    if (done)
    {
      return;
    }
    done = true;
    const Vec3 d0 = LoadVec(base, 0x82073C6C);
    const float d1x = LoadBeFloat(base, 0x820756F0), d2x = LoadBeFloat(base, 0x820756E8);
    const float dz = LoadBeFloat(base, 0x820DFD34);
    REXLOG_INFO("pdz_cover: scanner constants: probe directions in the player's frame (x, z; the game normalises "
                "them): 0 ahead ({:.2f} {:.2f}), 1 ({:.2f} {:.2f}), 2 ({:.2f} {:.2f}); ray length {:.0f} cm; ray "
                "heights above the scan position: lower {:.0f}, upper {:.0f} cm; point held {:.0f} cm off the wall; "
                "a scan restarts when the player has moved more than {:.0f} cm; the prompt needs the player within "
                "{:.0f} cm and {:.0f} deg of where the scan was made; entry stands {:.0f} cm off the point",
                d0.x, d0.z, d1x, dz, d2x, dz, LoadBeFloat(base, 0x820EBBE8), LoadBeFloat(base, 0x820883E4),
                LoadBeFloat(base, 0x8208A3D4), LoadBeFloat(base, 0x820EBBE4),
                std::sqrt(std::fabs(LoadBeFloat(base, 0x820EBBB0))),
                std::sqrt(std::fabs(LoadBeFloat(base, 0x820EBDE4))), LoadBeFloat(base, 0x820A77A0) * kRadToDeg,
                LoadBeFloat(base, 0x8208C4E8));
  }

  ProbeResult ReadSlot(const uint8_t *base, uint32_t player, uint32_t probe, uint32_t steps)
  {
    const uint32_t slot = player + kScanSlots + probe * kSlotSize;
    ProbeResult r;
    r.valid = true;
    r.type = LoadBe32(base, slot + kSlotType);
    r.ok = base[slot + kSlotOk] != 0;
    r.steps = steps;
    r.point = LoadVec(base, slot + kSlotPoint);
    r.hit = LoadVec(base, slot + kSlotHit);
    r.normal = LoadVec(base, slot + kSlotNormal);
    r.width = LoadBeFloat(base, slot + kSlotWidth);
    return r;
  }

  void LogProbe(const std::string &tag, double stamp, int index, const ProbeResult &r, const Vec3 &origin)
  {
    if (!r.valid)
    {
      REXLOG_INFO("pdz_cover: [{:9.3f}] {}   probe {} ({}): not recorded in this cycle", stamp, tag, index,
                  ProbeName(index));
      return;
    }
    if (!r.ok)
    {
      REXLOG_INFO("pdz_cover: [{:9.3f}] {}   probe {} ({}): REJECTED at step {} ({}), type then {:#x}; first hit {} "
                  "wall yaw {:.0f} deg",
                  stamp, tag, index, ProbeName(index), r.steps - 1, StepName(r.steps - 1), r.type, V(r.hit),
                  YawDeg(r.normal));
      return;
    }
    REXLOG_INFO("pdz_cover: [{:9.3f}] {}   probe {} ({}): FOUND type {:#x} [{}] height {} priority {}; point {} "
                "({:.0f} cm away, {:+.0f} cm up), wall yaw {:.0f} deg, first hit {}, edge search {:.0f}",
                stamp, tag, index, ProbeName(index), r.type, TypeName(r.type), HeightName(r.type), Priority(r.type),
                V(r.point), Dist2d(r.point, origin), r.point.y - origin.y, YawDeg(r.normal), V(r.hit), r.width);
  }

  // The last completed scan cycle, in full (candidates, choice and the reason).
  void LogScan(const uint8_t *base, uint32_t player, PlayerLog &log, Clock::time_point now, const char *why)
  {
    ScanLog &scan = log.scan;
    const std::string tag = Tag(base, player);
    const double stamp = Stamp(now);
    if (!scan.have_last)
    {
      REXLOG_INFO("pdz_cover: [{:9.3f}] {} scan ({}): no completed scan cycle yet", stamp, tag, why);
      return;
    }
    const char *kind = scan.last_caller == kReturnFromInCover     ? "in-cover rescan"
                       : scan.last_caller == kReturnFromDetection ? "out of cover"
                                                                  : "other caller";
    if (scan.last_chosen >= 0)
    {
      REXLOG_INFO("pdz_cover: [{:9.3f}] {} scan #{} ({}, {}; {:.0f} ms old) from {} yaw {:.0f} deg: CHOSE probe {} "
                  "({}) type {:#x} [{}] height {}, point {} wall yaw {:.0f} deg, {:.0f} cm from the scan position; "
                  "reason: highest priority {} (Rare's rank: +1 any, +1 low top, +1 one side open at both heights, "
                  "+2 side A and side B both open), ties go to the lower probe{}; {} restarts since the last line",
                  stamp, tag, scan.cycles, kind, why, Ms(scan.last_time, now), V(scan.last_origin),
                  scan.last_origin_yaw_deg, scan.last_chosen, ProbeName(scan.last_chosen), scan.last_type,
                  TypeName(scan.last_type), HeightName(scan.last_type), V(scan.last_point), scan.last_yaw_deg,
                  Dist2d(scan.last_point, scan.last_origin), Priority(scan.last_type),
                  scan.rare_chosen >= 0 && scan.rare_chosen != scan.last_chosen
                      ? fmt::format(" (C1: the faced wall was taken; Rare's rank would take probe {})",
                                    scan.rare_chosen)
                      : std::string(),
                  scan.restarts);
    }
    else
    {
      REXLOG_INFO("pdz_cover: [{:9.3f}] {} scan #{} ({}, {}; {:.0f} ms old) from {} yaw {:.0f} deg: NO COVER (no "
                  "probe passed; scanner's type {:#x}); {} restarts since the last line",
                  stamp, tag, scan.cycles, kind, why, Ms(scan.last_time, now), V(scan.last_origin),
                  scan.last_origin_yaw_deg, scan.last_type, scan.restarts);
    }
    for (int i = 0; i < 3; ++i)
    {
      LogProbe(tag, stamp, i, scan.last[i], scan.last_origin);
      scan.logged[i] = scan.last[i];
    }
    scan.restarts = 0;
    scan.logged_type = scan.last_type;
    scan.have_logged = true;
  }

  // One scanner call seen by the log: records each probe as it finishes, and a restart of the cycle.
  void TrackStep(const uint8_t *base, uint32_t player, uint32_t probe_before, uint32_t step_before, bool complete)
  {
    ScanLog &scan = g_logs[player].scan;
    const uint32_t probe_after = LoadBe32(base, player + kScanProbe);
    const uint32_t step_after = LoadBe32(base, player + kScanStep);
    int finished = -1;
    if (complete)
    {
      finished = probe_before <= 2 ? int(probe_before) : -1; // the third probe ended
    }
    else if (probe_after == probe_before + 1 && step_after == 0)
    {
      finished = int(probe_before);
    }
    else if (!(probe_after == probe_before && step_after == step_before + 1))
    {
      // The scanner threw the cycle away (the player moved too far from where it started, or no scan for 20 frames)
      // and started again at probe 0.
      ++scan.restarts;
      for (ProbeResult &r : scan.work)
      {
        r = ProbeResult{};
      }
    }
    if (finished >= 0 && finished <= 2)
    {
      scan.work[finished] = ReadSlot(base, player, uint32_t(finished), step_before + 1);
    }
  }

  // A completed scan cycle seen by the log. chosen = the probe the outputs now hold (-1 none), or -2 to replay
  // Rare's rule on the recorded slots.
  void TrackComplete(const uint8_t *base, uint32_t player, uint32_t caller, uint32_t type_out, uint32_t point_out,
                     uint32_t yaw_out, int chosen, Clock::time_point now)
  {
    LogConstantsOnce(base);
    PlayerLog &log = g_logs[player];
    ScanLog &scan = log.scan;
    ++scan.cycles;
    scan.last_caller = caller;
    scan.last_time = now;
    scan.last_origin = LoadVec(base, player + kScanOrigin);
    scan.last_origin_yaw_deg = LoadBeFloat(base, player + kScanYaw) * kRadToDeg;
    scan.last_type = type_out ? LoadBe32(base, type_out) : 0;
    scan.last_point = point_out ? LoadVec(base, point_out) : Vec3{};
    scan.last_yaw_deg = yaw_out ? LoadBeFloat(base, yaw_out) * kRadToDeg : 0.0f;
    // Which probe won: Rare's own rule, replayed on the recorded slots.
    scan.rare_chosen = -1;
    int best = 0;
    bool changed = !scan.have_logged || scan.last_type != scan.logged_type;
    for (int i = 0; i < 3; ++i)
    {
      scan.last[i] = scan.work[i];
      scan.work[i] = ProbeResult{};
      if (scan.last[i].valid && scan.last[i].ok && Priority(scan.last[i].type) > best)
      {
        best = Priority(scan.last[i].type);
        scan.rare_chosen = i;
      }
      changed = changed || scan.last[i].Differs(scan.logged[i]);
    }
    scan.last_chosen = chosen >= -1 ? chosen : scan.rare_chosen;
    scan.have_last = true;
    if (changed)
    {
      LogScan(base, player, log, now, "result changed");
    }
  }

  // ---- The player's cover fixes C1, C2, C3, C5 (see the top of the file) ----

  constexpr float kStaleCm = 40.0f;     // C3: a candidate found further than this from the scan position is old
  constexpr uint32_t kStaleFrames = 20; // C3: Rare's own age limit of a scan (the stamp test in sub_82501A00)
  constexpr float kFacedTieDeg = 20.0f; // C1: walls within this of the best-faced wall count as faced
  constexpr float kCentreBand = 0.5f;   // C5: an input inside +-this re-arms the side switch
  constexpr int kFanMaxCalls = 15;      // C2: 3 probes x 5 steps

  constexpr uint32_t kGameInfo = 0x82D6BFD8;     // -> +40 game type (0 single player, 1 co-op, 2 / 3 multiplayer)
  constexpr uint32_t kFrameCounter = 0x82D6C060; // 82D6BFD8 + 136: the frame counter the scanner stamps with
  constexpr uint32_t kZeroVector = 0x82073C48;   // the vector Rare's drop writes (r27 in sub_82501E88)
  constexpr uint32_t kNoYawValue = 0x820DFCA4;   // the float Rare's drop writes for the yaws (f30 there)
  constexpr uint32_t kTinyLength2 = 0x82073C2C;  // sub_824FF068: a shorter direction has no yaw
  constexpr uint32_t kSwitchLeft = 0x820EB7DC;   // -0.9: at side B, an input below this switches to side A
  constexpr uint32_t kSwitchRight = 0x8209A148;  // +0.9: at side A, an input above this switches to side B
  constexpr uint32_t kLatchHeight = 0x18FC;      // +6396: the latched scan's height argument
  constexpr uint32_t kInputBlock = 0x19A0;       // +6560 -> the input block
  constexpr uint32_t kInputStickPath = 0x6F6;    // input byte: 0 = the switch is fed by another control (825007AC)
  constexpr uint32_t kReturnFromSideQuery = 0x82500298; // sub_825001F0's call of sub_82504E50 (its answer is r26)

  bool Ptr(uint32_t a)
  {
    return a >= 0x10000u && a < 0xFFFF0000u;
  }

  void StoreBe32(uint8_t *base, uint32_t address, uint32_t value)
  {
    uint8_t *p = base + address;
    p[0] = uint8_t(value >> 24);
    p[1] = uint8_t(value >> 16);
    p[2] = uint8_t(value >> 8);
    p[3] = uint8_t(value);
  }

  void StoreBeFloat(uint8_t *base, uint32_t address, float value)
  {
    uint32_t bits;
    std::memcpy(&bits, &value, sizeof(bits));
    StoreBe32(base, address, bits);
  }

  // Three big-endian words from one guest address to another (what Rare's lwz / stw triples do).
  void CopyVec(uint8_t *base, uint32_t to, uint32_t from)
  {
    for (uint32_t k = 0; k < 12; k += 4)
    {
      StoreBe32(base, to + k, LoadBe32(base, from + k));
    }
  }

  bool SameVec(const Vec3 &a, const Vec3 &b)
  {
    return a.x == b.x && a.y == b.y && a.z == b.z;
  }

  // Player 1 in a single player game (not co-op, Counter-Operative, split screen or multiplayer).
  bool SoloPlayerOne(const uint8_t *base, uint32_t player)
  {
    if (player != LoadBe32(base, kPlayerOne))
    {
      return false;
    }
    const uint32_t game = LoadBe32(base, kGameInfo);
    return Ptr(game) && LoadBe32(base, game + 40) == 0;
  }

  bool AnyFix()
  {
    return REXCVAR_GET(pdz_cover_fix_faced) || REXCVAR_GET(pdz_cover_fix_fan) || REXCVAR_GET(pdz_cover_fix_stale) ||
           REXCVAR_GET(pdz_cover_fix_fresh_push);
  }

  // |a - b| in degrees, 0..180.
  float AngleOff(float a, float b)
  {
    float d = std::fmod(a - b, 360.0f);
    if (d > 180.0f)
    {
      d -= 360.0f;
    }
    else if (d < -180.0f)
    {
      d += 360.0f;
    }
    return std::fabs(d);
  }

  // sub_824FF068: the yaw of a wall direction in radians, as the choice loop stores it.
  float RareYaw(const uint8_t *base, const Vec3 &v)
  {
    const float none = LoadBeFloat(base, kNoYawValue);
    const float len2 = v.x * v.x + v.z * v.z;
    if (!(len2 > LoadBeFloat(base, kTinyLength2)))
    {
      return none;
    }
    float c = v.z / std::sqrt(len2);
    c = c > 1.0f ? 1.0f : (c < -1.0f ? -1.0f : c);
    const float a = std::acos(c);
    return v.x < none ? -a : a;
  }

  // At most one line per `ms`; the lines held back are counted into the next one.
  bool RateOk(Clock::time_point &last, uint32_t &held, double ms, Clock::time_point now)
  {
    if (last != Clock::time_point{} && Ms(last, now) < ms)
    {
      ++held;
      return false;
    }
    last = now;
    return true;
  }

  std::string Held(uint32_t &held)
  {
    std::string s = held ? fmt::format(" ({} more not logged)", held) : std::string();
    held = 0;
    return s;
  }

  struct FaceMemo
  {
    int rare = -1, ours = -1;
    Clock::time_point last{};
    uint32_t held = 0;
  };

  struct CoverFixes
  {
    uint64_t level = 0;
    FaceMemo face[2]; // 0 out-of-cover detection, 1 in-cover rescan
    // C2
    uint64_t fan_frames = 0, fan_cycles = 0, fan_calls = 0, fan_capped = 0;
    int fan_max = 0;
    Clock::time_point fan_since{};
    // C3
    bool have_position = false; // where the detection scan stood (its r5), and when
    Vec3 position;
    uint32_t position_frame = 0;
    bool have_commit = false; // the newest completed detection scan that found cover
    uint32_t commit_frame = 0;
    Clock::time_point commit_time{};
    bool newest_empty = false; // the newest completed detection scan found no cover
    Clock::time_point stale_last{}, drop_last{}, grace_last{};
    uint32_t stale_held = 0, drop_held = 0, grace_held = 0;
    // C5
    bool armed = false, held_logged = false, allow_logged = false;
    uint32_t end = 0, last_state = 0, last_frame = 0;
    const char *disarmed_by = "taking cover";
    Clock::time_point armed_at{};
  };
  CoverFixes g_fix; // player 1 only

  // Once per level (the game state and the player character both new): resets the fixes' state and logs them.
  void LevelCheck(const uint8_t *base, uint32_t player, Clock::time_point now)
  {
    const uint64_t level = (uint64_t(LoadBe32(base, kGameInfo)) << 32) | player;
    if (level == g_fix.level)
    {
      return;
    }
    g_fix = CoverFixes{};
    g_fix.level = level;
    g_fix.fan_since = now;
    const auto on = [](bool b) { return b ? "on" : "off"; };
    REXLOG_INFO("pdz_cover: C2 [{:9.3f}] level start (single player, player 1 {:08X}): whole probe fan in one frame "
                "{} (out-of-cover scan, at most {} scanner calls a frame); C1 faced face {} (tie band {:.0f} deg); "
                "C3 no stale candidate {} ({:.0f} cm, {} frames); C5 fresh push {} (centre band +-{:.1f})",
                Stamp(now), player, on(REXCVAR_GET(pdz_cover_fix_fan)), kFanMaxCalls,
                on(REXCVAR_GET(pdz_cover_fix_faced)), kFacedTieDeg, on(REXCVAR_GET(pdz_cover_fix_stale)), kStaleCm,
                kStaleFrames, on(REXCVAR_GET(pdz_cover_fix_fresh_push)), kCentreBand);
  }

  // C2: a counter line at most every 10 s, only after frames that ran the fan.
  void FanTick(Clock::time_point now)
  {
    CoverFixes &f = g_fix;
    const double ms = Ms(f.fan_since, now);
    if (ms < 10000.0)
    {
      return;
    }
    if (f.fan_frames)
    {
      REXLOG_INFO("pdz_cover: C2 [{:9.3f}] fan: {} frames scanned, {} full cycles, {} scanner calls ({:.1f} a frame, "
                  "{} at most; {} frames stopped at the {}-call cap) in the last {:.0f} s",
                  Stamp(now), f.fan_frames, f.fan_cycles, f.fan_calls, double(f.fan_calls) / double(f.fan_frames),
                  f.fan_max, f.fan_capped, kFanMaxCalls, ms / 1000.0);
    }
    f.fan_frames = f.fan_cycles = f.fan_calls = f.fan_capped = 0;
    f.fan_max = 0;
    f.fan_since = now;
  }

  // C1: after a completed cycle, take the probe whose wall faces the scan's yaw when Rare's rank took one more than
  // kFacedTieDeg further off; rewrite the three outputs as Rare's loop (82501E04-82501E68) does for its choice.
  // Returns the probe the outputs now hold (-1 none).
  int ApplyFaced(uint8_t *base, uint32_t player, uint32_t type_out, uint32_t point_out, uint32_t yaw_out, int which,
                 Clock::time_point now)
  {
    const float look = LoadBeFloat(base, player + kScanYaw) * kRadToDeg;
    bool ok[3] = {};
    uint32_t type[3] = {};
    float wall[3] = {}, off[3] = {};
    int rare = -1, rare_rank = 0;
    float best_off = 1000.0f;
    for (int i = 0; i < 3; ++i)
    {
      const uint32_t slot = player + kScanSlots + uint32_t(i) * kSlotSize;
      type[i] = LoadBe32(base, slot + kSlotType);
      ok[i] = base[slot + kSlotOk] != 0 && Priority(type[i]) > 0;
      if (!ok[i])
      {
        continue;
      }
      wall[i] = YawDeg(LoadVec(base, slot + kSlotNormal));
      off[i] = AngleOff(wall[i], look);
      if (Priority(type[i]) > rare_rank)
      {
        rare_rank = Priority(type[i]);
        rare = i;
      }
      best_off = off[i] < best_off ? off[i] : best_off;
    }
    FaceMemo &memo = g_fix.face[which];
    int ours = -1, ours_rank = 0;
    for (int i = 0; i < 3; ++i)
    {
      if (ok[i] && off[i] <= best_off + kFacedTieDeg && Priority(type[i]) > ours_rank)
      {
        ours_rank = Priority(type[i]);
        ours = i;
      }
    }
    if (rare < 0 || ours < 0 || ours == rare || !Ptr(type_out) || !Ptr(point_out) || !Ptr(yaw_out))
    {
      memo.rare = memo.ours = -1; // Rare's choice stands; the next change is logged again
      return rare;
    }
    const uint32_t slot = player + kScanSlots + uint32_t(ours) * kSlotSize;
    StoreBe32(base, type_out, type[ours]);
    CopyVec(base, point_out, slot + kSlotPoint);
    StoreBeFloat(base, yaw_out, RareYaw(base, LoadVec(base, slot + kSlotNormal)));
    if ((memo.rare != rare || memo.ours != ours) && RateOk(memo.last, memo.held, 200.0, now))
    {
      memo.rare = rare;
      memo.ours = ours;
      REXLOG_INFO("pdz_cover: C1 [{:9.3f}] P1 faced face ({}): Rare's rank chose probe {} ({}) type {:#x} wall yaw "
                  "{:.0f} deg, {:.0f} deg off the scan's look yaw {:.0f} deg; took probe {} ({}) type {:#x} wall yaw "
                  "{:.0f} deg, {:.0f} deg off, point {}{}",
                  Stamp(now), which ? "in-cover rescan" : "out of cover", rare, ProbeName(rare), type[rare],
                  wall[rare], off[rare], look, ours, ProbeName(ours), type[ours], wall[ours], off[ours],
                  V(LoadVec(base, slot + kSlotPoint)), Held(memo.held));
    }
    return ours;
  }

  // C3: Rare's own drop of the candidate (sub_82501E88, 82502008-82502054), the writes its second empty scan makes.
  void DropCandidate(uint8_t *base, uint32_t player)
  {
    const float none = LoadBeFloat(base, kNoYawValue);
    StoreBeFloat(base, player + kLatchYaw, none);
    StoreBeFloat(base, player + kCandidateYaw, none);
    StoreBe32(base, player + kCandidateType, 0);
    CopyVec(base, player + kLatchOrigin, kZeroVector);
    CopyVec(base, player + kCandidatePoint, kZeroVector);
    StoreBeFloat(base, player + kLatchHeight, none);
    base[player + kLatchGrace] = 0;
  }

  // C3: the candidate in words: age, distance from `here`, wall against the real look yaw.
  std::string CandidateText(const uint8_t *base, uint32_t player, const Vec3 &here, uint32_t frame,
                            Clock::time_point now)
  {
    const Vec3 latch = LoadVec(base, player + kLatchOrigin);
    const float wall = LoadBeFloat(base, player + kCandidateYaw) * kRadToDeg;
    const float look = LoadBeFloat(base, player + kViewYaw);
    const std::string age = g_fix.have_commit ? fmt::format("{:.0f} ms ({} frames)", Ms(g_fix.commit_time, now),
                                                            int32_t(frame - g_fix.commit_frame))
                                              : std::string("unknown");
    return fmt::format("candidate type {:#x} [{}] point {}, age {}, found {:.0f} cm from here (scan made at {}, here "
                       "{}), wall yaw {:.0f} deg vs look yaw {:.0f} deg ({:.0f} deg off)",
                       LoadBe32(base, player + kCandidateType), TypeName(LoadBe32(base, player + kCandidateType)),
                       V(LoadVec(base, player + kCandidatePoint)), age, Dist2d(latch, here), V(latch), V(here), wall,
                       look, AngleOff(wall, look));
  }

  // C3 at the press: true = refuse it (the candidate is dropped; the callback then does what it does with none).
  bool RefuseStalePress(uint8_t *base, uint32_t player)
  {
    const uint32_t type = LoadBe32(base, player + kCandidateType);
    if (!type || LoadBe32(base, player + kCoverState) != kNotInCover)
    {
      return false;
    }
    const Vec3 latch = LoadVec(base, player + kLatchOrigin);
    if (SameVec(latch, LoadVec(base, kZeroVector)))
    {
      return false; // Rare's "not near / not facing" state: no prompt is raised from it
    }
    const CoverFixes &f = g_fix;
    const uint32_t frame = LoadBe32(base, kFrameCounter);
    // Frame differences as signed numbers: a counter that started again (a new level) gives a negative age, which
    // counts as unknown.
    const int32_t position_age = int32_t(frame - f.position_frame);
    const int32_t commit_age = int32_t(frame - f.commit_frame);
    const bool position_now = f.have_position && position_age >= 0 && position_age <= 2;
    std::string why;
    if (position_now && Dist2d(latch, f.position) > kStaleCm)
    {
      why = fmt::format("it was found {:.0f} cm from where the player stands now (over {:.0f})",
                        Dist2d(latch, f.position), kStaleCm);
    }
    else if (f.have_commit && commit_age > int32_t(kStaleFrames))
    {
      why = fmt::format("its scan is {} frames old (over {})", commit_age, kStaleFrames);
    }
    else if (f.newest_empty)
    {
      why = "the newest completed scan from where the player stands found no cover";
    }
    if (why.empty())
    {
      return false;
    }
    const auto now = Clock::now();
    const Vec3 here = position_now ? f.position : LoadVec(base, player + kPosition);
    REXLOG_INFO("pdz_cover: C3 [{:9.3f}] P1 press refused: {}; {}", Stamp(now), why,
                CandidateText(base, player, here, frame, now));
    DropCandidate(base, player);
    return true;
  }

  // C5, at sub_825001F0's query of which end the player is at (x = its f29, the side switch input). `answer` is
  // sub_82504E50's result (0 side A, 1 side B, 2 none); returns the answer to give (2 = no switch this frame).
  uint32_t FreshPush(const uint8_t *base, uint32_t player, float x, uint32_t answer)
  {
    const auto now = Clock::now();
    LevelCheck(base, player, now);
    CoverFixes &f = g_fix;
    const uint32_t frame = LoadBe32(base, kFrameCounter);
    const uint32_t state = LoadBe32(base, player + kCoverState);
    const uint32_t side = LoadBe32(base, player + kCoverSide);
    const uint32_t end = (side & 0x09) ? 1 : (side & 0x12) ? 2 : 0; // 1 = side A end, 2 = side B end
    if (state == kEntering && (f.last_state != kEntering || frame - f.last_frame > 2))
    {
      f.armed = f.held_logged = f.allow_logged = false;
      f.disarmed_by = "taking cover";
      f.end = end;
    }
    f.last_state = state;
    f.last_frame = frame;
    if (end && f.end && end != f.end)
    {
      if (f.allow_logged)
      {
        REXLOG_INFO("pdz_cover: C5 [{:9.3f}] P1 fresh push: switched, now side {:#x} [{}]", Stamp(now), side,
                    TypeName(side));
      }
      f.armed = f.held_logged = f.allow_logged = false;
      f.disarmed_by = "the last change of end";
    }
    if (end)
    {
      f.end = end;
    }
    const uint32_t input = LoadBe32(base, player + kInputBlock);
    if (!Ptr(input) || base[input + kInputStickPath] == 0)
    {
      return answer; // the switch is fed by Rare's other control then (825007AC-825007E0): left as it is
    }
    if (std::fabs(x) < kCentreBand && !f.armed)
    {
      f.armed = true;
      f.armed_at = now;
      f.held_logged = f.allow_logged = false;
    }
    // Rare's switch test (825007E4-825008AC): in cover, no low top, both ends open at the height in use, and the
    // input past the threshold towards the other end.
    const uint32_t type = LoadBe32(base, player + kCoverType);
    if (state != kInCover || answer > 1 || (type & 0x04))
    {
      return answer;
    }
    const bool open = ((type & 0x01) && (type & 0x02) && (side & 0x03)) ||
                      ((type & 0x08) && (type & 0x10) && (side & 0x18));
    const bool push = answer == 1 ? x < LoadBeFloat(base, kSwitchLeft) : x > LoadBeFloat(base, kSwitchRight);
    if (!open || !push)
    {
      return answer;
    }
    if (!f.armed)
    {
      if (!f.held_logged)
      {
        f.held_logged = true;
        REXLOG_INFO("pdz_cover: C5 [{:9.3f}] P1 held push ignored: input {:+.2f}, not back inside +-{:.1f} since {}; "
                    "side {:#x} [{}], type {:#x} [{}]",
                    Stamp(now), x, kCentreBand, f.disarmed_by, side, TypeName(side), type, TypeName(type));
      }
      return 2; // the switch's own "no" path (825009D0)
    }
    if (!f.allow_logged)
    {
      f.allow_logged = true;
      REXLOG_INFO("pdz_cover: C5 [{:9.3f}] P1 fresh push: side switch allowed, input {:+.2f} (back inside +-{:.1f} "
                  "{:.0f} ms before); from side {:#x} [{}] to the other end",
                  Stamp(now), x, kCentreBand, Ms(f.armed_at, now), side, TypeName(side));
    }
    return answer;
  }
} // namespace

// The scanner: one probe step a call. The log records each probe as it finishes and logs a completed cycle when its
// result differs from the last one logged. The fixes (player 1, single player): C2 runs the detection's cycle to the
// end in one frame, C3 makes the detection scan from where the player stands and drops an unconfirmed candidate,
// C1 takes the faced wall when a cycle completes (detection and in-cover rescan).
REX_HOOK_RAW(sub_82501A00)
{
  const uint32_t player = ctx.r3.u32;
  const uint32_t caller = uint32_t(ctx.lr);
  const bool detection = caller == kReturnFromDetection;
  const bool rescan = caller == kReturnFromInCover;
  const bool log = Enabled();
  bool faced = false, fan = false, stale = false;
  if ((detection || rescan) && AnyFix() && SoloPlayerOne(base, player))
  {
    faced = REXCVAR_GET(pdz_cover_fix_faced);
    fan = detection && REXCVAR_GET(pdz_cover_fix_fan);
    stale = detection && REXCVAR_GET(pdz_cover_fix_stale);
  }
  if (!log && !faced && !fan && !stale)
  {
    __imp__sub_82501A00(ctx, base);
    return;
  }
  const auto now = Clock::now();
  if (faced || fan || stale)
  {
    LevelCheck(base, player, now);
  }
  const uint32_t type_out = ctx.r9.u32, point_out = ctx.r10.u32;
  const uint32_t yaw_out = LoadBe32(base, ctx.r1.u32 + 84); // the 9th argument, in the caller's frame
  const uint32_t frame = LoadBe32(base, kFrameCounter);

  // C3 before the scan. The detection passes the latched place and yaw (r5 / r6) while the grace is set; with the
  // grace never left set, r5 is where the player stands.
  bool run_to_end = fan;
  bool from_here = true;
  if (stale)
  {
    CoverFixes &f = g_fix;
    const Vec3 here = LoadVec(base, ctx.r5.u32);
    if (base[player + kLatchGrace])
    {
      from_here = false;
      base[player + kLatchGrace] = 0; // the next scan is made from where the player stands
      if (RateOk(f.grace_last, f.grace_held, 1000.0, now))
      {
        REXLOG_INFO("pdz_cover: C3 [{:9.3f}] P1 grace cleared: this scan is Rare's re-check from the latched place "
                    "{}; the next ones are made from where the player stands{}",
                    Stamp(now), V(here), Held(f.grace_held));
      }
    }
    else
    {
      f.have_position = true;
      f.position = here;
      f.position_frame = frame;
      const Vec3 latch = LoadVec(base, player + kLatchOrigin);
      if (LoadBe32(base, player + kCandidateType) && !SameVec(latch, LoadVec(base, kZeroVector)) &&
          Dist2d(latch, here) > kStaleCm)
      {
        // The candidate was found too far from here: finish a scan from here in this frame. A cycle that began
        // further than kStaleCm away is started again here (as Rare's own restart does, from probe 0).
        run_to_end = true;
        bool restarted = false;
        float began = 0.0f;
        if (LoadBe32(base, player + kScanProbe) || LoadBe32(base, player + kScanStep))
        {
          began = Dist2d(LoadVec(base, player + kScanOrigin), here);
          if (began > kStaleCm)
          {
            StoreBe32(base, player + kScanProbe, 0);
            StoreBe32(base, player + kScanStep, 0);
            restarted = true;
            if (log)
            {
              for (ProbeResult &r : g_logs[player].scan.work)
              {
                r = ProbeResult{};
              }
            }
          }
        }
        if ((!fan || restarted) && RateOk(f.stale_last, f.stale_held, 1000.0, now))
        {
          REXLOG_INFO("pdz_cover: C3 [{:9.3f}] P1 stale candidate: the scan runs to its end in this frame{}; {}{}",
                      Stamp(now),
                      restarted ? fmt::format(" (its cycle began {:.0f} cm away: restarted here)", began)
                                : std::string(),
                      CandidateText(base, player, here, frame, now), Held(f.stale_held));
        }
      }
    }
  }

  // The scan: one call, or (C2 / C3) calls until the cycle completes, at most kFanMaxCalls. The argument registers
  // are put back before each further call (the stack arguments are in the caller's frame and unchanged).
  const PPCRegister a3 = ctx.r3, a4 = ctx.r4, a5 = ctx.r5, a6 = ctx.r6, a7 = ctx.r7, a8 = ctx.r8, a9 = ctx.r9,
                    a10 = ctx.r10, af1 = ctx.f1, af2 = ctx.f2;
  const uint64_t lr = ctx.lr;
  bool complete = false;
  int calls = 0;
  for (;;)
  {
    const uint32_t probe_before = LoadBe32(base, player + kScanProbe);
    const uint32_t step_before = LoadBe32(base, player + kScanStep);
    __imp__sub_82501A00(ctx, base);
    ++calls;
    complete = (ctx.r3.u32 & 0xFF) != 0;
    if (log)
    {
      TrackStep(base, player, probe_before, step_before, complete);
    }
    if (complete || !run_to_end || calls >= kFanMaxCalls)
    {
      break;
    }
    ctx.r3 = a3;
    ctx.r4 = a4;
    ctx.r5 = a5;
    ctx.r6 = a6;
    ctx.r7 = a7;
    ctx.r8 = a8;
    ctx.r9 = a9;
    ctx.r10 = a10;
    ctx.f1 = af1;
    ctx.f2 = af2;
    ctx.lr = lr;
  }
  if (fan)
  {
    CoverFixes &f = g_fix;
    ++f.fan_frames;
    f.fan_calls += uint64_t(calls);
    f.fan_max = calls > f.fan_max ? calls : f.fan_max;
    f.fan_cycles += complete ? 1 : 0;
    f.fan_capped += (!complete && calls >= kFanMaxCalls) ? 1 : 0;
    FanTick(now);
  }
  if (!complete)
  {
    return;
  }

  int chosen = -2;
  if (faced)
  {
    chosen = ApplyFaced(base, player, type_out, point_out, yaw_out, rescan ? 1 : 0, now);
  }
  if (stale)
  {
    CoverFixes &f = g_fix;
    const uint32_t result = Ptr(type_out) ? LoadBe32(base, type_out) : 0;
    if (result)
    {
      f.have_commit = true; // the detection commits it (82501F8C-82501FF4) right after this returns
      f.commit_frame = frame;
      f.commit_time = now;
      f.newest_empty = false;
    }
    else
    {
      f.newest_empty = true;
      if (LoadBe32(base, player + kCandidateType))
      {
        // Rare would forgive this scan and re-check from the latched place; the candidate goes now instead. The
        // detection then finds no candidate: no grace, no prompt (82502094).
        if (RateOk(f.drop_last, f.drop_held, 500.0, now))
        {
          REXLOG_INFO("pdz_cover: C3 [{:9.3f}] P1 candidate dropped: the newest scan (from {}) found no cover; {}{}",
                      Stamp(now), from_here ? "where the player stands" : "the latched place",
                      CandidateText(base, player, LoadVec(base, player + kScanOrigin), frame, now),
                      Held(f.drop_held));
        }
        DropCandidate(base, player);
      }
    }
  }
  if (log)
  {
    TrackComplete(base, player, caller, type_out, point_out, yaw_out, chosen, now);
  }
}

// The context action callback: with target == player and a candidate it enters cover.
REX_HOOK_RAW(sub_825AAAD0)
{
  const uint32_t player = ctx.r3.u32;
  if (Enabled() && ctx.r4.u32 == player)
  {
    const auto now = Clock::now();
    PlayerLog &log = g_logs[player];
    const std::string tag = Tag(base, player);
    const uint32_t type = LoadBe32(base, player + kCandidateType);
    const Vec3 position = LoadVec(base, player + kPosition);
    const Vec3 point = LoadVec(base, player + kCandidatePoint);
    const Vec3 latch = LoadVec(base, player + kLatchOrigin);
    const float flag = LoadBeFloat(base, player + kEntryFlagFloat);
    log.enter_request = now;
    log.enter_pending = type != 0;
    log.press_position = position;
    log.press_point = point;
    if (!type)
    {
      REXLOG_INFO("pdz_cover: [{:9.3f}] {} PRESS with no candidate (nothing happens): player at {} look yaw {:.0f} "
                  "deg, state {}",
                  Stamp(now), tag, V(position), LoadBeFloat(base, player + kViewYaw),
                  LoadBe32(base, player + kCoverState));
    }
    else
    {
      const uint32_t side = PredictSide(type & 0xFF, flag < 0.0f);
      REXLOG_INFO("pdz_cover: [{:9.3f}] {} PRESS: player at {} look yaw {:.0f} deg; candidate type {:#x} [{}] height "
                  "{}, point {} ({:.0f} cm away, {:+.0f} cm up), wall yaw {:.0f} deg; found by a scan made at {} "
                  "yaw {:.0f} deg (grace {}); the entry will use side {:#x} [{}] (Rare's order: HIGH side A, HIGH "
                  "side B, LOW over the top, low-pass side A, low-pass side B; player +0x1684 = {:.2f})",
                  Stamp(now), tag, V(position), LoadBeFloat(base, player + kViewYaw), type,
                  TypeName(type), HeightName(type), V(point), Dist2d(point, position), point.y - position.y,
                  LoadBeFloat(base, player + kCandidateYaw) * kRadToDeg, V(latch),
                  LoadBeFloat(base, player + kLatchYaw) * kRadToDeg, base[player + kLatchGrace], side,
                  TypeName(side), flag);
    }
    LogScan(base, player, log, now, "at the press");
    const uint32_t probe = LoadBe32(base, player + kScanProbe), step = LoadBe32(base, player + kScanStep);
    if (probe || step)
    {
      REXLOG_INFO("pdz_cover: [{:9.3f}] {}   (a new cycle is at probe {} step {}; the candidate above is the one "
                  "the press uses)",
                  Stamp(now), tag, probe, step);
    }
  }
  // C3: a stale candidate is dropped and the press does what Rare's callback does without one (nothing).
  if (ctx.r4.u32 == player && REXCVAR_GET(pdz_cover_fix_stale) && SoloPlayerOne(base, player) &&
      RefuseStalePress(base, player))
  {
    return;
  }
  __imp__sub_825AAAD0(ctx, base);
}

// C5: sub_82504E50(player) -> 0 (side A end), 1 (side B end) or 2 (no side switch). Its call from sub_825001F0 at
// 82500294 gives r26, which only the side switch (825007D0, 82500844, 82500894) reads; f29 then holds the switch's
// input (loaded at 82500254). Other callers are left alone.
REX_HOOK_RAW(sub_82504E50)
{
  const uint32_t player = ctx.r3.u32;
  const uint32_t caller = uint32_t(ctx.lr);
  const float input = float(ctx.f29.f64);
  __imp__sub_82504E50(ctx, base);
  if (caller != kReturnFromSideQuery || !REXCVAR_GET(pdz_cover_fix_fresh_push) || !SoloPlayerOne(base, player))
  {
    return;
  }
  const uint32_t answer = ctx.r3.u32;
  const uint32_t given = FreshPush(base, player, input, answer);
  if (given != answer)
  {
    ctx.r3.u64 = given;
  }
}

// sub_824FD738(player, &point, f1 = wall yaw, r6 = type): starts entering cover.
REX_HOOK_RAW(sub_824FD738)
{
  if (!Enabled())
  {
    __imp__sub_824FD738(ctx, base);
    return;
  }
  const uint32_t player = ctx.r3.u32;
  const Vec3 point = ctx.r4.u32 ? LoadVec(base, ctx.r4.u32) : Vec3{};
  const float yaw_deg = float(ctx.f1.f64) * kRadToDeg;
  const uint32_t type = ctx.r6.u32 & 0xFF;
  const uint32_t caller = uint32_t(ctx.lr);
  const Vec3 before = LoadVec(base, player + kPosition);
  __imp__sub_824FD738(ctx, base);
  const uint32_t state = LoadBe32(base, player + kCoverState);
  const uint32_t side = LoadBe32(base, player + kCoverSide);
  const Vec3 snap = LoadVec(base, player + kCoverEntrySnap);
  REXLOG_INFO("pdz_cover: [{:9.3f}] {} ENTER {} (from {:08X}): asked point {} wall yaw {:.0f} deg type {:#x} [{}]; "
              "player at {}; now state {} target {}, type {:#x}, side {:#x} [{}], wall yaw {:.0f} deg, snap position "
              "{} ({:.0f} cm from the player, {:.0f} cm from the asked point)",
              Stamp(Clock::now()), Tag(base, player), state == kEntering ? "started" : "REFUSED", caller, V(point),
              yaw_deg, type, TypeName(type), V(before), state, LoadBe32(base, player + kCoverTarget),
              LoadBe32(base, player + kCoverType), side, TypeName(side),
              LoadBeFloat(base, player + kCoverWallYaw) * kRadToDeg, V(snap), Dist2d(snap, before),
              Dist2d(snap, point));
}

// sub_824FE9D0(player, r4 = instant): takes the player out of cover.
REX_HOOK_RAW(sub_824FE9D0)
{
  if (Enabled())
  {
    const uint32_t player = ctx.r3.u32;
    const uint32_t state = LoadBe32(base, player + kCoverState);
    if (state != kNotInCover) // Rare's function does nothing otherwise
    {
      const auto now = Clock::now();
      PlayerLog &log = g_logs[player];
      log.exit_request = now;
      log.exit_pending = true;
      REXLOG_INFO("pdz_cover: [{:9.3f}] {} forced exit (from {:08X}, instant {}), state {}, player at {}",
                  Stamp(now), Tag(base, player), uint32_t(ctx.lr), ctx.r4.u32 & 0xFF, state,
                  V(LoadVec(base, player + kPosition)));
    }
  }
  __imp__sub_824FE9D0(ctx, base);
}

// sub_824FE550(player, f1 = dt): the cover module's per-frame update. State changes with times, the candidate
// appearing / going, and where the player is once snapped in.
REX_HOOK_RAW(sub_824FE550)
{
  if (!Enabled())
  {
    __imp__sub_824FE550(ctx, base);
    return;
  }
  const uint32_t player = ctx.r3.u32;
  const double dt = ctx.f1.f64;
  __imp__sub_824FE550(ctx, base);

  PlayerLog &log = g_logs[player];
  const uint32_t state = LoadBe32(base, player + kCoverState);
  const uint32_t target = LoadBe32(base, player + kCoverTarget);
  const uint32_t type = LoadBe32(base, player + kCoverType);
  const uint32_t side = LoadBe32(base, player + kCoverSide);
  const uint32_t candidate = LoadBe32(base, player + kCandidateType);
  const uint32_t rescan = LoadBe32(base, player + kRescanType);
  const uint8_t busy = base[player + kCoverBusy];
  if (log.valid && state == log.state && target == log.target && type == log.type && side == log.side &&
      candidate == log.candidate && busy == log.busy && rescan == log.rescan)
  {
    return;
  }
  const auto now = Clock::now();
  const std::string tag = Tag(base, player);
  const Vec3 position = LoadVec(base, player + kPosition);
  if (!log.valid)
  {
    log.state_since = now;
    log.cover_since = now;
    log.state = state; // no transition line for the first sight of a player
    log.candidate = 0;
  }

  if (candidate != log.candidate && state == kNotInCover)
  {
    if (candidate)
    {
      const Vec3 point = LoadVec(base, player + kCandidatePoint);
      REXLOG_INFO("pdz_cover: [{:9.3f}] {} candidate {}: type {:#x} [{}] height {}, point {} ({:.0f} cm away), wall "
                  "yaw {:.0f} deg; player at {} look yaw {:.0f} deg",
                  Stamp(now), tag, log.candidate ? "changed" : "found", candidate, TypeName(candidate),
                  HeightName(candidate), V(point), Dist2d(point, position),
                  LoadBeFloat(base, player + kCandidateYaw) * kRadToDeg, V(position),
                  LoadBeFloat(base, player + kViewYaw));
    }
    else
    {
      REXLOG_INFO("pdz_cover: [{:9.3f}] {} candidate lost (player at {})", Stamp(now), tag, V(position));
    }
  }

  if (state != log.state)
  {
    const uint32_t old = log.state;
    const char *event = "state change";
    double since = Ms(log.state_since, now);
    const char *since_what = "in the previous state";
    if (old == kEntering && state == kInCover)
    {
      event = "snapped in";
      if (log.enter_pending)
      {
        since = Ms(log.enter_request, now);
        since_what = "since the press";
      }
      log.cover_since = now;
    }
    else if (old == kInCover && state == kTransition && target == kPoppedOut)
    {
      event = "lean out start";
    }
    else if (old == kTransition && state == kPoppedOut)
    {
      event = "leaned out (can fire)";
    }
    else if (old == kPoppedOut && state == kTransition && target == kInCover)
    {
      event = "lean back start";
    }
    else if (old == kTransition && state == kInCover)
    {
      event = "lean end (back in cover)";
    }
    else if (state == kExiting)
    {
      event = "exit started";
      log.exit_request = now;
      log.exit_pending = true;
    }
    else if (state == kNotInCover)
    {
      event = old == kExiting ? "exited" : "exited instantly";
      if (log.exit_pending)
      {
        since = Ms(log.exit_request, now);
        since_what = "since the exit began";
      }
      log.exit_pending = false;
    }
    REXLOG_INFO("pdz_cover: [{:9.3f}] {} {}: state {}->{} target {} type {:#x} side {:#x} [{}] busy {} | {:.0f} ms "
                "{}, dt {:.1f} ms",
                Stamp(now), tag, event, old, state, target, type, side, TypeName(side), busy, since, since_what,
                dt * 1000.0);
    if (old == kEntering && state == kInCover)
    {
      const Vec3 snap = LoadVec(base, player + kCoverEntrySnap);
      REXLOG_INFO("pdz_cover: [{:9.3f}] {} IN COVER at {} (press position {}, candidate point {}; {:.0f} cm from the "
                  "entry snap position {}), wall yaw {:.0f} deg, look yaw {:.0f} deg, type {:#x} [{}], side {:#x} "
                  "[{}], anchor {}",
                  Stamp(now), tag, V(position), V(log.press_position), V(log.press_point), Dist2d(position, snap),
                  V(snap), LoadBeFloat(base, player + kCoverWallYaw) * kRadToDeg,
                  LoadBeFloat(base, player + kViewYaw), type, TypeName(type), side, TypeName(side),
                  V(LoadVec(base, player + kCoverSnap)));
    }
    if (state == kNotInCover || state == kInCover)
    {
      log.enter_pending = false;
    }
    if (state == kNotInCover && old != kEntering)
    {
      REXLOG_INFO("pdz_cover: [{:9.3f}] {} out of cover after {:.0f} ms in cover, at {}", Stamp(now), tag,
                  Ms(log.cover_since, now), V(position));
    }
    log.state_since = now;
  }
  else if (log.valid && state != kNotInCover &&
           (target != log.target || type != log.type || side != log.side || busy != log.busy ||
            rescan != log.rescan))
  {
    // In cover the rescan (sub_825001F0 -> scanner, from the latched place) can change the type and the side:
    // a change here is the cover re-snapping without a press.
    REXLOG_INFO("pdz_cover: [{:9.3f}] {} in-cover change: state {} target {}->{} type {:#x}->{:#x} side {:#x}->{:#x} "
                "[{}] rescan type {:#x}->{:#x} [{}] busy {}->{} | player at {}, wall yaw {:.0f} deg, anchor {}",
                Stamp(now), tag, state, log.target, target, log.type, type, log.side, side, TypeName(side),
                log.rescan, rescan, TypeName(rescan), log.busy, busy, V(position),
                LoadBeFloat(base, player + kCoverWallYaw) * kRadToDeg, V(LoadVec(base, player + kCoverSnap)));
  }
  log.valid = true;
  log.state = state;
  log.target = target;
  log.type = type;
  log.side = side;
  log.candidate = candidate;
  log.rescan = rescan;
  log.busy = busy;
}
