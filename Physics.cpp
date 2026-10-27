#include "Physics.hpp"

#include <cmath>

glm::vec2 PhysicsEngine::half_extent(Collider const &collider)
{
    if (auto *c = std::get_if< CircleCollider >(&collider)) { return glm::vec2(c->radius); }
    return std::get< BoxCollider >(collider).half_size;
}

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

// shape vs shape tests, each writes a normal pointing from the first shape toward the second

// circle vs circle: centers closer than the sum of radii => hit, separated along the line between centers
static bool circle_circle(glm::vec2 p1, float r1, glm::vec2 p2, float r2, PhysicsEngine::Collision *hit)
{
    glm::vec2 d = p2 - p1;
    float r = r1 + r2;
    float len2 = glm::dot(d, d);
    if (len2 >= r * r) { return false; }

    // exactly coincident centers have no direction, so they fall back to +x
    float len = std::sqrt(len2);
    hit->normal = len > 0.0f ? d / len : glm::vec2(1.0f, 0.0f);
    hit->depth = r - len;
    return true;
}

// circle vs axis-aligned box: closest point on the box closer than the radius => hit
static bool circle_box(glm::vec2 pc, float r, glm::vec2 pb, glm::vec2 half, PhysicsEngine::Collision *hit)
{
    glm::vec2 d = pc - pb;  // circle center, relative to the box center
    glm::vec2 closest = glm::clamp(d, -half, half);

    if (closest != d)
    {   // center outside the box: push out along closest point -> center
        glm::vec2 diff = d - closest;
        float dist2 = glm::dot(diff, diff);
        if (dist2 >= r * r) { return false; }
        float dist = std::sqrt(dist2);
        hit->normal = -diff / dist;
        hit->depth = r - dist;
        return true;
    }

    // center inside the box: push out through the nearest face (center exactly at the box center picks +x / +y)
    glm::vec2 gap = half - glm::abs(d);
    if (gap.x < gap.y)
    {
        hit->normal = glm::vec2(d.x < 0.0f ? 1.0f : -1.0f, 0.0f);
        hit->depth = gap.x + r;
    }
    else
    {
        hit->normal = glm::vec2(0.0f, d.y < 0.0f ? 1.0f : -1.0f);
        hit->depth = gap.y + r;
    }
    return true;
}

// axis-aligned box vs box: overlapping on both axes => hit, separated along the axis of least penetration
static bool box_box(glm::vec2 p1, glm::vec2 half1, glm::vec2 p2, glm::vec2 half2, PhysicsEngine::Collision *hit)
{
    glm::vec2 d = p2 - p1;
    glm::vec2 overlap = (half1 + half2) - glm::abs(d);
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

// pick the test for this pair of shapes; normal always points from b1 toward b2
bool PhysicsEngine::detect_collision(Body const &b1, Body const &b2, Collision *hit)
{
    auto *c1 = std::get_if< CircleCollider >(&b1.pp->collider);
    auto *c2 = std::get_if< CircleCollider >(&b2.pp->collider);
    auto *x1 = std::get_if< BoxCollider >(&b1.pp->collider);
    auto *x2 = std::get_if< BoxCollider >(&b2.pp->collider);

    if (c1 && c2) { return circle_circle(*b1.position, c1->radius, *b2.position, c2->radius, hit); }
    if (c1 && x2) { return circle_box(*b1.position, c1->radius, *b2.position, x2->half_size, hit); }
    if (x1 && c2)
    {   // same test with the pair swapped, so flip the normal back to b1 -> b2
        if (!circle_box(*b2.position, c2->radius, *b1.position, x1->half_size, hit)) { return false; }
        hit->normal = -hit->normal;
        return true;
    }
    return box_box(*b1.position, x1->half_size, *b2.position, x2->half_size, hit);
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