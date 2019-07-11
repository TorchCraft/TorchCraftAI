/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <algorithm>
#include <torchcraft/client.h>
#include <utility>

#include "cherrypi.h"

namespace cherrypi {
namespace utils {

// Check whether a unit's current orders include the given command
inline bool isExecutingCommand(
    tc::Unit const& unit,
    tc::BW::UnitCommandType command) {
  auto orders = tc::BW::commandToOrders(command);
  auto res = std::find_first_of(
      unit.orders.begin(),
      unit.orders.end(),
      orders.begin(),
      orders.end(),
      [](tc::Order const& o1, tc::BW::Order o2) { return o1.type == o2; });
  return res != unit.orders.end();
}

inline bool isExecutingCommand(
    Unit const* unit,
    tc::BW::UnitCommandType command) {
  return isExecutingCommand(unit->unit, std::move(command));
}

inline UnitId commandUnitId(tc::Client::Command const& cmd) {
  if ((cmd.code == tc::BW::Command::CommandUnit ||
       cmd.code == tc::BW::Command::CommandUnitProtected) &&
      cmd.args.size() > 0) {
    return cmd.args[0];
  }
  return -1;
}

inline tc::BW::UnitCommandType commandUnitType(tc::Client::Command const& cmd) {
  if ((cmd.code == tc::BW::Command::CommandUnit ||
       cmd.code == tc::BW::Command::CommandUnitProtected) &&
      cmd.args.size() > 1) {
    auto uct = tc::BW::UnitCommandType::_from_integral_nothrow(cmd.args[1]);
    if (uct) {
      return *uct;
    }
  }
  return tc::BW::UnitCommandType::MAX;
}

inline tc::BW::UnitType buildCommandUnitType(tc::Client::Command const& cmd) {
  if ((cmd.code == tc::BW::Command::CommandUnit ||
       cmd.code == tc::BW::Command::CommandUnitProtected) &&
      cmd.args.size() > 5 && cmd.args[1] == tc::BW::UnitCommandType::Build) {
    auto ut = tc::BW::UnitType::_from_integral_nothrow(cmd.args[5]);
    if (ut) {
      return *ut;
    }
  }
  return tc::BW::UnitType::MAX;
}

inline tc::BW::UnitType trainCommandUnitType(tc::Client::Command const& cmd) {
  if ((cmd.code == tc::BW::Command::CommandUnit ||
       cmd.code == tc::BW::Command::CommandUnitProtected) &&
      cmd.args.size() > 2 && cmd.args[1] == tc::BW::UnitCommandType::Train) {
    if (cmd.args[2] < 0) {
      // Unit type specified in 'extra' field
      if (cmd.args.size() > 5) {
        auto ut = tc::BW::UnitType::_from_integral_nothrow(cmd.args[5]);
        if (ut) {
          return *ut;
        }
      } else {
        return tc::BW::UnitType::MAX;
      }
    } else {
      auto ut = tc::BW::UnitType::_from_integral_nothrow(cmd.args[2]);
      if (ut) {
        return *ut;
      }
    }
  }
  return tc::BW::UnitType::MAX;
}

inline Position buildCommandPosition(tc::Client::Command const& cmd) {
  if ((cmd.code == tc::BW::Command::CommandUnit ||
       cmd.code == tc::BW::Command::CommandUnitProtected) &&
      cmd.args.size() > 4 && cmd.args[1] == tc::BW::UnitCommandType::Build) {
    return Position(cmd.args[3], cmd.args[4]);
  }
  return Position(-1, -1);
}

inline bool tcOrderIsAttack(int orderId) {
  auto order = tc::BW::Order::_from_integral_nothrow(orderId);
  if (!order) {
    return false;
  }
  switch (*order) {
    // case tc::BW::Order::Die:
    // case tc::BW::Order::Stop:
    case tc::BW::Order::Guard:
    case tc::BW::Order::PlayerGuard:
    case tc::BW::Order::TurretGuard:
    case tc::BW::Order::BunkerGuard:
    // case tc::BW::Order::Move:
    // case tc::BW::Order::ReaverStop:
    case tc::BW::Order::Attack1:
    case tc::BW::Order::Attack2:
    case tc::BW::Order::AttackUnit:
    case tc::BW::Order::AttackFixedRange:
    case tc::BW::Order::AttackTile:
    // case tc::BW::Order::Hover:
    case tc::BW::Order::AttackMove:
    // case tc::BW::Order::InfestedCommandCenter:
    // case tc::BW::Order::UnusedNothing:
    // case tc::BW::Order::UnusedPowerup:
    case tc::BW::Order::TowerGuard:
    case tc::BW::Order::TowerAttack:
    case tc::BW::Order::VultureMine:
    case tc::BW::Order::StayInRange:
    case tc::BW::Order::TurretAttack:
    // case tc::BW::Order::Nothing:
    // case tc::BW::Order::Unused_24:
    // case tc::BW::Order::DroneStartBuild:
    // case tc::BW::Order::DroneBuild:
    case tc::BW::Order::CastInfestation:
    case tc::BW::Order::MoveToInfest:
    case tc::BW::Order::InfestingCommandCenter:
    // case tc::BW::Order::PlaceBuilding:
    // case tc::BW::Order::PlaceProtossBuilding:
    // case tc::BW::Order::CreateProtossBuilding:
    // case tc::BW::Order::ConstructingBuilding:
    // case tc::BW::Order::Repair:
    // case tc::BW::Order::MoveToRepair:
    // case tc::BW::Order::PlaceAddon:
    // case tc::BW::Order::BuildAddon:
    // case tc::BW::Order::Train:
    // case tc::BW::Order::RallyPointUnit:
    // case tc::BW::Order::RallyPointTile:
    // case tc::BW::Order::ZergBirth:
    // case tc::BW::Order::ZergUnitMorph:
    // case tc::BW::Order::ZergBuildingMorph:
    // case tc::BW::Order::IncompleteBuilding:
    // case tc::BW::Order::IncompleteMorphing:
    // case tc::BW::Order::BuildNydusExit:
    // case tc::BW::Order::EnterNydusCanal:
    // case tc::BW::Order::IncompleteWarping:
    // case tc::BW::Order::Follow:
    // case tc::BW::Order::Carrier:
    // case tc::BW::Order::ReaverCarrierMove:
    // case tc::BW::Order::CarrierStop:
    case tc::BW::Order::CarrierAttack:
    case tc::BW::Order::CarrierMoveToAttack:
    // case tc::BW::Order::CarrierIgnore2:
    case tc::BW::Order::CarrierFight:
    case tc::BW::Order::CarrierHoldPosition:
    // case tc::BW::Order::Reaver:
    case tc::BW::Order::ReaverAttack:
    case tc::BW::Order::ReaverMoveToAttack:
    case tc::BW::Order::ReaverFight:
    case tc::BW::Order::ReaverHoldPosition:
    // case tc::BW::Order::TrainFighter:
    case tc::BW::Order::InterceptorAttack:
    case tc::BW::Order::ScarabAttack:
    // case tc::BW::Order::RechargeShieldsUnit:
    // case tc::BW::Order::RechargeShieldsBattery:
    // case tc::BW::Order::ShieldBattery:
    // case tc::BW::Order::InterceptorReturn:
    // case tc::BW::Order::DroneLand:
    // case tc::BW::Order::BuildingLand:
    // case tc::BW::Order::BuildingLiftOff:
    // case tc::BW::Order::DroneLiftOff:
    // case tc::BW::Order::LiftingOff:
    // case tc::BW::Order::ResearchTech:
    // case tc::BW::Order::Upgrade:
    // case tc::BW::Order::Larva:
    // case tc::BW::Order::SpawningLarva:
    // case tc::BW::Order::Harvest1:
    // case tc::BW::Order::Harvest2:
    // case tc::BW::Order::MoveToGas:
    // case tc::BW::Order::WaitForGas:
    // case tc::BW::Order::HarvestGas:
    // case tc::BW::Order::ReturnGas:
    // case tc::BW::Order::MoveToMinerals:
    // case tc::BW::Order::WaitForMinerals:
    // case tc::BW::Order::MiningMinerals:
    // case tc::BW::Order::Harvest3:
    // case tc::BW::Order::Harvest4:
    // case tc::BW::Order::ReturnMinerals:
    // case tc::BW::Order::Interrupted:
    // case tc::BW::Order::EnterTransport:
    // case tc::BW::Order::PickupIdle:
    // case tc::BW::Order::PickupTransport:
    // case tc::BW::Order::PickupBunker:
    // case tc::BW::Order::Pickup4:
    // case tc::BW::Order::PowerupIdle:
    // case tc::BW::Order::Sieging:
    // case tc::BW::Order::Unsieging:
    // case tc::BW::Order::WatchTarget:
    // case tc::BW::Order::InitCreepGrowth:
    // case tc::BW::Order::SpreadCreep:
    // case tc::BW::Order::StoppingCreepGrowth:
    // case tc::BW::Order::GuardianAspect:
    // case tc::BW::Order::ArchonWarp:
    // case tc::BW::Order::CompletingArchonSummon:
    case tc::BW::Order::HoldPosition:
    // case tc::BW::Order::QueenHoldPosition:
    // case tc::BW::Order::Cloak:
    // case tc::BW::Order::Decloak:
    // case tc::BW::Order::Unload:
    // case tc::BW::Order::MoveUnload:
    case tc::BW::Order::FireYamatoGun:
    case tc::BW::Order::MoveToFireYamatoGun:
    case tc::BW::Order::CastLockdown:
    // case tc::BW::Order::Burrowing:
    // case tc::BW::Order::Burrowed:
    // case tc::BW::Order::Unburrowing:
    // case tc::BW::Order::CastDarkSwarm:
    case tc::BW::Order::CastParasite:
    case tc::BW::Order::CastSpawnBroodlings:
    case tc::BW::Order::CastEMPShockwave:
    // case tc::BW::Order::NukeWait:
    // case tc::BW::Order::NukeTrain:
    // case tc::BW::Order::NukeLaunch:
    // case tc::BW::Order::NukePaint:
    case tc::BW::Order::NukeUnit:
    case tc::BW::Order::CastNuclearStrike:
    // case tc::BW::Order::NukeTrack:
    // case tc::BW::Order::InitializeArbiter:
    // case tc::BW::Order::CloakNearbyUnits:
    // case tc::BW::Order::PlaceMine:
    // case tc::BW::Order::RightClickAction:
    case tc::BW::Order::SuicideUnit:
    // case tc::BW::Order::SuicideLocation:
    case tc::BW::Order::SuicideHoldPosition:
    // case tc::BW::Order::CastRecall:
    // case tc::BW::Order::Teleport:
    // case tc::BW::Order::CastScannerSweep:
    // case tc::BW::Order::Scanner:
    // case tc::BW::Order::CastDefensiveMatrix:
    // case tc::BW::Order::CastPsionicStorm:
    case tc::BW::Order::CastIrradiate:
    // case tc::BW::Order::CastPlague:
    // case tc::BW::Order::CastConsume:
    // case tc::BW::Order::CastEnsnare:
    // case tc::BW::Order::CastStasisField:
    // case tc::BW::Order::CastHallucination:
    // case tc::BW::Order::Hallucination2:
    // case tc::BW::Order::ResetCollision:
    // case tc::BW::Order::ResetHarvestCollision:
    case tc::BW::Order::Patrol:
    // case tc::BW::Order::CTFCOPInit:
    // case tc::BW::Order::CTFCOPStarted:
    // case tc::BW::Order::CTFCOP2:
    // case tc::BW::Order::ComputerAI:
    case tc::BW::Order::AtkMoveEP:
    case tc::BW::Order::HarassMove:
    case tc::BW::Order::AIPatrol:
    // case tc::BW::Order::GuardPost:
    // case tc::BW::Order::RescuePassive:
    // case tc::BW::Order::Neutral:
    // case tc::BW::Order::ComputerReturn:
    // case tc::BW::Order::InitializePsiProvider:
    // case tc::BW::Order::SelfDestructing:
    // case tc::BW::Order::Critter:
    // case tc::BW::Order::HiddenGun:
    // case tc::BW::Order::OpenDoor:
    // case tc::BW::Order::CloseDoor:
    // case tc::BW::Order::HideTrap:
    // case tc::BW::Order::RevealTrap:
    // case tc::BW::Order::EnableDoodad:
    // case tc::BW::Order::DisableDoodad:
    // case tc::BW::Order::WarpIn:
    // case tc::BW::Order::Medic:
    // case tc::BW::Order::MedicHeal:
    // case tc::BW::Order::HealMove:
    // case tc::BW::Order::MedicHoldPosition:
    // case tc::BW::Order::MedicHealToIdle:
    // case tc::BW::Order::CastRestoration:
    // case tc::BW::Order::CastDisruptionWeb:
    case tc::BW::Order::CastMindControl:
    // case tc::BW::Order::DarkArchonMeld:
    case tc::BW::Order::CastFeedback:
    case tc::BW::Order::CastOpticalFlare:
      // case tc::BW::Order::CastMaelstrom:
      // case tc::BW::Order::JunkYardDog:
      // case tc::BW::Order::Fatal:
      // case tc::BW::Order::None:
      return true;
    default:
      return false;
  }
}

} // namespace utils
} // namespace cherrypi
