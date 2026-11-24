// The player's animated moves above 30 fps (pdz_fix_mount_step, Tue 24 Nov 2026): getting on a turret, and the other
// moves the game plays as an animation with root motion (the mover's animation-driven routine sub_82697B88).
//
// The report: getting on and off turrets (and climbing a hovercraft's ladder on River) looks like stop motion at
// 60 / 120 fps, "almost as at 30 fps". Measured with pdz_mount_log at 60 fps (River, a stationary turret, Rare's own
// Use): during the mount the camera moved every frame, but the player's position (his root transform, character +48,
// and his mover's feet, mover +360) moved only every second frame: 24 of 50 frames stood still, each followed by a
// frame with a double move. sub_82697B88 moves him by the animation's root motion since the last frame
// (sub_821EA010: translation and rotation of the animation layer), and in these moves the animation hands that
// motion out in 1/30 s lumps: a whole lump in one frame, nothing in the next (at 120: nothing in the next three).
//
// The fix: for a player's animation-driven move only, and only while the lumps are seen (at least two empty frames
// among the last six), each lump is handed to Rare's move spread evenly over the 1/30 s it stands for: at 60 fps half
// in its own frame and half in the next. Nothing is added or lost: the same total motion, ended one frame later at
// most; a move whose animation already gives motion every frame is passed through untouched. What is still pending
// when the game stops asking (the move is over) is dropped: under half a lump, a few cm.
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/ppc/func.h>
#include "hook_profile.h" // pdz_hook_profile (must follow <rex/hook.h>)

#include "render/game_pace.h" // EffectiveHz

REXCVAR_DEFINE_BOOL(pdz_fix_mount_step, true, "Gameplay",
                    "Getting on a turret and the player's other animated moves are smooth at 60 / 120 fps: the "
                    "animation hands out its movement in 1/30 s lumps (a move in one frame, none in the next), which "
                    "looked like stop motion; each lump is now spread evenly over the frames of its 1/30 s. The move "
                    "takes as long and ends in the same place. Off = Rare's lumps")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);

REX_EXTERN(__imp__sub_821EA010); // an animation layer's root motion since the last update (controller, &translation, &rotation, layer)

namespace pdz::playermove
{
  bool InUpdate(uint32_t *mover, float *dt); // player_move.cpp: inside a PLAYER's mover update on this thread
}

namespace
{
  constexpr float kTick = 1.0f / 30.0f;
  struct Pool
  {
    uint32_t mover = 0;
    float t[3] = {0, 0, 0};
    float q[4] = {0, 0, 0, 1};
    float window = 0.0f;
    uint32_t history = 0; // bit 0 = the newest frame gave no motion
    bool holding = false;
    uint32_t lumps = 0, frames = 0, moves = 0;
    std::chrono::steady_clock::time_point last{};
  };
  Pool g_pool[2];

