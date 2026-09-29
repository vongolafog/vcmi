#pragma once

#include "InstanceID.h"

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
	ObjectInstanceID target;
	bool active = false;
};

}
