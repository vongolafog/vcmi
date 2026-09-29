#pragma once

#include "StrategicMission.h"

namespace NK2AI
{

class Nullkiller;

class StrategicPlanner
{
public:
	explicit StrategicPlanner(Nullkiller * aiNk);

	void update();

	const StrategicMission & getCurrentMission() const;

private:
	Nullkiller * aiNk;
	StrategicMission currentMission;
};

}
