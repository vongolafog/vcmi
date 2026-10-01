#pragma once

namespace NK2AI
{

class StrategicMission
{
public:
	enum class Type
	{
		NONE,
		CAPTURE_TOWN,
		CLEAR_BLOCKER,
		EXPLORE
	};

	Type type = Type::NONE;
	int target = -1;
	int assignedHero = -1;
	bool active = false;
	bool actionable = false;
};

}
