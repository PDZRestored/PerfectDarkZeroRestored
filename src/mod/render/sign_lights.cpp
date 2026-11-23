// pdz_fix_sign_lights (0.9.5, Mon 16 Nov 2026): Rare's bug. Shooting a neon sign on a wall puts its light out, but
// the sign's face keeps glowing (Subway: the blue sign with the scrolling yellow characters; the same on the Xbox 360).
//
// What Rare's game does (measured, research notes: stretch\neon_sign_Mon16Nov.md):
// - A shot on a "light" surface runs sub_825F3978: it picks the scene light with the best 1 - d^2 / R^2 from the hit
//   point among the lights that can be shot out (sub_825F3CD8) and switches that light off. Nothing else.
// - A sign whose face is only lit by its light goes dark with it (the white sign with red characters beside the blue
//   one). No code does that: the face is an ordinary lit surface.
// - The scrolling and the flipbook signs are background mesh too, but their materials add a glow of their own: the
//   pixel shader adds (glow texture x a constant) on top of the lit colour. Nothing links that constant to a light,
//   so the face glows on with its light dead.
//
// The fix: when such a sign face is drawn, its glow constant is scaled by the brightness of ITS light: the light
// Rare's own pick rule gives for the face (the best 1 - d^2 / R^2 at the face's centre among the lights that can be
// shot out). With the light on nothing changes (the scale is 1 and nothing is written). With it out the face is left
// with its lit colour only, which is how the lit-only signs look when their light is out; the glow fades with Rare's
// own fade of the light and comes back if the light does. No new art, no table of signs: the light is found from
// Rare's light table, the face from the draw's own vertices.
//
// Where: the D3DDevice_DrawIndexedVertices hook (shadow_hooks.cpp), before anything reads the device, so the emulated
// and the native renderer both draw the changed constant; it is put back after the guest's draw. A draw that is not a
// sign costs one load and one compare.
#include "sign_lights.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <mutex>

#include <rex/cvar.h>
#include <rex/logging.h>

#include "shadow_record.h"
#include "native/native.h"
#include "../guest_readable_Sat21Nov.h" // Sun 22 Nov night (audit): pdz::guestmem::Ok

REXCVAR_DEFINE_BOOL(pdz_fix_sign_lights, false, "Fixes",
                    "WORK IN PROGRESS, off (not proven by a picture test). Rare's bug: a neon sign that draws its own glow (the scrolling and the changing signs of Subway "
                    "and the Nightclub) kept glowing after its light was shot out. On: the sign's glow follows the "
                    "light Rare's own shot rule gives for it, so the face goes dark with the light. Off: as on the "
                    "Xbox 360")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_sign_log, false, "Diagnostics",
                    "Log every self-lit sign face the first time it is drawn in a level: where it is, its glow "
                    "constant, and the light Rare's shot rule gives for it (pdz_sign lines)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);

namespace pdz::signfix
{
  namespace
  {
    using namespace shadow;

    // The self-lit sign materials' pixel shaders (XXH3-64 of the microcode, as the shader cache names them) and the
    // float constant that scales their glow texture (read from the translated shaders, 16 Nov).
    // Only draws with the listed vertex shader too: the face's centre is read from the vertices, and that layout
    // (44 bytes, the position as three floats at +8, in world space) is this vertex shader's.
    // Not covered: the "changing" signs (pixel shader d6ab85f7d911f960, glow constant c9). They are drawn with their
    // own transform and packed vertices, so their place in the level is not in the draw's vertices.
    struct SignShader
    {
      uint64_t hash;    // pixel shader
      uint64_t vs_hash; // vertex shader: as the shader cache names it (the record's hash) ...
      uint64_t vs_bound; // ... and as the device's bound object reads at the draw hook (the same shader, bound to its
                         // vertex declaration)
      uint32_t glow;    // PS float constant index: xyz = the glow colour scale
      const char *name;
    };
    constexpr SignShader kShaders[] = {
        {0x801eb98aa91e25e1ull, 0xe97d2be02e01714cull, 0x33e4c7abf9352dc7ull, 7, "scrolling sign"},
    };

