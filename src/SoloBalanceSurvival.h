// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "SoloBalanceConfig.h"
class Unit;
class Player;
namespace SoloBalance
{
void Damage(Unit* attacker, Unit* victim, uint32& damage);
void Heal(Unit* healer, Unit* target, uint32& heal);
void Sustain(Player* player, MapState& state, Settings const& config, uint32 diff);
}
