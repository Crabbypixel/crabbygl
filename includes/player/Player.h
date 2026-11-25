#pragma once
#include <glm/glm.hpp>

class World;

class Player
{
public:
    Player() = default;

    // Player is unique, immovable and non-copyable
    Player(const Player&) = delete;
    Player(Player&&) = delete;
    Player& operator=(const Player&) = delete;
    Player& operator=(Player&&) = delete;

    // Dimensions
    static constexpr float HALF_W = 0.30f;      // AABB half-width (total 0.6)
    static constexpr float HEIGHT = 1.80f;      // AABB full height
    static constexpr float EYE_OFF = 1.60f;     // eye height above feet
    static constexpr float SPEED = 5.0f;
    static constexpr float JUMP_VEL = 8.0f;
    static constexpr float GRAVITY = -24.0f;
    static constexpr float MAX_FALL = -50.0f;
    static constexpr float BOOST_MULTIPLIER = 15.0f;

    void Update(
        float dt,
        const glm::vec3& camFront,
        bool fwd, bool back, bool left, bool right,
        bool jump, bool crouch, bool booster,
        bool shouldUpdateControls,
        const World& world
    );

    [[nodiscard]] glm::vec3 EyePos() const noexcept { return m_pos + glm::vec3(0.0f, EYE_OFF, 0.0f); }
    void SetPos(const glm::vec3 pos) noexcept { m_pos = pos; }
    [[nodiscard]] const glm::vec3& GetPos() const noexcept { return m_pos; }
    [[nodiscard]] const glm::vec3& GetVelcoty() const noexcept { return m_vel; }
    [[nodiscard]] bool IsOnGround() const noexcept { return m_onGround; }
    [[nodiscard]] bool CanFly() const noexcept { return m_canFly; }
    void ToggleFly() noexcept { m_canFly = !m_canFly; }

private:
    glm::vec3 m_pos = glm::vec3(0.0f, 10.0f, 0.0f);  // feet
    glm::vec3 m_vel = glm::vec3(0.0f);
    bool m_onGround = false;
    bool m_canFly = true;

    void ResolveX(const World& world);
    void ResolveY(const World& world);
    void ResolveZ(const World& world);
};