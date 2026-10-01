					score -= evaluationContext.goldCost / 4.0f; // don't include the full cost of School of Magic or others because those locations are beneficial
				score = evaluateArmyLossRatio(score, evaluationContext.armyLossRatio, evaluationContext.heroRole);

				score *= evaluationContext.closestWayRatio;
				score = evaluateMovement(score, evaluationContext.movementCost);

				break;
			}
			case DEFEND: //Defend whatever if nothing else is to do
			{
				if (evaluationContext.enemyHeroDangerRatio > maxEnemyDangerRatio)
					return 0;
				if (evaluationContext.isDefend || evaluationContext.isArmyUpgrade)
					score = evaluationContext.armyInvolvement;

				score *= evaluationContext.closestWayRatio;
				score = evaluateMovement(score, evaluationContext.movementCost);
				break;
			}
			case BUILDINGS: //For buildings and buying army
			{
				// TODO: Mircea: What's the point of this check for ::BUILDINGS? Isn't the priority itself just for buildings? To test
				if(maxWillingToLoseForTask - evaluationContext.armyLossRatio < 0)
					return 0;
				//If we already have locked resources, we don't look at other buildings
				if(aiNk->getLockedResources().marketValue() > 0)
					return 0;

				// TODO: Mircea: See if evaluateConquestValue can be reused here as well, to test, don't want to disturb building logic
				score += evaluationContext.conquestValue * 1000;
				score += evaluationContext.strategicalValue * 1000;
				score += evaluationContext.goldReward;
				score = evaluateSkillReward(score, evaluationContext.skillReward, evaluationContext.armyInvolvement, evaluationContext.armyLossRatio);
				score += evaluationContext.armyReward;
				score += evaluationContext.armyGrowth;

				if(evaluationContext.buildingCost.marketValue() > 0)
				{
					if(!evaluationContext.isTradeBuilding && aiNk->getFreeResources()[EGameResID::WOOD] - evaluationContext.buildingCost[EGameResID::WOOD] < 5
					   && aiNk->buildAnalyzer->getDailyIncome()[EGameResID::WOOD] < 1)
					{
						logAi->trace("priorityTier %d, Should make sure to build marketplace instead of %s", priorityTier, task->toString());
						for(auto town : aiNk->cc->getTownsInfo())
						{
							if(!town->hasBuiltResourceMarketplace())
								return 0;
						}
					}

					score += 1000;
					auto resourcesAvailable = evaluationContext.evaluator.aiNk->getFreeResources();
					auto income = aiNk->buildAnalyzer->getDailyIncome();

					// TODO: Mircea: Might want to use isGoldPressureOverMax or canAfford inside hunter gather as well if it's not already applied before
					if(aiNk->buildAnalyzer->isGoldPressureOverMax())
						score /= evaluationContext.buildingCost.marketValue();
					if(!resourcesAvailable.canAfford(evaluationContext.buildingCost))
					{
						TResources needed = evaluationContext.buildingCost - resourcesAvailable;
						needed.positive();
						int turnsTo = needed.maxPurchasableCount(income);
						bool haveEverythingButGold = true;

						for(const GameResID & i : LIBRARY->resourceTypeHandler->getAllObjects())
						{
							if(i != GameResID::GOLD && resourcesAvailable[i] < evaluationContext.buildingCost[i])
								haveEverythingButGold = false;
						}

						if(turnsTo == INT_MAX)
							return 0;
						if(!haveEverythingButGold)
							score /= turnsTo;
					}
				}
				else
				{
					if(evaluationContext.enemyHeroDangerRatio > 1 && !evaluationContext.isDefend && vstd::isAlmostZero(evaluationContext.conquestValue))
						return 0;
				}
				break;
			}
			default:
				throw std::runtime_error("PriorityEvaluator::evaluate Unsupported priority: " + std::to_string(priorityTier));
		}

		result = score;
		//TODO: Figure out the root cause for why evaluationContext.closestWayRatio has become -nan(ind).
		if (std::isnan(result))
			return 0;
	}

#if NK2AI_TRACE_LEVEL >= 2
	logAi->trace(
		"priorityTier %d, Evaluated %s, armyLossRatio: %f, turn: %d, turns main: %f, turns scout: %f, armyInvolvement: %f, "
		"goldReward: %f, goldCost: %d, armyReward: %f, armyGrowth: %f, skillReward: %f, danger: %d, threatTurns: %d, threat: %d, "
		"heroRole: %s, strategicalValue: %f, conquestValue: %f, buildingCost.marketValue: %f, closestWayRatio: %f, enemyHeroDangerRatio: %f, "
		"explorePriority: %d, isDefend: %d, isEnemy: %d, powerRatio: %f, result %f",
		priorityTier,
		task->toString(),
		evaluationContext.armyLossRatio,
		static_cast<int>(evaluationContext.turn),
		evaluationContext.getMovementCost(HeroRole::MAIN),
		evaluationContext.getMovementCost(HeroRole::SCOUT),
		evaluationContext.armyInvolvement,
		evaluationContext.goldReward,
		evaluationContext.goldCost,
		evaluationContext.armyReward,
		evaluationContext.armyGrowth,
		evaluationContext.skillReward,
		evaluationContext.danger,
		evaluationContext.threatTurns,
		evaluationContext.threat,
		evaluationContext.heroRole == HeroRole::MAIN ? "main" : "scout",
		evaluationContext.strategicalValue,
		evaluationContext.conquestValue,
		evaluationContext.buildingCost.marketValue(),
		evaluationContext.closestWayRatio,
		evaluationContext.enemyHeroDangerRatio,
		evaluationContext.explorePriority,
		evaluationContext.isDefend,
		evaluationContext.isEnemy,
		evaluationContext.powerRatio,
		result
	);
#endif

	return result;
}

}
