// Guard footsteps on every level (Mon 16 Nov 2026; pdz_stealth_guard_footsteps, on by default, part of the stealth
// layer: nothing happens while the master switch pdz_lab_guard_stealth is off, which is Rare's game).
//
// Rare's step sound (read in the code):
//   - A step is an ANIMATION EVENT. The event handler sub_821EDD40 (r3 character, r4 event; type = uint16 at event + 4)
//     sends the footstep type to sub_821ED878 (r3 character, r4 = which foot: event byte 6 == 0). There is no other
//     caller: a walk or run animation that carries no footstep event makes no step sound, whoever plays it.
//   - sub_821ED878 takes the foot bone's place (bone 12 or 16 of the skeleton at character + 72), plays the level's
//     foot effects, and calls sub_821FF7C0 (r3 character, r4 -> position): the surface under the foot gives the cue
//     (surface record + 128 walking, + 132 running; running from 480 cm/s, 230400.0 at 820EC25C; no cue name = silent).
//     For a character of type 2 (a human figure, character + 12) that no player controls, the cue is started as a
//     positioned sound with the cue's own volume and no delay (sub_821FF428, then DarkAudioSound::Play with 0.0). No
//     test of the level, the character's setup or the AI's state: Jo's own steps only differ in a fixed volume (0.8,
//     0.5 walking, 0.25 creeping). The step also raises Rare's noise pulse (200 walking / 400 running, sub_8218B150).
//   - Early outs in sub_821FF7C0: the character's controller byte + 1606 (set for a figure that is down), and the
//     human-only quiet flag. Neither depends on the level.
// So where a guard is silent while he walks, his animation carries no footstep event (inferred: the animation data
// itself was not decoded; the routine has no other way to be silent on a surface that has a cue). This file settles it
// in the game and supplies the missing steps:
//   - the hook of sub_821ED878 notes every real step event of a tracked character;
//   - Tick (called from the NPC sight update hook sub_82580508 in lab_stealth.cpp, once per update per character)
//     follows each human figure's position (character + 48). A figure that has walked 2.5 m with no step event of its
//     own gets Rare's own routine sub_821ED878 called for it, one call per stride (80 cm walking, 140 cm running), feet
//     alternating: Rare's surface look-up, Rare's cue, Rare's ranges (12 m walking / 15 m running), Rare's noise
//     pulse. The first real step event of that figure stops the supplied steps at once and for the next 3 s, so a
//     character whose animation has its own steps (the Subway bikers) is never doubled.
// Not written to: any guest memory (Rare's routine does its own work). No syscalls: one hash look-up and a few loads
// per character update; the log lines are rare (first supplied step per character, capped, and totals at powers of 2).
//
// Tue 17 Nov night (job fix2, item 8): pdz_snd_all_footsteps (Gameplay, on, under pdz_restored_sounds). The same
// supplied steps for EVERY human figure the game updates on EVERY level (clubbers and other civilians, scientists,
// guards, enemies, allies), whether or not the stealth layer runs. Sound only in effect: Rare's step routine makes
// its own noise pulse as before, but the stealth layer's hearing is its own and is not fed by it. Never doubled, now
// checked PER ANIMATION as well: every real step event marks the animation playing at that moment as one that has
// steps (sub_821EC340, the number the log prints); a figure whose current animation is marked gets nothing supplied,
// and a real step event of his own still stops the supplied steps for 3 s. The log names each animation supplied for
// the first time (pdz_all_steps: lines, capped), so the morning report can say which animations it covers.
#include <cmath>
#include <cstdint>
#include <cstring>
#include <unordered_map>

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/ppc/func.h>
#include "lab_stealth.h" // Counter-Op round 14: the layer is off in a Counter-Operative game
#include "guest_readable_Sat21Nov.h" // Sun 22 Nov night (audit): pdz::guestmem::Ok
#include "hook_profile.h" // pdz_hook_profile (must follow <rex/hook.h>)