    // Rare's scene light table (lab_dark_patches.cpp has the same constants).
    constexpr uint32_t kLightArray = 0x82E19AE0, kLightCount = 0x82E3C5F4, kMaxLights = 1024;
    constexpr uint32_t kStateIndex = 0x82E1BA20; // 0 / 1: which of the two state words is current

    uint32_t Load32(const uint8_t *p)
    {
      return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
    }
    uint32_t Load32(const uint8_t *base, uint32_t a) { return Load32(base + a); }
    float LoadF(const uint8_t *p)
    {
      const uint32_t v = Load32(p);
      float f;
      std::memcpy(&f, &v, 4);
      return f;
    }
    float LoadF(const uint8_t *base, uint32_t a) { return LoadF(base + a); }
    void Store32(uint8_t *base, uint32_t a, uint32_t v)
    {
      uint8_t *p = base + a;
      p[0] = uint8_t(v >> 24), p[1] = uint8_t(v >> 16), p[2] = uint8_t(v >> 8), p[3] = uint8_t(v);
    }
    void StoreF(uint8_t *base, uint32_t a, float f)
    {
      uint32_t v;
      std::memcpy(&v, &f, 4);
      Store32(base, a, v);
    }
    bool Ptr(uint32_t a) { return a >= 0x10000u && a < 0xFFFF0000u; }

    // The last pixel shader object looked at on this thread and what it is (-1 = not a sign). A shader object's
    // address can be used again after a level, so the answer is kept only while the same object stays bound.
    thread_local uint32_t t_ps = 0;
    thread_local int t_kind = -1;

    // Rare's pick for a point (sub_825F3CD8), but over the lights whether they are on or not: the light with the
    // best 1 - d^2 / R^2 among the scene lights of type 0 / 2 that can be shot out and that no prop owns.
    uint32_t PickLight(const uint8_t *base, const float p[3], float *score_out, uint32_t *index_out)
    {
      const uint32_t count = std::min(Load32(base, kLightCount), kMaxLights);
      uint32_t best = 0, best_i = 0;
      float best_score = 0.0f;
      for (uint32_t i = 0; i < count; ++i)
      {
        const uint32_t a = Load32(base, kLightArray + 4 * i);
        if (!Ptr(a) || !pdz::guestmem::Ok(base, a, 0x59C)) // Sun 22 Nov night (audit): the light is there
        {
          continue;
        }
        const uint32_t type = Load32(base, a + 0x2A8);
        if ((type != 0 && type != 2) || Load32(base, a + 0x32C) != 0 || Load32(base, a + 0x338) != 0)
        {
          continue;
        }
        const float r = LoadF(base, a + 0x2AC);
        if (!(r > 1.0f))
        {
          continue;
        }
        const float dx = LoadF(base, a + 0x2D0) - p[0], dy = LoadF(base, a + 0x2D4) - p[1], dz = LoadF(base, a + 0x2D8) - p[2];
        const float score = 1.0f - (dx * dx + dy * dy + dz * dz) / (r * r);
        if (score > best_score)
        {
          best_score = score, best = a, best_i = i;
        }
      }
      *score_out = best_score;
      *index_out = best_i;
      return best;
    }

    // One draw of a sign material holds one sign (a quad, or a few dozen quads) or several signs of the same material
    // batched together (Subway's blue scrolling sign shares a strip with others). The draw's indices are cut into
    // runs of faces that lie together; each run is one sign with its own light. Whole rows of lit slits are batched
    // too (574 and 1042 indices): no single light stands for those, they are left alone.
    constexpr uint32_t kMaxIndices = 256;
    constexpr int kMaxRuns = 32;
    constexpr float kMaxSize = 600.0f; // cm: the largest box (diagonal) that still counts as one sign
    constexpr float kGap = 150.0f;     // cm: a vertex further than this from the run's box starts another sign

