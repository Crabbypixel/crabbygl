#include "player/Player.h"
#include "world/World.h"

#include <glm/glm.hpp>
#include <algorithm>
#include <cmath>

// ----- helpers ---------------------------------------------------------

// ----- axis-separated resolution ---------------------------------------
// Each axis resolved independently to prevent corner-locking
void Player::ResolveX(const World& world)
{
    const float e = 0.001f;
    // Epsilon contracts Y and Z — avoids sampling floor/wall blocks at exact boundaries
    int y0 = (int)std::floor(m_pos.y + e);
    int y1 = (int)std::floor(m_pos.y + HEIGHT - e);
    int z0 = (int)std::floor(m_pos.z - HALF_W + e);
    int z1 = (int)std::floor(m_pos.z + HALF_W - e);

    if (m_vel.x > 0.0f)
    {
        float px1 = m_pos.x + HALF_W;
        int   bx = (int)std::floor(px1);
        float push = 0.0f;
        for (int y = y0; y <= y1; ++y)
            for (int z = z0; z <= z1; ++z)
            {
                if (!world.IsSolid(bx, y, z)) continue;
                float pen = px1 - (float)bx;
                if (pen > 0.0f) push = std::min(push, -pen);
            }
        m_pos.x += push;
        if (push != 0.0f) m_vel.x = 0.0f;
    }
    else if (m_vel.x < 0.0f)
    {
        float px0 = m_pos.x - HALF_W;
        int   bx = (int)std::floor(px0);
        float push = 0.0f;
        for (int y = y0; y <= y1; ++y)
            for (int z = z0; z <= z1; ++z)
            {
                if (!world.IsSolid(bx, y, z)) continue;
                float pen = (float)(bx + 1) - px0;
                if (pen > 0.0f) push = std::max(push, pen);
            }
        m_pos.x += push;
        if (push != 0.0f) m_vel.x = 0.0f;
    }
}

void Player::ResolveZ(const World& world)
{
    const float e = 0.001f;
    int x0 = (int)std::floor(m_pos.x - HALF_W + e);
    int x1 = (int)std::floor(m_pos.x + HALF_W - e);
    int y0 = (int)std::floor(m_pos.y + e);
    int y1 = (int)std::floor(m_pos.y + HEIGHT - e);

    if (m_vel.z > 0.0f)
    {
        float pz1 = m_pos.z + HALF_W;
        int   bz = (int)std::floor(pz1);
        float push = 0.0f;
        for (int x = x0; x <= x1; ++x)
            for (int y = y0; y <= y1; ++y)
            {
                if (!world.IsSolid(x, y, bz)) continue;
                float pen = pz1 - (float)bz;
                if (pen > 0.0f) push = std::min(push, -pen);
            }
        m_pos.z += push;
        if (push != 0.0f) m_vel.z = 0.0f;
    }
    else if (m_vel.z < 0.0f)
    {
        float pz0 = m_pos.z - HALF_W;
        int   bz = (int)std::floor(pz0);
        float push = 0.0f;
        for (int x = x0; x <= x1; ++x)
            for (int y = y0; y <= y1; ++y)
            {
                if (!world.IsSolid(x, y, bz)) continue;
                float pen = (float)(bz + 1) - pz0;
                if (pen > 0.0f) push = std::max(push, pen);
            }
        m_pos.z += push;
        if (push != 0.0f) m_vel.z = 0.0f;
    }
}

void Player::ResolveY(const World& world)
{
    const float e = 0.001f;
    int x0 = (int)std::floor(m_pos.x - HALF_W + e);
    int x1 = (int)std::floor(m_pos.x + HALF_W - e);
    int z0 = (int)std::floor(m_pos.z - HALF_W + e);
    int z1 = (int)std::floor(m_pos.z + HALF_W - e);

    if (m_vel.y < 0.0f)
    {
        int   by = (int)std::floor(m_pos.y);
        float push = 0.0f;
        for (int x = x0; x <= x1; ++x)
            for (int z = z0; z <= z1; ++z)
            {
                if (!world.IsSolid(x, by, z)) continue;
                float pen = (float)(by + 1) - m_pos.y;
                if (pen > 0.0f) push = std::max(push, pen);
            }
        m_pos.y += push;
        if (push != 0.0f) { m_onGround = true; m_vel.y = 0.0f; }
    }
    else if (m_vel.y > 0.0f)
    {
        float py1 = m_pos.y + HEIGHT;
        int   by = (int)std::floor(py1);
        float push = 0.0f;
        for (int x = x0; x <= x1; ++x)
            for (int z = z0; z <= z1; ++z)
            {
                if (!world.IsSolid(x, by, z)) continue;
                float pen = py1 - (float)by;
                if (pen > 0.0f) push = std::min(push, -pen);
            }
        m_pos.y += push;
        if (push != 0.0f) m_vel.y = 0.0f;
    }
}

// ----- main update ------------------------------------------------
void Player::Update(float dt, const glm::vec3& camFront, bool fwd, bool back, bool left, bool right, bool jump, bool crouch, bool booster, bool shouldUpdateControls, const World& world)
{
    // XZ movement — flatten camFront onto horizontal plane
    glm::vec3 flatFront = glm::normalize(glm::vec3(camFront.x, 0.0f, camFront.z));
    glm::vec3 flatRight = glm::normalize(glm::cross(flatFront, glm::vec3(0.0f, 1.0f, 0.0f)));

    glm::vec3 move(0.0f);

    if (shouldUpdateControls)
    {
        if (fwd)   move += flatFront;
        if (back)  move -= flatFront;
        if (right) move += flatRight;
        if (left)  move -= flatRight;
    }

    if (glm::length(move) > 0.001f)
        move = glm::normalize(move);

    m_vel.x = (booster ? BOOST_MULTIPLIER : 1.0f) * move.x * SPEED;
    m_vel.z = (booster ? BOOST_MULTIPLIER : 1.0f) * move.z * SPEED;

    // Gravity
    if (!m_canFly)
    {
        m_vel.y += GRAVITY * dt;
        m_vel.y = std::max(m_vel.y, MAX_FALL);
    }
    else
    {
        if (jump)
            m_vel.y = (booster ? BOOST_MULTIPLIER : 1.0f) * SPEED;
        else if (crouch)
            m_vel.y = -(booster ? BOOST_MULTIPLIER : 1.0f) * SPEED;
        else
            m_vel.y = 0.0f;
    }

    // Jump — single frame trigger, only when grounded
    if (shouldUpdateControls && jump && m_onGround && !m_canFly)
    {
        m_vel.y = JUMP_VEL;
        m_onGround = false;
    }

    // Move + resolve each axis independently
    m_pos.x += m_vel.x * dt;  ResolveX(world);
    m_onGround = false;        // reset before Y resolve
    m_pos.y += m_vel.y * dt;  ResolveY(world);   // sets onGround = true if floor hit
    m_pos.z += m_vel.z * dt;  ResolveZ(world);
}