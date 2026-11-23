// pdz_fix_sign_lights (0.9.5, Mon 16 Nov 2026): a neon sign of the level's own walls that draws its own glow goes
// dark when its light is shot out (sign_lights.cpp).
#pragma once

#include <cstdint>

namespace pdz::signfix
{
  // What BeforePart changed in the device, for AfterDraw to put back.
  struct Saved
  {
    uint32_t address = 0; // device address of the glow constant (0 = nothing changed)
    uint32_t xyz[3] = {0, 0, 0};
    uint32_t group = 0;
  };

  // A sign draw whose glow has to change: the draw's index range in parts, each with the factor for its glow (1 =
  // Rare's). One part = the whole draw. Several = a batch that holds more than one sign.
  struct Plan
  {
    struct Part
    {
      uint32_t start = 0, count = 0; // within the draw's index range
      float factor = 1.0f;
    };
    uint32_t constant = 0, group = 0;
    int parts = 0;
    Part part[32];
  };

  // The switch (one load: the only cost of a draw while it is off).
  bool On();
  // D3DDevice_DrawIndexedVertices, at the hook's entry, before anything reads the device. True when the draw is a
  // self-lit sign (or a batch of them) of which at least one has its light out: the hook then draws plan's parts one
  // by one, each between BeforePart and AfterDraw. False (nearly always): the draw goes on untouched.
  bool MakePlan(uint8_t *base, uint32_t device, uint32_t prim, uint32_t base_vertex, uint32_t start_index,
                uint32_t index_count, Plan *plan);
  // Scales the glow constant for this part (nothing for factor 1).
  void BeforePart(uint8_t *base, uint32_t device, const Plan &plan, int part, Saved *saved);
  // After the guest's own draw: the constant is put back (and marked changed, so the next draw gets Rare's value).
  void AfterDraw(uint8_t *base, uint32_t device, const Saved &saved);
  // A level start (Rare's level music setup): what is remembered of the level's signs is dropped.
  void LevelReset();
} // namespace pdz::signfix