    struct Run
    {
      uint32_t start = 0, count = 0; // within the draw's indices
      float lo[3] = {0, 0, 0}, hi[3] = {0, 0, 0};
      float pos[3] = {0, 0, 0};
      uint32_t light = 0; // 0: none reaches it (or the run is too big for one sign): Rare's glow
      bool dark_logged = false;
    };
    struct Entry
    {
      uint32_t key[4] = {0, 0, 0, 0}; // index buffer object, base vertex, start index, index count
      bool refused = true;
      int runs = 0;
      Run run[kMaxRuns];
    };
    constexpr int kEntries = 512;
    std::mutex g_lock;
    Entry g_entries[kEntries];
    int g_entry_n = 0;
    std::atomic<bool> g_reset{false};
    std::atomic<bool> g_other_logged{false}; // pdz_sign_log: once per level

    float BoxDistance(const Run &r, const float p[3])
    {
      float d2 = 0.0f;
      for (int i = 0; i < 3; ++i)
      {
        const float d = p[i] < r.lo[i] ? r.lo[i] - p[i] : p[i] > r.hi[i] ? p[i] - r.hi[i] : 0.0f;
        d2 += d * d;
      }
      return std::sqrt(d2);
    }

    // Reads the draw's vertices and cuts it into runs. The sign materials' vertices are 44 bytes with the position
    // as three floats at +8 (their vertex shader's fetch), stream 0, in world space. Returns why not, or nullptr.
    const char *BuildRuns(const uint8_t *base, uint32_t device, uint32_t prim, uint32_t base_vertex, uint32_t start_index,
                          uint32_t index_count, Entry *entry)
    {
      const uint32_t ib = Load32(base, device + dev::kIndexBuffer);
      const uint32_t vb = Load32(base, device + dev::kStreams + 4);
      if (!Ptr(ib) || !Ptr(vb) || index_count == 0 || !pdz::guestmem::Ok(base, ib, dev::kVbBase + 4) ||
          !pdz::guestmem::Ok(base, vb, std::max(dev::kVbBase, dev::kVbSize) + 4)) // Sun 22 Nov night (audit)
      {
        return "no buffers";
      }
      const bool index32 = (Load32(base, ib) & 0x80000000u) != 0;
      const uint32_t index_size = index32 ? 4u : 2u;
      const uint32_t ib_base = Load32(base, ib + dev::kVbBase) & 0x1FFFFFFEu;
      const uint32_t vb_base = (Load32(base, vb + dev::kVbBase) & 0x1FFFFFFCu) + Load32(base, device + dev::kStreams);
      const uint32_t vb_size = Load32(base, vb + dev::kVbSize);
      constexpr uint32_t kStride = 44, kPosition = 8;
      if (uint64_t(ib_base) + (uint64_t(start_index) + index_count) * index_size > 0x20000000ull)
      {
        return "its indices could not be read";
      }
      const uint8_t *indices = native::PhysPtr(ib_base + start_index * index_size);
      if (!indices)
      {
        return "its indices could not be read";
      }
      static thread_local uint32_t idx[kMaxIndices];
      static thread_local float pos[kMaxIndices][3];
      if (index_count > kMaxIndices)
      {
        return "a batch of many faces in one draw";
      }
      for (uint32_t i = 0; i < index_count; ++i)
      {
        const uint32_t index = index32 ? Load32(indices + 4 * i) : uint32_t((indices[2 * i] << 8) | indices[2 * i + 1]);
        idx[i] = index;
        if (index == (index32 ? 0xFFFFFFFFu : 0xFFFFu))
        {
          return "a strip with restart indices";
        }
        const uint64_t offset = (uint64_t(base_vertex) + index) * kStride + kPosition;
        if (offset + 12 > vb_size || uint64_t(vb_base) + offset + 12 > 0x20000000ull)
        {
          return "its vertices could not be read";
        }
        const uint8_t *v = native::PhysPtr(vb_base + uint32_t(offset));
        if (!v)
        {
          return "its vertices could not be read";
        }
        for (int k = 0; k < 3; ++k)
        {
          pos[i][k] = LoadF(v + 4 * k);
          if (!std::isfinite(pos[i][k]) || std::fabs(pos[i][k]) > 1e6f)
          {
            return "its vertices are not positions";
          }
        }
      }
      Run *cur = nullptr;
      for (uint32_t i = 0; i < index_count; ++i)
      {
        const float *p = pos[i];
        // Where another sign may start. A strip: only at a joining (degenerate) triangle, never inside a face (a
        // real triangle ties its corners together however far apart they are) and not before the run has its first
        // triangle. A list: only between triangles, and only when the whole next triangle is away from the run.
        bool cut = false;
        if (cur && prim == 6)
        {
          const bool real = i >= 2 && idx[i] != idx[i - 1] && idx[i] != idx[i - 2] && idx[i - 1] != idx[i - 2];
          cut = cur->count >= 3 && !real && BoxDistance(*cur, p) > kGap;
        }
        else if (cur && i % 3 == 0 && i + 2 < index_count)
        {
          cut = BoxDistance(*cur, p) > kGap && BoxDistance(*cur, pos[i + 1]) > kGap && BoxDistance(*cur, pos[i + 2]) > kGap;
        }
        if (!cur || cut)
        {
          if (entry->runs >= kMaxRuns)
          {
            return "more separate groups of faces than the fix handles";
          }
          cur = &entry->run[entry->runs++];
          *cur = Run{};
          cur->start = i;
          for (int k = 0; k < 3; ++k)
          {
            cur->lo[k] = cur->hi[k] = p[k];
          }
        }
        cur->count = i + 1 - cur->start;
        for (int k = 0; k < 3; ++k)
        {
          cur->lo[k] = std::min(cur->lo[k], p[k]), cur->hi[k] = std::max(cur->hi[k], p[k]);
        }
      }
      return nullptr;
    }
  } // namespace