REXCVAR_DEFINE_BOOL(pdz_stealth_guard_footsteps, true, "Gameplay",
                    "Stealth layer: every walking guard makes the game's own step sounds. In the game a step is an "
                    "event in the walk or run animation, and some guards' animations have none, so they walk "
                    "silently. On: a guard who walks without step events gets the game's step routine called for each "
                    "stride (the surface's own sound, heard to 12 m walking and 15 m running); guards who already "
                    "have steps are left alone. Does nothing while pdz_lab_guard_stealth is off")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_stealth_guard_footsteps_log, false, "Debug",
                    "Diagnostic for pdz_stealth_guard_footsteps: a 'pdz_guard_steps:' line for each character's first "
                    "real step event and first supplied step (with its animation number), to see which characters "
                    "walk without step events")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DECLARE(bool, pdz_lab_guard_stealth);
REXCVAR_DECLARE(bool, pdz_restored_sounds);
REXCVAR_DEFINE_BOOL(pdz_snd_step_log, true, "Diagnostics",
                    "Log only: one line for every footstep event of a character within 15 m of player one (the "
                    "game's own event or one supplied by pdz_snd_all_footsteps): the cue, whether the sound was made, "
                    "its range and loudness, the distance. At most 600 lines a session")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_snd_steps_silent_walkers, true, "Gameplay",
                    "Restored sound: a character who has walked 5 m without a single footstep gets the game's step "
                    "routine each stride even when his animation has made steps for someone before (a search walk "
                    "that plays a stepping animation without its step events). Off = such an animation is never "
                    "covered")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_snd_all_footsteps, true, "Gameplay",
                    "Restored sound: every character who walks or runs (clubbers, civilians, scientists, guards, allies) "
                    "makes the game's own step sounds for the floor under him, on every level, when his walk or run "
                    "animation has no step events of its own. Animations that already make steps are left alone. "
                    "Sound only. Needs pdz_restored_sounds")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);

REX_EXTERN(__imp__sub_821ED878); // Rare's step: r3 character, r4 which foot (1 / 0)
REX_EXTERN(sub_821EC340);        // the animation playing: r3 [character + 72], r4 0 -> r3 its number

namespace
{
  constexpr uint32_t kFrameCounter = 0x82D6BFD8 + 136; // one per Swap (transparency_water.cpp)
  constexpr uint32_t kFrameDt = 0x82D6BFD8 + 120;      // the game's frame time, seconds (physics_step.cpp)
  constexpr uint32_t kPosition = 48;                   // character: x, y (up), z in cm
  constexpr uint32_t kType = 12;                       // character: its class number (sub_821FF7C0 reads it)
  // (fix2, Tue 17 Nov night: a play log shows every NPC with +12 = 33; 2 is the branch of sub_821FF7C0 that calls
  // the player's own functions (vfunc 656), 43 a corpse. The test for 2 here never matched an NPC, so no step was
  // ever supplied: the guard fix of 16 Nov was inert. NPCs (33) only; Jo's steps are her own.)
  constexpr uint32_t kNpcType = 33;
  constexpr uint32_t kSkeleton = 72;
  constexpr float kWalkStride = 80.0f, kRunStride = 140.0f; // cm between supplied steps
  constexpr float kRunSpeed = 480.0f;                       // Rare's running threshold, cm/s
  constexpr float kMinSpeed = 60.0f, kMaxSpeed = 900.0f;    // slower: standing / shuffling; faster: not on foot
  constexpr float kSilentWalk = 250.0f;                     // cm walked with no step event before the first one
  constexpr float kOwnStepsHold = 3.0f;                     // s after a real step event: nothing supplied
  constexpr float kJump = 300.0f;                           // cm in one update: a warp or a spawn, not a walk

  bool Plausible(uint32_t p) { return p >= 0x10000u && p < 0xF0000000u && (p & 3u) == 0; }

  uint32_t Load32(const uint8_t *base, uint32_t a)
  {
    const uint8_t *p = base + a;
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
  }

  float LoadFloat(const uint8_t *base, uint32_t a)
  {
    const uint32_t bits = Load32(base, a);
    float v;
    std::memcpy(&v, &bits, 4);
    return v;
  }