  float LoadF(const uint8_t *base, uint32_t a)
  {
    const uint8_t *p = base + a;
    const uint32_t v = (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
    float f;
    std::memcpy(&f, &v, 4);
    return f;
  }
  void StoreF(uint8_t *base, uint32_t a, float f)
  {
    uint32_t v;
    std::memcpy(&v, &f, 4);
    base[a] = uint8_t(v >> 24), base[a + 1] = uint8_t(v >> 16), base[a + 2] = uint8_t(v >> 8), base[a + 3] = uint8_t(v);
  }
  void Mul(const float a[4], const float b[4], float out[4]) // quaternions (x, y, z, w)
  {
    const float x = a[3] * b[0] + a[0] * b[3] + a[1] * b[2] - a[2] * b[1];
    const float y = a[3] * b[1] - a[0] * b[2] + a[1] * b[3] + a[2] * b[0];
    const float z = a[3] * b[2] + a[0] * b[1] - a[1] * b[0] + a[2] * b[3];
    const float w = a[3] * b[3] - a[0] * b[0] - a[1] * b[1] - a[2] * b[2];
    out[0] = x, out[1] = y, out[2] = z, out[3] = w;
  }
  void Reset(Pool &p)
  {
    p.t[0] = p.t[1] = p.t[2] = 0.0f;
    p.q[0] = p.q[1] = p.q[2] = 0.0f;
    p.q[3] = 1.0f;
    p.window = 0.0f;
    p.holding = false;
  }
} // namespace

REX_HOOK_RAW(sub_821EA010)
{
  const uint32_t out_t = ctx.r4.u32, out_q = ctx.r5.u32, lr = uint32_t(ctx.lr);
  __imp__sub_821EA010(ctx, base);
  uint32_t mover = 0;
  float dt = 0.0f;
  // Only the call of the mover's animation-driven routine (sub_82697B88 .. the next routine 82698258), for a player.
  if (lr < 0x82697B88u || lr >= 0x82698258u || !pdz::playermove::InUpdate(&mover, &dt) || !mover)
  {
    return;
  }
  Pool *p = nullptr;
  for (Pool &c : g_pool)
  {
    if (c.mover == mover)
    {
      p = &c;
      break;
    }
  }
  const auto now = std::chrono::steady_clock::now();
  if (!p)
  {
    p = g_pool[0].last <= g_pool[1].last ? &g_pool[0] : &g_pool[1];
    *p = Pool{};
    p->mover = mover;
  }
  if (std::chrono::duration<double>(now - p->last).count() > 0.25)
  {
    // A new move (the game had stopped asking): nothing is carried over from the last one.
    if (p->frames && REXCVAR_GET(pdz_fix_mount_step))
    {
      REXLOG_INFO("pdz_mount_step: mover {:08X}: the last animated move gave {} lump(s) of root motion over {} frame(s) "
                  "(spread over the frames in between; pdz_fix_mount_step)", mover, p->lumps, p->frames);
    }
    Reset(*p);
    p->history = 0;
    p->lumps = p->frames = 0;
    ++p->moves;
  }
  p->last = now;
  float t[3] = {LoadF(base, out_t), LoadF(base, out_t + 4), LoadF(base, out_t + 8)};
  float q[4] = {LoadF(base, out_q), LoadF(base, out_q + 4), LoadF(base, out_q + 8), LoadF(base, out_q + 12)};
  if (!std::isfinite(t[0] + t[1] + t[2] + q[0] + q[1] + q[2] + q[3]))
  {
    Reset(*p);
    return;
  }
  const bool empty = t[0] * t[0] + t[1] * t[1] + t[2] * t[2] < 1e-10f && std::fabs(q[3]) > 1.0f - 1e-7f;
  p->history = ((p->history << 1) | (empty ? 1u : 0u)) & 0x3Fu;
  int empties = 0;
  for (uint32_t h = p->history; h; h >>= 1)
  {
    empties += int(h & 1u);
  }
  const uint32_t hz = pdz::pace::EffectiveHz();
  const bool on = REXCVAR_GET(pdz_fix_mount_step) && hz > 45 && dt > 0.0005f && dt < 0.03f;
  const bool lumpy = empties >= 2 && p->history != 0x3Fu;
  if (!on || (!lumpy && !p->holding))
  {
    Reset(*p);
    return; // Rare's values as they are
  }
  ++p->frames;
  if (!empty)
  {
    ++p->lumps;
    p->t[0] += t[0], p->t[1] += t[1], p->t[2] += t[2];
    float m[4];
    Mul(p->q, q, m);
    std::memcpy(p->q, m, sizeof(m));
    p->window = kTick;
    p->holding = true;
  }
  // What this frame gets: its share of the 1/30 s the pending motion stands for (all of it when the motion comes
  // every frame again, or when the window is used up).
  const float frac = (!lumpy || p->window <= dt) ? 1.0f : dt / p->window;
  float give_t[3] = {p->t[0] * frac, p->t[1] * frac, p->t[2] * frac};
  float give_q[4] = {0, 0, 0, 1};
  {
    float s[4] = {p->q[0], p->q[1], p->q[2], p->q[3]};
    if (s[3] < 0.0f)
    {
      s[0] = -s[0], s[1] = -s[1], s[2] = -s[2], s[3] = -s[3];
    }
    // identity -> s by frac (the lumps are a few degrees: a normalised straight blend is exact enough)
    give_q[0] = s[0] * frac, give_q[1] = s[1] * frac, give_q[2] = s[2] * frac, give_q[3] = 1.0f + (s[3] - 1.0f) * frac;
    const float n = std::sqrt(give_q[0] * give_q[0] + give_q[1] * give_q[1] + give_q[2] * give_q[2] + give_q[3] * give_q[3]);
    if (n > 1e-6f)
    {
      give_q[0] /= n, give_q[1] /= n, give_q[2] /= n, give_q[3] /= n;
    }
    else
    {
      give_q[0] = give_q[1] = give_q[2] = 0.0f, give_q[3] = 1.0f;
    }
    // what stays pending: give^-1 * s
    const float inv[4] = {-give_q[0], -give_q[1], -give_q[2], give_q[3]};
    float rest[4];
    Mul(inv, s, rest);
    std::memcpy(p->q, rest, sizeof(rest));
  }
  p->t[0] -= give_t[0], p->t[1] -= give_t[1], p->t[2] -= give_t[2];
  p->window = std::max(0.0f, p->window - dt);
  if (frac >= 1.0f)
  {
    Reset(*p);
  }
  StoreF(base, out_t, give_t[0]);
  StoreF(base, out_t + 4, give_t[1]);
  StoreF(base, out_t + 8, give_t[2]);
  StoreF(base, out_q, give_q[0]);
  StoreF(base, out_q + 4, give_q[1]);
  StoreF(base, out_q + 8, give_q[2]);
  StoreF(base, out_q + 12, give_q[3]);
}