  bool On() { return REXCVAR_GET(pdz_fix_sign_lights) || REXCVAR_GET(pdz_sign_log); }

  void LevelReset()
  {
    g_reset.store(true, std::memory_order_relaxed);
    g_other_logged.store(false, std::memory_order_relaxed);
  }

  bool MakePlan(uint8_t *base, uint32_t device, uint32_t prim, uint32_t base_vertex, uint32_t start_index,
                uint32_t index_count, Plan *plan)
  {
    const uint32_t ps = Load32(base, device + dev::kPixelShader);
    if (ps != t_ps)
    {
      t_ps = ps;
      t_kind = -1;
      if (Ptr(ps))
      {
        if (const ShaderEntry *e = FindOrReadShader(base, ps, false))
        {
          for (int k = 0; k < int(std::size(kShaders)); ++k)
          {
            if (e->hash == kShaders[k].hash)
            {
              t_kind = k;
            }
          }
          // pdz_sign_log: the self-lit material this fix does not cover, once per level.
          if (e->hash == 0xd6ab85f7d911f960ull && REXCVAR_GET(pdz_sign_log) &&
              !g_other_logged.exchange(true, std::memory_order_relaxed))
          {
            REXLOG_INFO("pdz_sign: a changing sign (pixel shader d6ab85f7d911f960, {} indices) is drawn in this level: "
                        "not covered by pdz_fix_sign_lights, it stays lit as in Rare's game",
                        index_count);
          }
        }
      }
    }
    if (t_kind < 0 || (prim != 6 && prim != 4))
    {
      return false;
    }
    const SignShader &shader = kShaders[t_kind];
    const uint32_t constant = device + dev::kPsAluConstants + 16 * shader.glow;
    const uint32_t key[4] = {Load32(base, device + dev::kIndexBuffer), base_vertex, start_index, index_count};
    const uint32_t state_index = Load32(base, kStateIndex) & 1;
    std::lock_guard<std::mutex> hold(g_lock);
    if (g_reset.exchange(false, std::memory_order_relaxed))
    {
      g_entry_n = 0;
    }
    Entry *entry = nullptr;
    for (int i = 0; i < g_entry_n; ++i)
    {
      Entry &e = g_entries[i];
      if (e.key[0] == key[0] && e.key[1] == key[1] && e.key[2] == key[2] && e.key[3] == key[3])
      {
        entry = &e;
        break;
      }
    }
    if (!entry)
    {
      if (g_entry_n >= kEntries)
      {
        return false; // more draws of these materials than the table holds: the rest stay as Rare's
      }
      entry = &g_entries[g_entry_n++];
      *entry = Entry{};
      std::memcpy(entry->key, key, sizeof(key));
      // The vertex layout is this vertex shader's: another one is left alone.
      const uint32_t vs = Load32(base, device + dev::kVertexShader);
      const ShaderEntry *e = Ptr(vs) ? FindOrReadShader(base, vs, true) : nullptr;
      const char *why = nullptr;
      if (!e || (e->hash != shader.vs_hash && e->hash != shader.vs_bound))
      {
        why = "another vertex shader";
      }
      else if (index_count > kMaxIndices)
      {
        why = "a batch of many faces in one draw";
      }
      else
      {
        why = BuildRuns(base, device, prim, base_vertex, start_index, index_count, entry);
      }
      if (why)
      {
        entry->runs = 0;
        if (REXCVAR_GET(pdz_sign_log))
        {
          REXLOG_INFO("pdz_sign: draw {} of the {} material, {} indices (prim {}, base vertex {}, start {}): {}: left as "
                      "in Rare's game",
                      g_entry_n, shader.name, index_count, prim, base_vertex, start_index, why);
        }
        return false;
      }
      entry->refused = false;
      for (int i = 0; i < entry->runs; ++i)
      {
        Run &r = entry->run[i];
        float size = 0.0f;
        for (int k = 0; k < 3; ++k)
        {
          r.pos[k] = 0.5f * (r.lo[k] + r.hi[k]);
          size += (r.hi[k] - r.lo[k]) * (r.hi[k] - r.lo[k]);
        }
        size = std::sqrt(size);
        float score = 0.0f;
        uint32_t index = 0;
        r.light = size <= kMaxSize ? PickLight(base, r.pos, &score, &index) : 0;
        if (REXCVAR_GET(pdz_sign_log))
        {
          if (r.light)
          {
            REXLOG_INFO("pdz_sign: draw {} sign {} of {}: {} at ({:.0f} {:.0f} {:.0f}), {:.0f} cm across, indices {}+{} of "
                        "{}, glow c{} = ({:.2f} {:.2f} {:.2f}): its light by Rare's shot rule is light #{} at ({:.0f} "
                        "{:.0f} {:.0f}), radius {:.0f}, score {:.2f}, state {}",
                        g_entry_n, i + 1, entry->runs, shader.name, r.pos[0], r.pos[1], r.pos[2], size, r.start, r.count,
                        index_count, shader.glow, LoadF(base, constant), LoadF(base, constant + 4),
                        LoadF(base, constant + 8), index, LoadF(base, r.light + 0x2D0), LoadF(base, r.light + 0x2D4),
                        LoadF(base, r.light + 0x2D8), LoadF(base, r.light + 0x2AC), score,
                        Load32(base, r.light + 0x590 + 4 * state_index));
          }
          else
          {
            REXLOG_INFO("pdz_sign: draw {} sign {} of {}: {} at ({:.0f} {:.0f} {:.0f}), {:.0f} cm across, indices {}+{} of "
                        "{}, glow c{} = ({:.2f} {:.2f} {:.2f}): {}: it stays lit, as in Rare's game",
                        g_entry_n, i + 1, entry->runs, shader.name, r.pos[0], r.pos[1], r.pos[2], size, r.start, r.count,
                        index_count, shader.glow, LoadF(base, constant), LoadF(base, constant + 4),
                        LoadF(base, constant + 8),
                        size > kMaxSize ? "too big for one sign" : "no light that can be shot out reaches it");
          }
        }
      }
    }
    if (entry->refused || !REXCVAR_GET(pdz_fix_sign_lights))
    {
      return false;
    }
    // The glow of each sign follows its light: 1 while it is on (or fading in), else the light's own brightness
    // (Rare's fade), down to nothing.
    float factor[kMaxRuns];
    bool any = false;
    for (int i = 0; i < entry->runs; ++i)
    {
      Run &r = entry->run[i];
      factor[i] = 1.0f;
      // Sun 22 Nov night (audit): the cached light is kept until the next level setup: read only while it is there.
      if (!r.light || !pdz::guestmem::Ok(base, r.light, 0x59C))
      {
        continue;
      }
      const uint32_t state = Load32(base, r.light + 0x590 + 4 * state_index);
      if (state == 1 || state == 2)
      {
        r.dark_logged = false;
        continue;
      }
      factor[i] = state == 0 ? 0.0f : std::clamp(LoadF(base, r.light + 0x598), 0.0f, 0.999f);
      any = true;
      if (!r.dark_logged)
      {
        r.dark_logged = true;
        REXLOG_INFO("pdz_fix_sign_lights: the {} at ({:.0f} {:.0f} {:.0f}) goes dark with its light at ({:.0f} {:.0f} "
                    "{:.0f}) (the light's state {}, brightness {:.2f})",
                    shader.name, r.pos[0], r.pos[1], r.pos[2], LoadF(base, r.light + 0x2D0), LoadF(base, r.light + 0x2D4),
                    LoadF(base, r.light + 0x2D8), state, LoadF(base, r.light + 0x598));
      }
    }
    if (!any)
    {
      return false; // every light is on: Rare's draw, untouched
    }
    plan->constant = constant;
    plan->group = shader.glow / 4;
    plan->parts = 0;
    // Neighbouring runs with the same factor are drawn together. A strip is cut at an even index, so the winding of
    // every triangle stays as it was (a run that starts at an odd index takes the index before it too: the extra
    // triangles are the degenerate ones that join two faces of the strip). A part runs up to the next part's run, so
    // the joining triangles are drawn once more at most, and they have no area.
    for (int i = 0; i < entry->runs; ++i)
    {
      const Run &r = entry->run[i];
      if (plan->parts && plan->part[plan->parts - 1].factor == factor[i])
      {
        Plan::Part &last = plan->part[plan->parts - 1];
        last.count = r.start + r.count - last.start;
        continue;
      }
      Plan::Part &part = plan->part[plan->parts++];
      part.start = prim == 6 ? (r.start & ~1u) : r.start;
      part.count = r.start + r.count - part.start;
      part.factor = factor[i];
    }
    return true;
  }