  struct Walker
  {
    bool have = false;      // pos / frame are set
    bool supplying = false; // this figure walks without step events: one step per stride
    bool real_seen = false; // a real step event was seen (real_frame)
    bool foot = false;
    bool logged_real = false, logged_supplied = false, logged_tick = false;
    bool running = false, moving = false; // as last seen (for the tally)
    uint32_t frame = 0, real_frame = 0;
    float pos[3] = {};
    float travel = 0.0f; // cm walked since the last step (real or supplied)
    int marked_refusals = 0; // (Wed 18 Nov night) times his 2.5 m of silent walking ended on a marked animation
    bool logged_marked = false;
  };
  // Wed 18 Nov night (pdz_snd_step_log): the step event running now on this thread, for the sound hook.
  thread_local uint32_t t_step_char = 0;
  thread_local bool t_step_ours = false;
  thread_local int t_step_sounds = 0;
  int g_step_lines = 0;
  constexpr uint32_t kStepPlayerOne = 0x82E3C3E4;
  // His distance to player one (cm), or -1.
  float StepDistance(const uint8_t *base, uint32_t c)
  {
    const uint32_t jo = Load32(base, kStepPlayerOne);
    if (!Plausible(jo) || !Plausible(c) || jo == c)
    {
      return -1.0f;
    }
    const float dx = LoadFloat(base, c + kPosition) - LoadFloat(base, jo + kPosition),
                dy = LoadFloat(base, c + kPosition + 4) - LoadFloat(base, jo + kPosition + 4),
                dz = LoadFloat(base, c + kPosition + 8) - LoadFloat(base, jo + kPosition + 8);
    return std::sqrt(dx * dx + dy * dy + dz * dz);
  }
  bool StepLogged(const uint8_t *base, uint32_t c, float &d)
  {
    if (!REXCVAR_GET(pdz_snd_step_log) || g_step_lines >= 600 || !Plausible(c) || Load32(base, c + kType) != kNpcType)
    {
      return false;
    }
    d = StepDistance(base, c);
    return d >= 0.0f && d <= 1500.0f;
  }
  struct StepScope
  {
    uint32_t prev_char;
    bool prev_ours;
    int prev_sounds;
    StepScope(uint32_t c, bool ours) : prev_char(t_step_char), prev_ours(t_step_ours), prev_sounds(t_step_sounds)
    {
      t_step_char = c;
      t_step_ours = ours;
      t_step_sounds = 0;
    }
    void Done(const uint8_t *base)
    {
      float d = 0.0f;
      if (t_step_sounds == 0 && StepLogged(base, t_step_char, d))
      {
        ++g_step_lines;
        REXLOG_INFO("pdz_snd_step: character {:08X}, {:.1f} m from Jo: a step event ({}) and NO sound was asked for (Rare's "
                    "routine left early: in the air, a silent controller, or a surface with no step name) (line {} of 600)",
                    t_step_char, d / 100.0f, t_step_ours ? "supplied by pdz_snd_all_footsteps" : "the game's own", g_step_lines);
      }
    }
    ~StepScope()
    {
      t_step_char = prev_char;
      t_step_ours = prev_ours;
      t_step_sounds = prev_sounds;
    }
  };
  std::unordered_map<uint32_t, Walker> g_walkers; // the game thread only
  std::unordered_map<uint32_t, bool> g_anim_steps; // (fix2) animation number -> a real step event was seen in it
  std::unordered_map<uint32_t, uint32_t> g_anim_supplied; // (fix2) animation number -> steps supplied in it
  bool g_in_supplied = false;                     // our own call of Rare's routine is not a real event
  uint64_t g_supplied = 0, g_real = 0;
  uint32_t g_first_lines = 0, g_first_ticks = 0, g_type_lines = 0;
  // Wed 18 Nov (sound3; logs 019 / 020 have not one supplied step): why a walking figure got no step, counted, and
  // said every 30 s while anything changes (pdz_all_steps: tally; log only).
  uint64_t g_why_marked = 0, g_why_hold = 0, g_why_lead = 0, g_why_slow = 0, g_ticks = 0, g_tally_said = 0;
  uint32_t g_tally_frame = 0, g_real_anim_lines = 0;
  double g_walk_m = 0.0, g_run_m = 0.0;     // metres the followed figures moved at walking / running speed
  uint64_t g_walk_real = 0, g_run_real = 0; // real step events of figures last seen walking / running
  void Tally(const uint8_t *base, uint32_t frame)
  {
    if (frame - g_tally_frame < 1800) // (about 30 s at 60, 15 s at 120)
    {
      return;
    }
    g_tally_frame = frame;
    const uint64_t sum = g_supplied + g_real + g_why_marked + g_why_hold + g_why_lead;
    if (sum == g_tally_said)
    {
      return;
    }
    g_tally_said = sum;
    REXLOG_INFO("pdz_all_steps: tally: {} steps supplied, {} real step events of followed figures; a walking figure got "
                "none {} times because its animation is marked as having steps ({} animations marked), {} times within "
                "3 s of a real step of his own, {} updates still in the first 2.5 m; {} updates standing or slower than "
                "0.6 m/s; {} figures followed, {} updates | walking (under 4.8 m/s): {:.0f} m moved, {} real steps (one per "
                "{:.1f} m); running: {:.0f} m moved, {} real steps (one per {:.1f} m)",
                g_supplied, g_real, g_why_marked, g_anim_steps.size(), g_why_hold, g_why_lead, g_why_slow, g_walkers.size(),
                g_ticks, g_walk_m, g_walk_real, g_walk_real ? g_walk_m / double(g_walk_real) : 0.0, g_run_m, g_run_real,
                g_run_real ? g_run_m / double(g_run_real) : 0.0);
  }

