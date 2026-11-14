#pragma once
#include <glm/glm.hpp>
#include <cmath>

class DayNightCycle
{
public:
	float dayLengthSec = 600.0f;		// Full cycle
	float time = 0.30f;					// 0 = midnight, 0.25 = sunrise, 0.5 noon, 0.75 = sunset
	bool paused = false;

	void Update(float dt);
	
	glm::vec3 LightDir() const;
	glm::vec3 SunDir() const;

	float SkyFactor() const;
	glm::vec3 SkyColor() const;
	glm::vec3 ZenithColor() const;
	glm::vec3 HorizonColor() const;
	glm::vec4 SunsetColor() const;
};

