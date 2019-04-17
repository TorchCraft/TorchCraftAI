/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "agent.h"
#include "squadtask.h"
#include "utils.h"

namespace cherrypi {

void Agent::preMicro() {
  currentAction = MicroAction();
  const auto position = Position(unit);
  if (lastMicroFrame > 0 && !unit->flying() &&
      std::max(lastMove, lastTarget) == lastMicroFrame &&
      (utils::distance(position, lastMoveTo) > 1 || attacking != nullptr) &&
      position == lastPosition &&
      (unit->cd() <= 0 || !target || !target->inRangeOf(unit))) {
    stuckFrames += state->currentFrame() - lastMicroFrame;
    if (stuckFrames >= unstickTriggerFrames) {
      VLOG(3) << utils::unitString(unit) << " stuck for " << stuckFrames
              << " frames";
      if (attacking) {
        VLOG(3) << "It's trying to attack " << utils::unitString(attacking);
      } else {
        VLOG(3) << "It's trying to move to " << lastMoveTo;
      }
    }
  } else {
    stuckFrames = 0;
  }

  lastPosition = position;
  lastMicroFrame = state->currentFrame();

  VLOG(2) << utils::unitString(unit) << " targeted "
          << (attacking ? utils::unitString(attacking) : "nobody")
          << "; will target "
          << (target ? utils::unitString(target) : "nobody");
}

std::shared_ptr<UPCTuple> Agent::microDelete() {
  wantsToFight = true;
  preMicro();
  behaviorDelete->perform(*this);
  return currentAction.getFinalUPC();
}

std::shared_ptr<UPCTuple> Agent::microFlee() {
  wantsToFight = false;
  preMicro();
  behaviorFlee->perform(*this);
  return currentAction.getFinalUPC();
}

void Agent::postCommand(tc::BW::UnitCommandType command) {
  lastMove = -1;
  lastMoveTo = Position();
  attacking = nullptr;
  lastAttack = -1;
  state->board()->postCommand(
      tc::Client::Command(tc::BW::Command::CommandUnit, unit->id, command),
      task->upcId());
}

/// Convenience method for issuing an attack-move UPC.
std::shared_ptr<UPCTuple> Agent::attack(Position const& pos) {
  if (VLOG_IS_ON(3)) {
    VLOG(3) << utils::unitString(unit) << " is sending attack-move to " << pos;
    utils::drawLine(state, unit, pos, tc::BW::Color::Red);
  }

  lastMove = -1;
  lastMoveTo = Position();
  lastAttack = state->currentFrame();
  attacking = target;
  return utils::makeSharpUPC(unit, pos, Command::Delete);
}

/// Convenience method for issuing an attack-unit UPC.
std::shared_ptr<UPCTuple> Agent::attack(Unit* u) {
  if (VLOG_IS_ON(3)) {
    VLOG(3) << utils::unitString(unit) << " is sending attack to "
            << utils::unitString(u);
    utils::drawLine(state, unit, u, tc::BW::Color::Red);
    utils::drawCircle(state, u, 10);
  }

  lastMove = -1;
  lastMoveTo = Position();
  lastAttack = state->currentFrame();
  attacking = u;
  if (!u->visible) {
    return utils::makeSharpUPC(unit, u->pos(), Command::Move);
  } else {
    return utils::makeSharpUPC(unit, u, Command::Delete);
  }
}

/// Convenience method for issuing a move UPC.
std::shared_ptr<UPCTuple> Agent::moveTo(Position pos, bool protect) {
  pos = utils::clampPositionToMap(state, pos);
  // For ground units, protect move commands so we don't mess up pathfinding
  if (protect && !unit->flying() && lastMove >= 0 &&
      utils::distance(pos, lastMoveTo) < 8 &&
      state->currentFrame() - lastMove < 8) {
    return nullptr;
  }
  if (VLOG_IS_ON(3)) {
    VLOG(3) << "Sending move to " << pos;
    utils::drawLine(state, unit, pos);
  }
  lastMove = state->currentFrame();
  lastMoveTo = pos;
  attacking = nullptr;
  lastAttack = -1;
  return utils::makeSharpUPC(unit, pos, Command::Move);
}

/// Convenience method for issuing a move UPC.
std::shared_ptr<UPCTuple> Agent::moveTo(Vec2 pos, bool protect) {
  return moveTo(Position(pos), protect);
}

/// Convenience method for issuing a move UPC using movefilters.
std::shared_ptr<UPCTuple> Agent::filterMove(
    const movefilters::PositionFilters& pfs) {
  return moveTo(movefilters::smartMove(state, unit, pfs));
}

/// Convenience method for issuing a threat-aware move UPC.
std::shared_ptr<UPCTuple> Agent::smartMove(const Position& tgt) {
  return moveTo(movefilters::smartMove(state, unit, tgt));
}

/// Convenience method for issuing a threat-aware move UPC.
std::shared_ptr<UPCTuple> Agent::smartMove(Unit* tgt) {
  return smartMove(tgt->pos());
}

std::shared_ptr<UPCTuple> Agent::tryCastSpellOnUnit(
    const BuildType* spell,
    std::function<double(Unit* const)> scoring,
    double minimumScore) {
  if (!state->hasResearched(spell)) {
    return nullptr;
  }

  Unit* bestTarget = nullptr;
  double bestScore = std::numeric_limits<double>::min();
  auto consider = [&](Unit* candidate) {
    auto candidateScore = scoring(candidate);
    if (candidateScore > std::max(minimumScore, bestScore)) {
      bestTarget = candidate;
      bestScore = candidateScore;
    }
  };
  for (auto* target : task->targets_) {
    consider(target);
  }
  for (auto* ally : task->squadUnits()) {
    consider(ally);
  }

  if (bestTarget) {
    lastMove = -1;
    lastMoveTo = Position();
    attacking = nullptr;
    lastAttack = -1;
    VLOG(1) << utils::unitString(unit) << " with " << unit->unit.energy
            << " energy casting " << spell->name << " on "
            << utils::unitString(bestTarget);
    return utils::makeSharpUPC(unit, bestTarget, Command::Cast, spell);
  }
  VLOG(1) << utils::unitString(unit) << " not casting " << spell->name
          << ": best score was " << bestScore << " / " << minimumScore;
  return nullptr;
}

namespace {
struct SpellArea {
  Position start;
  Position end;
  double score = 0;