  bool On()
  {
    // Merge Fri 20 Nov: the stealth layer's supplied guard steps stay off in a Counter-Operative game (Counter-Op
    // round 14), the restored-sounds steps for every figure (fix2) stay on there, as the other sound fixes do.
    return (REXCVAR_GET(pdz_stealth_guard_footsteps) && REXCVAR_GET(pdz_lab_guard_stealth) && !lab_stealth::CounterOpGameNow()) ||
           (REXCVAR_GET(pdz_restored_sounds) && REXCVAR_GET(pdz_snd_all_footsteps)); // (fix2: every figure, every level)
  }

  float FrameDt(const uint8_t *base)
  {
    const float dt = LoadFloat(base, kFrameDt);
    return dt > 1.0f / 240.0f && dt < 0.05f ? dt : dt >= 0.05f ? 0.05f : 1.0f / 240.0f; // also not-a-number
  }

  uint32_t Animation(PPCContext &ctx, uint8_t *base, uint32_t character)
  {
    const uint32_t skeleton = Load32(base, character + kSkeleton);
    if (!Plausible(skeleton) || !pdz::guestmem::Ok(base, skeleton, 4)) // (Sun 22 Nov night, audit)
    {
      return 0;
    }
    rex::CallFrame frame(ctx);
    frame.ctx.r1.u64 = (ctx.r1.u32 - 0x1000) & ~0xFu;
    frame.ctx.r3.u64 = skeleton;
    frame.ctx.r4.u64 = 0;
    sub_821EC340(frame.ctx, base);
    return frame.ctx.r3.u32;
  }

  void Supply(PPCContext &ctx, uint8_t *base, uint32_t character, Walker &w, float speed)
  {
    // (fix2: never doubled, per animation: an animation seen with a real step event of its own gets nothing)
    const uint32_t anim = Animation(ctx, base, character);
    if (g_anim_steps.count(anim))
    {
      // (Wed 18 Nov night: log 021, a guard on a search walk made no step in 10 s while four such refusals were
      // counted. The second time his 2.5 m of silent walking ends on a marked animation he HAS walked 5 m without a
      // step: he is covered whatever the mark says. A real step of his own ends it at once.)
      ++w.marked_refusals;
      if (!REXCVAR_GET(pdz_snd_steps_silent_walkers) || w.marked_refusals < 2)
      {
        ++g_why_marked;
        w.supplying = false;
        return;
      }
      if (!w.logged_marked)
      {
        w.logged_marked = true;
        REXLOG_INFO("pdz_all_steps: character {:08X} has walked 5 m without a step event while animation {} plays (it made "
                    "steps for someone before): he gets the game's step routine each stride until a step event of his own "
                    "comes (pdz_snd_steps_silent_walkers)",
                    character, anim);
      }
    }
    uint32_t &n_anim = g_anim_supplied[anim];
    if (n_anim++ == 0 && g_anim_supplied.size() <= 64)
    {
      REXLOG_INFO("pdz_all_steps: animation {} has no step events of its own (first seen on character {:08X}, {:.0f} cm/s, "
                  "{}): the game's step routine is called each stride while it plays ({} animations covered so far)",
                  anim, character, speed, speed >= kRunSpeed ? "running" : "walking", g_anim_supplied.size());
    }
    w.foot = !w.foot;
    g_in_supplied = true;
    {
      rex::CallFrame frame(ctx);
      frame.ctx.r1.u64 = (ctx.r1.u32 - 0x1000) & ~0xFu; // a frame of its own below the caller's
      frame.ctx.r3.u64 = character;
      frame.ctx.r4.u64 = w.foot ? 1 : 0;
      StepScope step(character, true);
      __imp__sub_821ED878(frame.ctx, base);
      step.Done(base);
    }
    g_in_supplied = false;
    ++g_supplied;
    const bool log = REXCVAR_GET(pdz_stealth_guard_footsteps_log);
    if (!w.logged_supplied && (log || g_first_lines < 24))
    {
      w.logged_supplied = true;
      ++g_first_lines;
      REXLOG_INFO("pdz_guard_steps: character {:08X} walks with no step events (animation {}, {:.0f} cm/s, at {:.0f} "
                  "{:.0f} {:.0f}): the game's step routine is called for him each stride",
                  character, Animation(ctx, base, character), speed, w.pos[0], w.pos[1], w.pos[2]);
    }
    if ((g_supplied & (g_supplied - 1)) == 0) // 1, 2, 4, 8, ...
    {
      REXLOG_INFO("pdz_guard_steps: {} steps supplied so far, {} real step events of tracked characters, {} "
                  "characters followed",
                  g_supplied, g_real, g_walkers.size());
    }
  }
} // namespace

