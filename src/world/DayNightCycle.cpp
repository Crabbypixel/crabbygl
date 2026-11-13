#include "world/DayNightCycle.h"
#include <glm/gtc/constants.hpp>

void DayNightCycle::Update(float dt)
{
    if (paused)
        return;

    time = std::fmod(time + dt / dayLengthSec, 1.0f);
}


glm::vec3 DayNightCycle::LightDir() const
{
    // -u_lightDir points towards light
    float a = (time - 0.25f) * glm::two_pi<float>(); // Zero at sunrise

    // Z tilt: overhead sun would give ever side face NdotL = 0 at noon
    glm::vec3 toSun = glm::normalize(glm::vec3(std::cos(a), std::sin(a), 0.3f));
    return -toSun;
}

// 0 night .. 1 day, smooth through twilight
float DayNightCycle::SkyFactor() const
{
    return glm::smoothstep(-0.10f, 0.25f, -LightDir().y);
}

glm::vec3 DayNightCycle::SkyColor() const
{
    const glm::vec3 night(0.02f, 0.03f, 0.08f);
    const glm::vec3 day(0.227f, 0.757f, 1.0f);
    const glm::vec3 dusk(0.95f, 0.45f, 0.20f);

    float h = -LightDir().y;
    glm::vec3 c = glm::mix(night, day, SkyFactor());

    // Dust tint peaks with sun on the horizon
    float duskAmount = 1.0f - glm::smoothstep(0.0f, 0.3f, std::abs(h));
    return glm::mix(c, dusk, duskAmount * 0.5f);
}