// Mount / dismount frame log (Tue 24 Nov 2026): a diagnostic, no behaviour change (pdz_mount_log, default off).
//
// The report: getting on and off a turret (and climbing a hovercraft's ladder on River) looks like stop motion at
// 60 / 120 fps, "almost as at 30 fps". While the log is on, every update of a player's camera manager (Rare's
// 8220C0D8, once per player per game frame) writes one line: the frame's step, the view position the manager made
// (manager +0x3B4), the player's own position (+48), what he is attached to (+0x5C) and his mover's mode byte. A value
// that moves only every second frame at 60 (every fourth at 120) is stepping at Rare's 30 Hz tick.
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>

#include <rex/cvar.h>
#include <rex/logging.h>

#include "guest_readable_Sat21Nov.h" // pdz::guestmem::Ok

REXCVAR_DEFINE_BOOL(pdz_mount_log, false, "Diagnostics",
                    "Log every camera update of each player frame by frame (the view position, the player's position, "
                    "what he is attached to): for the stop-motion look of getting on and off turrets and hovercraft. "
                    "No behaviour change")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);

namespace
{
  constexpr uint32_t kPlayerVtable = 0x820C7DE0, kManagerOf = 0x19A4, kParent = 0x5C, kPosition = 48;
  struct Seen
  {
    uint32_t character = 0, mover = 0, manager = 0;
  };
  Seen g_seen[2];
  uint64_t g_lines = 0;
  std::chrono::steady_clock::time_point g_prev[2];

  uint32_t Load32(const uint8_t *base, uint32_t a)
  {
    const uint8_t *p = base + a;
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
  }
  float LoadF(const uint8_t *base, uint32_t a)
  {
    const uint32_t v = Load32(base, a);
    float f;
    std::memcpy(&f, &v, 4);
    return f;
  }
} // namespace

namespace pdz::mountlog
{
  // From player_move.cpp's hook of the mover update (player characters only): who the players are.
  void OnPlayerMove(const uint8_t *base, uint32_t character, uint32_t mover)
  {
    if (!REXCVAR_GET(pdz_mount_log) || !pdz::guestmem::Ok(base, character, kManagerOf + 4) ||
        Load32(base, character) != kPlayerVtable)
    {
      return;
    }
    const uint32_t manager = Load32(base, character + kManagerOf);
    for (Seen &s : g_seen)
    {
      if (s.character == character || s.character == 0 || s.manager == manager)
      {
        s = Seen{character, mover, manager};
        return;
      }
    }
  }

  // From the camera manager update's hook (counter_op_spectate_Sat21Nov.cpp), after Rare's update.
  void OnCameraUpdate(const uint8_t *base, uint32_t manager, double step)
  {
    if (!REXCVAR_GET(pdz_mount_log))
    {
      return;
    }
    for (int i = 0; i < 2; ++i)
    {
      const Seen &s = g_seen[i];
      if (s.manager != manager || !s.character || !pdz::guestmem::Ok(base, manager, 0x3C0) ||
          !pdz::guestmem::Ok(base, s.character, kManagerOf + 4) || Load32(base, s.character) != kPlayerVtable ||
          !pdz::guestmem::Ok(base, s.mover, 608))
      {
        continue;
      }
      const auto now = std::chrono::steady_clock::now();
      const double real_ms = std::chrono::duration<double, std::milli>(now - g_prev[i]).count();
      g_prev[i] = now;
      if (++g_lines > 200000)
      {
        return;
      }
      REXLOG_INFO("pdz_mount: p{} step {:.5f} real {:.2f} ms | view {:.2f} {:.2f} {:.2f} | body {:.2f} {:.2f} {:.2f} | "
                  "feet {:.2f} {:.2f} {:.2f} | on {:08X} mode {} yaw {:.2f}",
                  i + 1, step, real_ms < 10000.0 ? real_ms : 0.0, LoadF(base, manager + 0x3B4), LoadF(base, manager + 0x3B8),
                  LoadF(base, manager + 0x3BC), LoadF(base, s.character + kPosition), LoadF(base, s.character + kPosition + 4),
                  LoadF(base, s.character + kPosition + 8), LoadF(base, s.mover + 360), LoadF(base, s.mover + 364),
                  LoadF(base, s.mover + 368), Load32(base, s.character + kParent), base[s.mover + 605],
                  LoadF(base, s.character + 0x1670));
      return;
    }
  }
} // namespace pdz::mountlog