  void BeforePart(uint8_t *base, uint32_t device, const Plan &plan, int part, Saved *saved)
  {
    *saved = Saved{};
    const float factor = plan.part[part].factor;
    if (factor >= 1.0f)
    {
      return;
    }
    saved->address = plan.constant;
    saved->group = plan.group;
    for (int i = 0; i < 3; ++i)
    {
      saved->xyz[i] = Load32(base, plan.constant + 4 * i);
      StoreF(base, plan.constant + 4 * i, LoadF(base, plan.constant + 4 * i) * factor);
    }
    // The pixel shader constant group is marked changed, as the game's own setters do (+24: bit 63 - group).
    uint8_t *mask = base + device + dev::kDirtyMasks + 8;
    mask[saved->group / 8] |= uint8_t(0x80u >> (saved->group % 8));
  }

  void AfterDraw(uint8_t *base, uint32_t device, const Saved &saved)
  {
    if (!saved.address)
    {
      return;
    }
    for (int i = 0; i < 3; ++i)
    {
      Store32(base, saved.address + 4 * i, saved.xyz[i]);
    }
    uint8_t *mask = base + device + dev::kDirtyMasks + 8;
    mask[saved.group / 8] |= uint8_t(0x80u >> (saved.group % 8));
  }
} // namespace pdz::signfix
