#include "StdInc.h"
#include "StrategicPlanner.h"

#include "../AIUtility.h"
#include "../Engine/Nullkiller.h"
#include "../Engine/PriorityEvaluator.h"

namespace NK2AI
{

namespace
{
constexpr float STRATEGIC_MISSION_PRIORITY = 1000000.0f;

float missionPathScore(const AIPath & path, float conquestValue)
{
	// Prefer a valuable town, but strongly prefer a route that can be acted on sooner.
	const float movementCost = std::max(0.0f, path.movementCost());
	const float value = std::max(0.25f, conquestValue);
	return value * 100.0f / (1.0f + 2.0f * static_cast<float>(path.turn()) + movementCost);
}
}

StrategicPlanner::StrategicPlanner(Nullkiller * aiNk)
	: aiNk(aiNk)
{
}

bool StrategicPlanner::isMissionTarget(const CGObjectInstance * object) const
{
	if(!object || object->ID != Obj::TOWN)
		return false;

	const auto owner = object->getOwner();
	if(owner == aiNk->playerID)
		return false;

	// Neutral towns are valid expansion targets. Player-owned towns are valid
	// only while that player is an enemy.
	return !owner.isValidPlayer()
		|| aiNk->cc->getPlayerRelations(aiNk->playerID, owner) == PlayerRelations::ENEMIES;
}

void StrategicPlanner::clearMission()
{
	currentMission = StrategicMission();
}

void StrategicPlanner::update()
{
	// A capture mission is persistent: normal turn replanning must not replace it
	// with a resource pickup. It is cleared only when the town is no longer a
	// valid conquest target (captured, allied, removed, etc.).
	if(currentMission.active)
	{
		const auto * target = aiNk->cc->getObj(ObjectInstanceID(currentMission.target), false);
		if(isMissionTarget(target))
			return;

		logAi->debug("Strategic mission target %d is no longer valid. Clearing mission.", currentMission.target);
		clearMission();
	}

	const CGTownInstance * bestTown = nullptr;
	float bestScore = -1.0f;
	RewardEvaluator rewardEvaluator(aiNk);

	for(const auto * town : aiNk->cc->getTownsInfo(false))
	{
		if(!isMissionTarget(town))
			continue;

		std::vector<AIPath> paths;
		aiNk->pathfinder->calculatePathInfo(paths, town->visitablePos(), aiNk->isObjectGraphAllowed());

		for(const auto & path : paths)
		{
			if(!path.targetHero || path.targetHero->getOwner() != aiNk->playerID)
				continue;

			const CGHeroInstance * releasedDefender =
				aiNk->canReleaseDefenderForTownCapture(path.targetHero, town, path)
					? path.targetHero
					: nullptr;

			if(aiNk->arePathHeroesLocked(path, releasedDefender))
				continue;

			// Do not create a strategic mission from a suicidal route. If the
			// town is currently too dangerous, normal gathering can continue
			// until a safe route appears.
			if(!isSafeToVisit(
				path.targetHero,
				path.heroArmy,
				path.getTotalDanger(),
				aiNk->settings->getSafeAttackRatio()))
			{
				continue;
			}

			const float score = missionPathScore(path, rewardEvaluator.getConquestValue(town));
			if(score > bestScore)
			{
				bestScore = score;
				bestTown = town;
			}
		}
	}

	if(!bestTown)
		return;

	currentMission.type = StrategicMission::Type::CAPTURE_TOWN;
	currentMission.target = bestTown->id.getNum();
	currentMission.active = true;

	logAi->info(
		"Strategic mission acquired: capture town %s at %s (target %d, score %.3f).",
		bestTown->getNameTextID(),
		bestTown->visitablePos().toString(),
		currentMission.target,
		bestScore);
}

const StrategicMission & StrategicPlanner::getCurrentMission() const
{
	return currentMission;
}

bool StrategicPlanner::hasActiveMission() const
{
	return currentMission.active && currentMission.type == StrategicMission::Type::CAPTURE_TOWN;
}

int StrategicPlanner::getMissionTargetId() const
{
	return hasActiveMission() ? currentMission.target : -1;
}

float StrategicPlanner::adjustPriority(
	const Goals::TSubgoal & task,
	const int priorityTier,
	const float priority) const
{
	if(!hasActiveMission()
		|| priorityTier != PriorityEvaluator::PriorityTier::EXPLORE_AND_GATHER
		|| !task
		|| !task->isElementar())
	{
		return priority;
	}

	const ObjectInstanceID targetId(currentMission.target);
	if(!task->asTask()->isObjectAffected(targetId))
		return priority;

	// KILL/INSTADEFEND/ESCAPE tiers have already had their chance. At the normal
	// gather tier, keep the hero on the strategic route instead of allowing a
	// chest/resource pickup to replace the town mission. Deep decomposition
	// keeps the original town in the composition, so a border guard/keymaster,
	// monster, quest or other blocker is boosted as a subtask of this mission.
	return std::max(priority, STRATEGIC_MISSION_PRIORITY);
}

}