namespace pdz::guardsteps
{
  // One NPC update (the sight update hook, before Rare's routine): r3 = the character.
  void Tick(PPCContext &ctx, uint8_t *base, uint32_t character)
  {
    if (REXCVAR_GET(pdz_stealth_guard_footsteps_log) && Plausible(character) && g_type_lines < 16)
    {
      // (fix2 diagnostic: what this update sees; the step code takes only +12 == 2)
      static uint32_t seen[16] = {};
      bool known = false;
      for (uint32_t i = 0; i < g_type_lines; ++i)
      {
        known = known || seen[i] == character;
      }
      if (!known)
      {
        seen[g_type_lines++] = character;
        REXLOG_INFO("pdz_guard_steps: update of character {:08X}: +12 = {}, on = {}", character, Load32(base, character + kType),
                    On() ? 1 : 0);
      }
    }
    if (!On() || !Plausible(character) || !pdz::guestmem::Ok(base, character, kSkeleton + 4) || // (Sun 22 Nov night, audit)
        Load32(base, character + kType) != kNpcType)
    {
      return;
    }
    if (g_walkers.size() > 512)
    {
      g_walkers.clear(); // a new level's characters; the old pointers are gone
      g_anim_steps.clear();
      g_anim_supplied.clear();
    }
    Walker &w = g_walkers[character];
    const uint32_t frame = Load32(base, kFrameCounter);
    float pos[3];
    for (int i = 0; i < 3; ++i)
    {
      pos[i] = LoadFloat(base, character + kPosition + 4 * i);
    }
    if (!std::isfinite(pos[0]) || !std::isfinite(pos[1]) || !std::isfinite(pos[2]))
    {
      w.have = false;
      return;
    }
    if (!w.logged_tick && REXCVAR_GET(pdz_stealth_guard_footsteps_log) && g_first_ticks < 96)
    {
      w.logged_tick = true; // (fix2: which figures this tick reaches at all)
      ++g_first_ticks;
      REXLOG_INFO("pdz_guard_steps: character {:08X} (a human figure) is followed from now (at {:.0f} {:.0f} {:.0f}, animation {})",
                  character, pos[0], pos[1], pos[2], Animation(ctx, base, character));
    }
    if (!w.have || frame < w.frame || frame - w.frame > 240)
    {
      w.have = true;
      w.frame = frame;
      std::memcpy(w.pos, pos, sizeof(pos));
      w.travel = 0.0f;
      return;
    }
    const uint32_t frames = frame - w.frame;
    if (frames == 0)
    {
      return;
    }
    ++g_ticks;
    Tally(base, frame);
    const float elapsed = float(frames) * FrameDt(base);
    const float dx = pos[0] - w.pos[0], dy = pos[1] - w.pos[1], dz = pos[2] - w.pos[2];
    const float d = std::sqrt(dx * dx + dz * dz);
    w.frame = frame;
    std::memcpy(w.pos, pos, sizeof(pos));
    const float speed = d / elapsed;
    if (d > kJump || std::fabs(dy) > 1.2f * d + 10.0f || speed < kMinSpeed || speed > kMaxSpeed)
    {
      g_why_slow += speed < kMinSpeed ? 1 : 0;
      w.moving = false;
      w.travel = 0.0f; // standing, falling, a warp, or carried by something
      return;
    }
    w.moving = true;
    w.running = speed >= kRunSpeed;
    (w.running ? g_run_m : g_walk_m) += double(d) / 100.0;
    if (w.real_seen && float(frame - w.real_frame) * FrameDt(base) < kOwnStepsHold)
    {
      ++g_why_hold;
      w.travel = 0.0f; // his animation makes its own steps
      return;
    }
    w.travel += d;
    const float stride = speed >= kRunSpeed ? kRunStride : kWalkStride;
    if (w.travel < (w.supplying ? stride : kSilentWalk))
    {
      g_why_lead += w.supplying ? 0 : 1;
      return;
    }
    w.travel = 0.0f;
    w.supplying = true;
    Supply(ctx, base, character, w, speed);
  }
} // namespace pdz::guardsteps

