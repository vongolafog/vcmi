/*
 * FuzzyHelper.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
*/
#include "../StdInc.h"
#include "FuzzyHelper.h"

#include "../Goals/Goals.h"
#include "Nullkiller.h"

#include "../../../lib/mapObjectConstructors/AObjectTypeHandler.h"
#include "../../../lib/mapObjectConstructors/CObjectClassesHandler.h"

namespace NK2AI
{

ui64 FuzzyHelper::evaluateDanger(const int3 & tile, const CGHeroInstance * visitor, bool checkGuards)
{
	auto cb = aiNk->cc.get();
	const TerrainTile * t = cb->getTile(tile, false);
	if(!t) //we can know about guard but can't check its tile (the edge of fow)
		return 190000000; //MUCH

	ui64 objectDanger = 0;
	ui64 guardDanger = 0;

	auto visitableObjects = cb->getVisitableObjs(tile);
	// in some scenarios hero happens to be "under" the object (eg town). Then we consider ONLY the hero.
	if(vstd::contains_if(visitableObjects, objWithID<Obj::HERO>))
	{
		vstd::erase_if(visitableObjects, [](const CGObjectInstance * obj) -> bool
		{
				return !objWithID<Obj::HERO>(obj);
		});
	}

	if(const CGObjectInstance * dangerousObject = vstd::backOrNull(visitableObjects))
	{
		objectDanger = evaluateDanger(dangerousObject); //unguarded objects can also be dangerous or unhandled

		if(objWithID<Obj::HERO>(dangerousObject))
		{
			const auto * hero = dynamic_cast<const CGHeroInstance *>(dangerousObject);
			const ui64 outsideHeroDanger =
				evaluateDanger(hero) * aiNk->heroManager->getFightingStrengthCached(hero);
			objectDanger = outsideHeroDanger;

			// Treat a visiting hero and the army behind the town as separate combat
			// stages. This mirrors the classic AI behaviour better than summing every
			// defender into one perfect combined estimate and also keeps the familiar
			// "one-unit visiting hero" bait bounded by the actual inner garrison.
			if(const auto * town = hero->getVisitedTown())
			{
				ui64 innerTownDanger = evaluateDanger(town);
				if(const auto * garrisonHero = town->getGarrisonHero())
					innerTownDanger *= aiNk->heroManager->getFightingStrengthCached(garrisonHero);

				objectDanger = std::max(outsideHeroDanger, innerTownDanger);
			}
		}
		if(objWithID<Obj::TOWN>(dangerousObject))
		{
			const auto * town = dynamic_cast<const CGTownInstance *>(dangerousObject);
			if(const auto * garrisonHero = town->getGarrisonHero())
				objectDanger *= aiNk->heroManager->getFightingStrengthCached(garrisonHero);
		}

		if(dangerousObject->ID == Obj::SUBTERRANEAN_GATE)
		{
			//check guard on the other side of the gate
			auto it = aiNk->memory->knownSubterraneanGates.find(dangerousObject);
			if(it != aiNk->memory->knownSubterraneanGates.end())
			{
				auto guards = cb->getGuardingCreatures(it->second->visitablePos());

				for(auto cre : guards)
					vstd::amax(guardDanger, evaluateDanger(cre));
			}
		}
	}

	if(checkGuards)
	{
		auto guards = cb->getGuardingCreatures(tile);
		for(auto cre : guards)
			vstd::amax(guardDanger, evaluateDanger(cre)); //we are interested in strongest monster around
	}

	//TODO mozna odwiedzic blockvis nie ruszajac straznika
	return std::max(objectDanger, guardDanger);
}

ui64 FuzzyHelper::evaluateDanger(const CGObjectInstance * obj)
{
	auto cb = aiNk->cc.get();

	if(obj->tempOwner.isValidPlayer() && cb->getPlayerRelations(obj->tempOwner, aiNk->playerID) != PlayerRelations::ENEMIES) //owned or allied objects don't pose any threat
		return 0;

	switch(obj->ID)
	{
	case Obj::TOWN:
	{
		const auto * town = dynamic_cast<const CGTownInstance *>(obj);
		auto danger = town->getUpperArmy()->getArmyStrength();

		// Strategic siege estimate, intentionally kept small and fixed:
		// Fort walls alone add no abstract army value because the attacker has a
		// catapult. Citadel/Castle account only for the extra towers/moat/delay.
		// Do not scale this bonus with army size.
		if(danger || town->getVisitingHero())
		{
			switch(town->fortLevel())
			{
			case CGTownInstance::EFortLevel::CASTLE:
				danger += 2000;
				break;
			case CGTownInstance::EFortLevel::CITADEL:
				danger += 1000;
				break;
			default:
				break; // NONE / FORT = +0
			}
		}

		return danger;
	}

	case Obj::HERO:
	{
		const CGHeroInstance * hero = dynamic_cast<const CGHeroInstance *>(obj);
		return getHeroArmyStrengthWithCommander(hero, hero);
	}

	case Obj::ARTIFACT:
	case Obj::RESOURCE:
	{
		if(!vstd::contains(aiNk->memory->alreadyVisited, obj->id))
			return 0;
		[[fallthrough]];
	}
	default:
	{
		const CArmedInstance * a = dynamic_cast<const CArmedInstance *>(obj);
		if (a)
			return a->getArmyStrength();
		else
			return 0;
	}
	}
}
}