  SpellArea(
      State* const state,
      Position const origin,
      int width,
      int height,
      int dx,
      int dy) {
    int x0 = origin.x + dx * width / 2;
    int x1 = origin.x - dx * width / 2;
    int y0 = origin.y + dy * height / 2;
    int y1 = origin.y - dy * height / 2;

    start = Position(std::min(x0, x1), std::min(y0, y1));
    end = Position(std::max(x0, x1), std::max(y0, y1));

    start = utils::clampPositionToMap(state, start);
    end = utils::clampPositionToMap(state, end);
  }
  bool contains(Position position) {
    return position.x >= start.x && position.x < end.x &&
        position.y >= start.y && position.y < end.y;
  }
};
} // namespace
std::shared_ptr<UPCTuple> Agent::tryCastSpellOnArea(
    const BuildType* spell,
    double width,
    double height,
    std::function<double(Unit* const)> scoring,
    double minimumScore,
    std::function<Position(Position input)> positionTransform) {
  if (!state->hasResearched(spell)) {
    return nullptr;
  }

  auto& relevantUnits = task->relevantUnits();

  // Calculate unit scores
  std::unordered_map<Unit*, double> scores;
  for (auto* unit : relevantUnits) {
    scores[unit] = scoring(unit);
  }

  // Consider all viable target areas
  std::vector<SpellArea> spellAreas;
  for (auto* unit : relevantUnits) {
    auto position = unit->pos();
    spellAreas.push_back(SpellArea(state, position, width, height, 1, 1));
    spellAreas.push_back(SpellArea(state, position, width, height, -1, 1));
    spellAreas.push_back(SpellArea(state, position, width, height, 1, -1));
    spellAreas.push_back(SpellArea(state, position, width, height, -1, -1));
  }

  // Score each area
  for (auto& area : spellAreas) {
    for (auto* unit : relevantUnits) {
      if (area.contains(unit->pos())) {
        area.score += scores[unit];
      }
    }
  }

  // Pick the best area
  SpellArea* bestArea = nullptr;
  double bestScore = std::numeric_limits<double>::min();
  for (auto& area : spellAreas) {
    if (area.score > std::max(minimumScore, bestScore)) {
      bestArea = &area;
      bestScore = area.score;
    }
  }

  // Found a good area? Cast the spell there.
  if (bestArea) {
    auto target = positionTransform(Position(
        (bestArea->start.x + bestArea->end.x) / 2,
        (bestArea->start.y + bestArea->end.y) / 2));

    lastMove = -1;
    lastMoveTo = Position();
    attacking = nullptr;
    lastAttack = -1;
    VLOG(1) << utils::unitString(unit) << " with " << unit->unit.energy
            << " energy casting " << spell->name << " on " << target;
    return utils::makeSharpUPC(unit, target, Command::Cast, spell);
  }

  VLOG(1) << utils::unitString(unit) << " not casting " << spell->name
          << ": best score was " << bestScore << " / " << minimumScore;

  return nullptr;
}

} // namespace cherrypi
