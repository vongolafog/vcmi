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

struct StrategicPathCandidate
{
	const CGHeroInstance * hero = nullptr;
	float score = -1.0f;
};

StrategicPathCandidate findBestTownPath(
	Nullkiller * aiNk,
	const CGTownInstance * town,
	const CGHeroInstance * requiredHero = nullptr)
{
	StrategicPathCandidate best;
	RewardEvaluator rewardEvaluator(aiNk);
	std::vector<AIPath> paths;

	aiNk->pathfinder->calculatePathInfo(paths, town->visitablePos(), aiNk->isObjectGraphAllowed());

	for(const auto & path : paths)
	{
		if(!path.targetHero || path.targetHero->getOwner() != aiNk->playerID)
			continue;

		if(requiredHero && path.targetHero != requiredHero)
			continue;

		const CGHeroInstance * releasedDefender =
			aiNk->canReleaseDefenderForTownCapture(path.targetHero, town, path)
				? path.targetHero
				: nullptr;

		if(aiNk->arePathHeroesLocked(path, releasedDefender))
			continue;

		if(!isSafeToVisit(
			path.targetHero,
			path.heroArmy,
			path.getTotalDanger(),
			aiNk->settings->getSafeAttackRatio()))
		{
			continue;
		}

		const float score = missionPathScore(path, rewardEvaluator.getConquestValue(town));
		if(score > best.score)
		{
			best.hero = path.targetHero;
			best.score = score;
		}
	}

	return best;
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
	// Keep both the target and the responsible hero stable. This prevents the
	// normal per-pass rescoring loop from making the same hero walk toward a
	// strategic target, then turn around for a chest/resource, then turn back.
	if(currentMission.active)
	{
		const auto * targetObject = aiNk->cc->getObj(ObjectInstanceID(currentMission.target), false);
		if(!isMissionTarget(targetObject))
		{
			logAi->debug("Strategic mission target %d is no longer valid. Clearing mission.", currentMission.target);
			clearMission();
		}
		else
		{
			const auto * town = dynamic_cast<const CGTownInstance *>(targetObject);
			const CGHeroInstance * assignedHero = nullptr;

			if(currentMission.assignedHero >= 0)
				assignedHero = aiNk->cc->getHero(ObjectInstanceID(currentMission.assignedHero));

			if(assignedHero && assignedHero->getOwner() == aiNk->playerID)
			{
				const auto assignedPath = findBestTownPath(aiNk, town, assignedHero);
				if(assignedPath.hero)
				{
					currentMission.actionable = true;
					return;
				}
			}

			// Assigned hero was lost, locked by an urgent duty, or currently has no
			// safe route. Prefer a replacement hero for the SAME town before ever
			// changing the strategic target.
			const auto replacement = findBestTownPath(aiNk, town);
			if(replacement.hero)
			{
				const int oldHero = currentMission.assignedHero;
				currentMission.assignedHero = replacement.hero->id.getNum();
				currentMission.actionable = true;

				if(oldHero != currentMission.assignedHero)
				{
					logAi->info(
						"Strategic mission target %d reassigned from hero %d to hero %d.",
						currentMission.target,
						oldHero,
						currentMission.assignedHero);
				}
				return;
			}

			// Preserve the long-term town target even when it is temporarily unsafe
			// or unreachable. In that state we deliberately stop suppressing normal
			// gathering so the AI can acquire troops/keys or discover another route.
			currentMission.actionable = false;
			return;
		}
	}

	const CGTownInstance * bestTown = nullptr;
	const CGHeroInstance * bestHero = nullptr;
	float bestScore = -1.0f;

	for(const auto * town : aiNk->cc->getTownsInfo(false))
	{
		if(!isMissionTarget(town))
			continue;

		const auto candidate = findBestTownPath(aiNk, town);
		if(candidate.hero && candidate.score > bestScore)
		{
			bestTown = town;
			bestHero = candidate.hero;
			bestScore = candidate.score;
		}
	}

	if(!bestTown || !bestHero)
		return;

	currentMission.type = StrategicMission::Type::CAPTURE_TOWN;
	currentMission.target = bestTown->id.getNum();
	currentMission.assignedHero = bestHero->id.getNum();
	currentMission.active = true;
	currentMission.actionable = true;

	logAi->info(
		"Strategic mission acquired: capture town %s at %s with hero %s (target %d, hero %d, score %.3f).",
		bestTown->getNameTextID(),
		bestTown->visitablePos().toString(),
		bestHero->getNameTextID(),
		currentMission.target,
		currentMission.assignedHero,
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

int StrategicPlanner::getAssignedHeroId() const
{
	return hasActiveMission() ? currentMission.assignedHero : -1;
}

bool StrategicPlanner::isMissionSupportTask(const Goals::TSubgoal & task) const
{
	if(!task)
		return false;

	switch(task->goalType)
	{
	case Goals::HERO_EXCHANGE:
	case Goals::ARMY_UPGRADE:
	case Goals::ADVENTURE_SPELL_CAST:
		return true;
	default:
		return false;
	}
}

float StrategicPlanner::adjustPriority(
	const Goals::TSubgoal & task,
	const int priorityTier,
	const float priority) const
{
	if(!hasActiveMission()
		|| !currentMission.actionable
		|| currentMission.assignedHero < 0
		|| !task
		|| !task->isElementar())
	{
		return priority;
	}

	const ObjectInstanceID targetId(currentMission.target);
	const ObjectInstanceID heroId(currentMission.assignedHero);
	const auto * taskImpl = task->asTask();

	const bool usesAssignedHero = taskImpl->isObjectAffected(heroId);
	if(!usesAssignedHero)
		return priority;

	const bool advancesMission = taskImpl->isObjectAffected(targetId);

	// Do not steal the assigned hero for another multi-turn conquest/chase while
	// a town mission is actionable. Immediate INSTAKILL, urgent defence and
	// ESCAPE are evaluated in their own tiers and remain untouched.
	if(priorityTier == PriorityEvaluator::PriorityTier::KILL && !advancesMission)
		return 0.0f;

	if(priorityTier != PriorityEvaluator::PriorityTier::EXPLORE_AND_GATHER)
		return priority;

	if(advancesMission)
	{
		// The deep-decomposed blocker task still carries the town as an affected
		// object, so monsters, border guards, quest gates and keymaster subtasks
		// inherit this strategic priority instead of replacing the town mission.
		return std::max(priority, STRATEGIC_MISSION_PRIORITY);
	}

	if(isMissionSupportTask(task))
		return priority;

	// Suppress ordinary resource/chest/exploration detours for the committed
	// hero. Other heroes continue using the normal Nullkiller priorities.
	return 0.0f;
}

}
