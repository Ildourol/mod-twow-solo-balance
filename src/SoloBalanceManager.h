// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "SoloBalanceConfig.h"
class Map;
class Player;
class Creature;
class Unit;
namespace SoloBalance
{
std::shared_ptr<MapState> State(Map* map);
void Destroy(Map* map);
Player* Anchor(Map* map);
void BindAnchor(Player* player, MapState& state);
Scope Content(Map* map, Settings const& config);
bool Eligible(Creature* creature, Player* anchor, Settings const& config);
Snapshot* Ensure(Creature* creature, Player* anchor, MapState& state, Settings const& config);
Snapshot* Find(Creature* creature, MapState& state);
void Restore(Creature* creature, Snapshot const& snapshot);
void Update(Map* map, uint32 diff);
void Leave(Map* map, Player* player);
void Death(Unit* unit);
bool Fighting(Player* player, MapState& state);
Player* CreditedPlayer(Unit* unit, bool includePet);
}