// Rare's step routine: a real step event of a followed character stops the supplied steps for him.
REX_HOOK_RAW(sub_821ED878)
{
  if (!g_in_supplied && On())
  {
    const auto it = g_walkers.find(ctx.r3.u32);
    if (it != g_walkers.end())
    {
      Walker &w = it->second;
      w.real_seen = true;
      w.real_frame = Load32(base, kFrameCounter);
      if (g_anim_steps.size() < 4096)
      {
        const uint32_t anim = Animation(ctx, base, ctx.r3.u32);
        if (!g_anim_steps.count(anim) && g_real_anim_lines < 48)
        {
          ++g_real_anim_lines;
          REXLOG_INFO("pdz_all_steps: animation {} is marked as having step events of its own (a real step of character "
                      "{:08X}; {} marked so far)",
                      anim, ctx.r3.u32, g_anim_steps.size() + 1);
        }
        g_anim_steps[anim] = true; // (fix2: this animation makes its own steps)
      }
      w.supplying = false;
      w.travel = 0.0f;
      w.marked_refusals = 0;
      ++g_real;
      if (w.moving)
      {
        ++(w.running ? g_run_real : g_walk_real);
      }
      if (!w.logged_real && REXCVAR_GET(pdz_stealth_guard_footsteps_log))
      {
        w.logged_real = true;
        REXLOG_INFO("pdz_guard_steps: character {:08X} has step events of his own (first one now)", ctx.r3.u32);
      }
    }
  }
  const uint32_t step_char = ctx.r3.u32;
  StepScope step(step_char, false);
  __imp__sub_821ED878(ctx, base);
  step.Done(base);
}

namespace pdz::guardsteps
{
  // From the positioned-sound hook (sub_821FF428, lab_stealth.cpp), after Rare made (or refused) the sound: inside a
  // step event of a character near player one, its line (log only; plain loads).
  void OnStepSound(const uint8_t *base, uint32_t ref, uint32_t sound)
  {
    if (!t_step_char)
    {
      return;
    }
    ++t_step_sounds;
    float d = 0.0f;
    if (!StepLogged(base, t_step_char, d))
    {
      return;
    }
    ++g_step_lines;
    const char *name = Plausible(ref & ~3u) ? reinterpret_cast<const char *>(base + ref) : "";
    const std::string_view cue(name, strnlen(name, 64));
    const bool made = sound >= 0x10000u && sound < 0xF0000000u;
    REXLOG_INFO("pdz_snd_step: character {:08X}, {:.1f} m from Jo: a step event ({}): cue {} -> {}{} (line {} of 600)", t_step_char,
                d / 100.0f, t_step_ours ? "supplied by pdz_snd_all_footsteps" : "the game's own", cue.empty() ? "(empty)" : cue,
                made ? "sound made" : "REFUSED by the sound manager (out of the cue's range, its kind's limit, or no free sound)",
                made ? fmt::format(", range {:.0f} m, loudness {:.2f}", LoadFloat(base, sound + 324) / 100.0f, LoadFloat(base, sound + 332))
                     : std::string(),
                g_step_lines);
  }
} // namespace pdz::guardsteps
