// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "SoloBalanceConfig.h"
class Unit;
class Player;
class Aura;
class Creature;
namespace SoloBalance
{
void AuraApplied(Unit* target, Aura* aura);
void ProcessAuras(Player* player, MapState& state, Settings const& config);
void LimitAdds(Player* player, MapState& state, Settings const& config);
}
