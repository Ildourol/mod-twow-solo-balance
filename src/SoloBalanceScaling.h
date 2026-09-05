// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "SoloBalanceConfig.h"
namespace SoloBalance
{
double Clamp(double value, double low, double high);
uint32 BoundedUInt(double value, uint32 minimum = 0);
double OnePlayerCurve(double capacity, Settings const& config);
uint32 ScaledLevel(uint32 nativeLevel, uint32 anchorLevel, LevelPolicy const& policy);
Factors Calculate(double capacity, Profile profile, uint32 nativeLevel, uint32 targetLevel,
                  uint32 mapId, uint32 entry, Settings const& config);
}
