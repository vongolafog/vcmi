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

	/// Strategic town missions are allowed to survive beyond the normal short
	/// conquest horizon. Urgent kill/defence/escape tiers still run first.
	float adjustPriority(const Goals::TSubgoal & task, int priorityTier, float priority) const;

private:
	Nullkiller * aiNk;
	StrategicMission currentMission;

	void clearMission();
	bool isMissionTarget(const CGObjectInstance * object) const;
};

}
