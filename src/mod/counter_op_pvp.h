// Counter-Operative round 4 (item 1), counter_op_pvp.cpp: damage between the two humans.
#pragma once

#include <cstdint>

// The scope the shared damage routine's hook (82546BC8, cut_scenarios.cpp) holds for one hit.
struct PdzCounterOpPvpScope
{
  bool no_mercy = false;   // set the routine's skip byte (entry r1 + 0x6F) for this call
  uint32_t floor_at = 0;   // the agent's health floor (human +7828), lifted for this call
  uint32_t floor_bits = 0;
  uint32_t total_at = 0;   // the victim's participant +0xA10 (damage total), zeroed for this call
  uint32_t total_bits = 0;
};
PdzCounterOpPvpScope PdzCounterOpPvpBegin(uint8_t *base, uint32_t victim, uint32_t attacker);
void PdzCounterOpPvpEnd(uint8_t *base, const PdzCounterOpPvpScope &scope);
bool PdzCounterOpPvpOn(); // pdz_counterop_pvp_damage is not "off"
