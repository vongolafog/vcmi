#pragma once

#include "StrategicMission.h"
#include "../Goals/AbstractGoal.h"

namespace NK2AI
{

class Nullkiller;

class StrategicPlanner
{
public:
	explicit StrategicPlanner(Nullkiller * aiNk);

	/// Keep an existing strategic mission alive or acquire a new reachable town target.
	void update();

	const StrategicMission & getCurrentMission() const;
	bool hasActiveMission() const;
	int getMissionTargetId() const;
	int getAssignedHeroId() const;

	/// Keep the assigned hero committed to the strategic route while still allowing
	/// immediate danger/defence handling and army-support actions.
	float adjustPriority(const Goals::TSubgoal & task, int priorityTier, float priority) const;

private:
	Nullkiller * aiNk;
	StrategicMission currentMission;

	void clearMission();
	bool isMissionTarget(const CGObjectInstance * object) const;
	bool isMissionSupportTask(const Goals::TSubgoal & task) const;
};

}
