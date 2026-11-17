#include "world/DayNightCycle.h"
#include <glm/gtc/constants.hpp>

void DayNightCycle::Update(float dt)
{
    if (paused)
        return;

    time = std::fmod(time + dt / dayLengthSec, 1.0f);
}

// World -> sun, rises at +X, sets at -X, exactly in the XY plane (Z = 0)
glm::vec3 DayNightCycle::SunDir() const
{
    float a = CelestialAngle();   // Zero at sunrise
    return glm::vec3(std::cos(a), std::sin(a), 0.0f);
}

// Shading only: small Z tilt so side faces aren't all NdotL = 0 at noon
// Nothing casts shadows, so the mismatch with the drawn sun is invisible
glm::vec3 DayNightCycle::LightDir() const
{
    const glm::vec3 s = SunDir();
    return -glm::normalize(glm::vec3(s.x, s.y, 0.3f));
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

glm::vec3 DayNightCycle::ZenithColor() const
{
    const glm::vec3 night(0.01f, 0.01f, 0.04f);
    const glm::vec3 day(0.227f, 0.757f, 1.0f);      // Old flat sky color
    return glm::mix(night, day, SkyFactor());
}

glm::vec3 DayNightCycle::HorizonColor() const
{
    // Minecraft's horizon is the fog color; paler than zenith
    const glm::vec3 night(0.04f, 0.05f, 0.10f);
    const glm::vec3 day(0.70f, 0.85f, 1.0f);
    return glm::mix(night, day, SkyFactor());
}

glm::vec4 DayNightCycle::SunsetColor() const
{
    static constexpr float RANGE = 0.4f;    // Active while sun is within +-0.4 of the horizon
    const float h = SunDir().y;

    if (h < -RANGE || h > RANGE)
        return glm::vec4(0.0f);

    const float f = h / RANGE * 0.5f + 0.5f;    // 0 -> sun below, 1 -> sun above
    float strength = 1.0f - (1.0f - std::sin(f * glm::pi<float>())) * 0.99f;
    strength *= strength;

    return glm::vec4(f * 0.3f + 0.7f, f * f * 0.7f + 0.2f, 0.2f, strength);
}

// Sun's angle on its orbit, 0 at sunrise
// Stars rotate by the same angle so the whole sky rotates as one
float DayNightCycle::CelestialAngle() const
{
    return (time - 0.25f) * glm::two_pi<float>();
}

float DayNightCycle::StarStrength() const
{
    const float n = 1.0f - SkyFactor();
    return (n * n);
}