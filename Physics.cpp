#include "Physics.hpp"

void PhysicsEngine::advance(std::vector< Body > const &bodies, float dt)
{
    for (PhysicsEngine::Body const &b : bodies)
    {
        if (b.pp->type != PhysicsEngine::MovementType::Static)
        {
            *b.position += b.pp->velocity * dt;
        }
    }
}

// axis-aligned box vs box: overlapping on both axes => hit, separated along the axis of least penetration
bool PhysicsEngine::detect_collision(Body const &b1, Body const &b2, Collision *hit)
{
    glm::vec2 d = *b2.position - *b1.position;
    glm::vec2 overlap = (b1.pp->box.half_size + b2.pp->box.half_size) - glm::abs(d);
    if (overlap.x <= 0.0f || overlap.y <= 0.0f) { return false; }

    // exactly coincident centers have no direction, so they fall back to +x / +y
    if (overlap.x < overlap.y)
    {
        hit->normal = glm::vec2(d.x < 0.0f ? -1.0f : 1.0f, 0.0f);
        hit->depth = overlap.x;
    }
    else
    {
        hit->normal = glm::vec2(0.0f, d.y < 0.0f ? -1.0f : 1.0f);
        hit->depth = overlap.y;
    }
    return true;
}

// push the pair apart and bounce them, both split by inverse mass (static / kinematic side doesn't budge)
void PhysicsEngine::handle_collision(Body const &b1, Body const &b2, Collision const &c)
{
    float inv1 = b1.pp->inv_mass();
    float inv2 = b2.pp->inv_mass();
    float inv_sum = inv1 + inv2;
    if (inv_sum == 0.0f) { return; }    // neither side can be moved

    *b1.position -= c.normal * (c.depth * inv1 / inv_sum);
    *b2.position += c.normal * (c.depth * inv2 / inv_sum);

    float vn = glm::dot(b2.pp->velocity - b1.pp->velocity, c.normal);
    if (vn >= 0.0f) { return; }         // already separating
    float e = glm::min(b1.pp->elasticity, b2.pp->elasticity);
    float j = -(1.0f + e) * vn / inv_sum;
    b1.pp->velocity -= c.normal * (j * inv1);
    b2.pp->velocity += c.normal * (j * inv2);
}

// tick one physics_dt, apply this tick's collisions, grabs, throws
void PhysicsEngine::step(std::vector< Body > const &bodies, std::vector< Grab > const &grabs, std::vector< Throw > &throws)
{
    {   // move, then resolve every overlapping pair in index order
        advance(bodies, physics_dt);

        // O(n^2) single pass, discrete; broadphase grid if body count grows, swept test if fast bodies tunnel
        for (size_t i = 0; i < bodies.size(); i++)
        {
            for (size_t j = i + 1; j < bodies.size(); j++)
            {
                if (bodies[i].pp->inv_mass() == 0.0f && bodies[j].pp->inv_mass() == 0.0f) { continue; }
                Collision c;
                if (detect_collision(bodies[i], bodies[j], &c)) { handle_collision(bodies[i], bodies[j], c); }
            }
        }
    }

    {   // grabs

    }

    {   // throws

    }
}