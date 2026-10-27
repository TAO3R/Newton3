#pragma once

#include <glm/glm.hpp>

#include <cassert>
#include <cstdint>
#include <vector>

struct PhysicsEngine
{
    static constexpr float physics_dt = 1.0f / 120.0f;	// seconds per tick
    
    enum class MovementType : uint8_t
    {
        Static = 0,
        Dynamic = 1,
        Kinematic = 2,
        Count = 3
    };

    struct PhysicsProp
    {
        MovementType type = MovementType::Dynamic;
        
        float mass = 1.0f;
        float elasticity = 0.8f;
        glm::vec2 velocity = glm::vec2(0.f);

        struct BoxCollider
        {
            glm::vec2 half_size = glm::vec2(0.06f); // world-space units, axis-aligned
        } box;

        // for static and kinematic collision
        float inv_mass() const
        {   
            assert(mass > 0.0f);
            return type == MovementType::Dynamic ? 1.0f / mass : 0.0f;
        }
    };

    struct Collision
    {
        glm::vec2 normal;   // unit, points from b1 toward b2
        float depth;        // penetration along normal, > 0
    };

    struct Body
    {
        glm::vec2 *position;
        PhysicsProp *pp;
    };

    // player actions
    struct Grab
    {

    };

    struct Throw
    {
        
    };

    static void advance(std::vector< Body > const &bodies, float dt);
    static bool detect_collision(Body const &b1, Body const &b2, Collision *hit);
    static void handle_collision(Body const &b1, Body const &b2, Collision const &c);
    
    // tick one physics_dt, apply this tick's collisions, grabs, throws
    static void step(std::vector< Body > const &bodies, std::vector< Grab > const &grabs, std::vector< Throw > &throws);
};
