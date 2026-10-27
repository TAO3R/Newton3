#pragma once

#include <glm/glm.hpp>

#include <cassert>
#include <cstdint>
#include <variant>
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

    // collider shapes, sizes in world-space units
    struct CircleCollider
    {
        float radius = 0.06f;
    };

    struct BoxCollider
    {
        glm::vec2 half_size = glm::vec2(0.06f); // axis-aligned
    };

    // exactly one shape at a time; the first alternative is what a default-constructed collider holds
    using Collider = std::variant< CircleCollider, BoxCollider >;

    // half size of the axis-aligned box around the collider
    static glm::vec2 half_extent(Collider const &collider);

    struct PhysicsProp;

    struct Body
    {
        glm::vec2 *position = nullptr;
        PhysicsProp *pp = nullptr;
    };

    struct PhysicsProp
    {
        MovementType type = MovementType::Dynamic;

        float mass = 1.0f;
        float elasticity = 0.8f;
        glm::vec2 velocity = glm::vec2(0.f);

        Collider collider;

        bool grabbable = false;     // players can grab it (dynamic: carried along; static: an anchor to hold on to)

        // the other half of a held pair (holder <-> held, set on both), empty when free
        // the pair moves as one body: shared velocity, combined mass in collisions and against walls
        // (holding a static body anchors the holder: it stays put until it lets go or pushes off)
        Body partner = {};

        // while anchored, a single hit whose impulse exceeds this knocks the holder loose
        float grip = 0.0f;

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

    // player actions this physics frame, applied once by step and then cleared

    // try to grab the nearest grabbable body whose collider is within 'range' of the holder's collider
    struct Grab
    {
        Body holder;
        float range = 0.0f;
    };

    // let go of whatever the holder holds, pushing it with 'impulse' and the holder with -impulse (zero = plain release)
    struct Throw
    {
        Body holder;
        glm::vec2 impulse = glm::vec2(0.0f);
    };

    static void advance(std::vector< Body > const &bodies, float dt);
    static bool detect_collision(Body const &b1, Body const &b2, Collision *hit);
    static void handle_collision(Body const &b1, Body const &b2, Collision const &c);

    // keep every collider inside [min, max], bouncing off the walls (a held pair is pushed back as one)
    static void confine(std::vector< Body > const &bodies, glm::vec2 min, glm::vec2 max);

    // drop whatever the holder holds, both keep their current velocity
    static void release(Body const &holder);

    // instant push on b (and whatever it's paired with), split by the group's mass; false if the group can't move
    static bool apply_impulse(Body const &b, glm::vec2 impulse);

    // free to be grabbed: grabbable, dynamic or static, not already held
    static bool can_be_grabbed(Body const &b);

    // target overlaps the holder's collider grown by 'range'; depth (optional) is larger for nearer targets
    static bool in_reach(Body const &holder, Body const &target, float range, float *depth = nullptr);

    // tick one physics_dt, apply this tick's throws, grabs, then movement and collisions
    // discrete test: bodies closing faster than about (r1 + r2) / physics_dt can pass through each other
    // (two 0.09 players: ~22 units/s)
    static void step(std::vector< Body > const &bodies, std::vector< Grab > &grabs, std::vector< Throw > &throws);
};
