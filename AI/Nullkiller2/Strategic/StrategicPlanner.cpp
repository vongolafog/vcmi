#include "StdInc.h"
#include "StrategicPlanner.h"

namespace NK2AI
{

StrategicPlanner::StrategicPlanner(Nullkiller * aiNk)
	: aiNk(aiNk)
{
}

void StrategicPlanner::update()
{
	// v0.12-alpha1: strategic planner skeleton.
}

const StrategicMission & StrategicPlanner::getCurrentMission() const
{
	return currentMission;
}

}
