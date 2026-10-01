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
	bool safe = false;
	bool mainHero = false;
};

bool isBetterStrategicCandidate(const StrategicPathCandidate & candidate, const StrategicPathCandidate & best)
{
	if(!candidate.hero)
		return false;
	if(!best.hero)
		return true;

	// A strategic conquest mission should belong to a main hero whenever one has
	// a route. Within the same role, prefer a route that is already safe; only
	// then compare distance/value score.
	if(candidate.mainHero != best.mainHero)
		return candidate.mainHero;
	if(candidate.safe != best.safe)
		return candidate.safe;
	return candidate.score > best.score;
}

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

		const bool survivalMode = aiNk->cc->getTownsInfo().empty();

		StrategicPathCandidate candidate;
		candidate.hero = path.targetHero;
		candidate.score = survivalMode
			? 100.0f / (1.0f + 2.0f * static_cast<float>(path.turn()) + std::max(0.0f, path.movementCost()))
			: missionPathScore(path, rewardEvaluator.getConquestValue(town));
		candidate.safe = survivalMode || isSafeToVisit(
			path.targetHero,
			path.heroArmy,
			path.getTotalDanger(),
			aiNk->settings->getSafeAttackRatio());
		candidate.mainHero =
			aiNk->heroManager->getHeroRoleOrDefaultInefficient(path.targetHero) == HeroRole::MAIN;

		if(isBetterStrategicCandidate(candidate, best))
			best = candidate;
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
					const bool wasActionable = currentMission.actionable;
					currentMission.actionable = assignedPath.safe;

					if(wasActionable != currentMission.actionable)
					{
						logAi->info(
							"Strategic mission target %d with hero %d changed attack state: %s.",
							currentMission.target,
							currentMission.assignedHero,
							currentMission.actionable ? "SAFE" : "WAIT");
					}
					return;
				}
			}

			// Assigned hero was lost, locked by an urgent duty, or has no route.
			// Prefer a replacement hero for the SAME town before ever changing the
			// strategic target. The replacement route does not have to be safe yet.
			const auto replacement = findBestTownPath(aiNk, town);
			if(replacement.hero)
			{
				const int oldHero = currentMission.assignedHero;
				currentMission.assignedHero = replacement.hero->id.getNum();
				currentMission.actionable = replacement.safe;

				if(oldHero != currentMission.assignedHero)
				{
					logAi->info(
						"Strategic mission target %d reassigned from hero %d to hero %d (attack state: %s).",
						currentMission.target,
						oldHero,
						currentMission.assignedHero,
						currentMission.actionable ? "SAFE" : "WAIT");
				}
				return;
			}

			// Keep memory of the town even with no current route. This is deliberately
			// different from the old behaviour where an unsafe/unreachable town simply
			// vanished from strategic consideration.
			currentMission.actionable = false;
			return;
		}
	}

	const CGTownInstance * bestTown = nullptr;
	StrategicPathCandidate bestCandidate;

	for(const auto * town : aiNk->cc->getTownsInfo(false))
	{
		if(!isMissionTarget(town))
			continue;

		const auto candidate = findBestTownPath(aiNk, town);
		if(isBetterStrategicCandidate(candidate, bestCandidate))
		{
			bestTown = town;
			bestCandidate = candidate;
		}
	}

	if(!bestTown || !bestCandidate.hero)
		return;

	// Discovery and attack permission are separate decisions. Seeing a reachable
	// enemy town is enough to remember it as a long-term mission; safety only
	// decides whether the assigned hero is allowed to execute the assault now.
	currentMission.type = StrategicMission::Type::CAPTURE_TOWN;
	currentMission.target = bestTown->id.getNum();
	currentMission.assignedHero = bestCandidate.hero->id.getNum();
	currentMission.active = true;
	currentMission.actionable = bestCandidate.safe;

	const bool survivalMode = aiNk->cc->getTownsInfo().empty();
	logAi->info(
		"Strategic mission acquired: capture town %s at %s with hero %s "
		"(target %d, hero %d, role %s, attack state %s, mode %s, score %.3f).",
		bestTown->getNameTextID(),
		bestTown->visitablePos().toString(),
		bestCandidate.hero->getNameTextID(),
		currentMission.target,
		currentMission.assignedHero,
		bestCandidate.mainHero ? "MAIN" : "SCOUT",
		currentMission.actionable ? "SAFE" : "WAIT",
		survivalMode ? "SURVIVAL" : "NORMAL",
		bestCandidate.score);
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
