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

// a held pair acts as one body: inverse of the summed mass, or 0 when anchored to something immovable
static float group_inv_mass(PhysicsEngine::Body const &b)
{
    float inv = b.pp->inv_mass();
    PhysicsEngine::PhysicsProp const *partner = b.pp->partner.pp;
    if (inv == 0.0f || !partner) { return inv; }
    if (partner->inv_mass() == 0.0f) { return 0.0f; }
    return 1.0f / (b.pp->mass + partner->mass);
}

// shift and accelerate b together with its partner, so a held pair keeps its offset and shared velocity
static void move_group(PhysicsEngine::Body const &b, glm::vec2 dp, glm::vec2 dv)
{
    *b.position += dp;
    b.pp->velocity += dv;
    PhysicsEngine::Body const &partner = b.pp->partner;
    if (partner.pp)
    {
        *partner.position += dp;
        partner.pp->velocity += dv;
    }
}

// holding on to something immovable (a dynamic body paired with a static one)
static bool anchored(PhysicsEngine::Body const &b)
{
    PhysicsEngine::PhysicsProp const *partner = b.pp->partner.pp;
    return b.pp->inv_mass() != 0.0f && partner && partner->inv_mass() == 0.0f;
}

// push the pair apart and bounce them, both split by inverse mass (static / kinematic side doesn't budge)
void PhysicsEngine::handle_collision(Body const &b1, Body const &b2, Collision const &c)
{
    float vn = glm::dot(b2.pp->velocity - b1.pp->velocity, c.normal);
    float e = glm::min(b1.pp->elasticity, b2.pp->elasticity);

    // an anchored holder lets go when the hit is harder than its grip (the impulse the anchor would have to absorb),
    // and then takes this hit empty-handed
    auto knocked_loose = [&](Body const &holder, Body const &hitter)
    {
        float inv = group_inv_mass(hitter);
        return anchored(holder) && inv != 0.0f && -(1.0f + e) * vn / inv > holder.pp->grip;
    };
    if (vn < 0.0f)
    {
        if (knocked_loose(b1, b2)) { release(b1); }
        if (knocked_loose(b2, b1)) { release(b2); }
    }

    float inv1 = group_inv_mass(b1);
    float inv2 = group_inv_mass(b2);
    float inv_sum = inv1 + inv2;
    if (inv_sum == 0.0f) { return; }    // neither side can be moved

    move_group(b1, -c.normal * (c.depth * inv1 / inv_sum), glm::vec2(0.0f));
    move_group(b2,  c.normal * (c.depth * inv2 / inv_sum), glm::vec2(0.0f));

    if (vn >= 0.0f) { return; }         // already separating
    float j = -(1.0f + e) * vn / inv_sum;
    move_group(b1, glm::vec2(0.0f), -c.normal * (j * inv1));
    move_group(b2, glm::vec2(0.0f),  c.normal * (j * inv2));
}

void PhysicsEngine::confine(std::vector< Body > const &bodies, glm::vec2 min, glm::vec2 max)
{
    for (Body const &b : bodies)
    {
        if (group_inv_mass(b) == 0.0f) { continue; }  // static / kinematic / anchored aren't pushed by walls either
        glm::vec2 half = half_extent(b.pp->collider);
        glm::vec2 lo = min + half;
        glm::vec2 hi = max - half;
        for (int a = 0; a < 2; a++)
        {
            float pos = (*b.position)[a];
            float vel = b.pp->velocity[a];
            glm::vec2 dp(0.0f), dv(0.0f);
            if (pos < lo[a])
            {
                dp[a] = lo[a] - pos;
                dv[a] = std::abs(vel) * b.pp->elasticity - vel;
            }
            else if (pos > hi[a])
            {
                dp[a] = hi[a] - pos;
                dv[a] = -std::abs(vel) * b.pp->elasticity - vel;
            }
            else { continue; }
            move_group(b, dp, dv);
        }
    }
}

void PhysicsEngine::release(Body const &holder)
{
    Body held = holder.pp->partner;
    if (!held.pp) { return; }
    held.pp->partner = {};
    holder.pp->partner = {};
}

bool PhysicsEngine::apply_impulse(Body const &b, glm::vec2 impulse)
{
    float inv = group_inv_mass(b);
    if (inv == 0.0f) { return false; }
    move_group(b, glm::vec2(0.0f), impulse * inv);
    return true;
}

bool PhysicsEngine::can_be_grabbed(Body const &b)
{
    MovementType type = b.pp->type;
    return b.pp->grabbable && (type == MovementType::Dynamic || type == MovementType::Static) && !b.pp->partner.pp;
}

// reach = the holder's collider grown by range
bool PhysicsEngine::in_reach(Body const &holder, Body const &target, float range, float *depth)
{
    PhysicsProp reach = *holder.pp;
    if (auto *circle = std::get_if< CircleCollider >(&reach.collider)) { circle->radius += range; }
    else { std::get< BoxCollider >(reach.collider).half_size += glm::vec2(range); }

    Collision c;
    if (!detect_collision(Body{ holder.position, &reach }, target, &c)) { return false; }
    if (depth) { *depth = c.depth; }
    return true;
}

void PhysicsEngine::step(std::vector< Body > const &bodies, std::vector< Grab > &grabs, std::vector< Throw > &throws)
{
    {   // throws: unlink the pair, then +impulse on the held body and -impulse on the holder
        for (Throw const &t : throws)
        {
            Body held = t.holder.pp->partner;
            if (!held.pp) { continue; }     // not holding anything
            release(t.holder);
            held.pp->velocity += t.impulse * held.pp->inv_mass();
            t.holder.pp->velocity -= t.impulse * t.holder.pp->inv_mass();
        }
        throws.clear();
    }

    {   // grabs: link the holder to the nearest grabbable body in reach, merging momentum like an inelastic hit
        for (Grab const &g : grabs)
        {
            PhysicsProp *holder = g.holder.pp;
            if (holder->partner.pp) { continue; }   // already holding something

            // deepest overlap with the grown collider = nearest surface
            Body const *target = nullptr;
            float target_depth = 0.0f;
            for (Body const &b : bodies)
            {
                if (b.pp == holder || !can_be_grabbed(b)) { continue; }
                float depth;
                if (in_reach(g.holder, b, g.range, &depth) && (!target || depth > target_depth))
                {
                    target = &b;
                    target_depth = depth;
                }
            }
            if (!target) { continue; }

            // anchoring to something immovable just stops the holder against it
            float m1 = holder->mass;
            float m2 = target->pp->mass;
            glm::vec2 v = target->pp->inv_mass() == 0.0f ? target->pp->velocity
                : (m1 * holder->velocity + m2 * target->pp->velocity) / (m1 + m2);
            holder->velocity = v;
            target->pp->velocity = v;
            holder->partner = *target;
            target->pp->partner = g.holder;
        }
        grabs.clear();
    }

    {   // move, then resolve every overlapping pair in index order
        advance(bodies, physics_dt);

        // O(n^2) single pass, discrete; broadphase grid if body count grows, swept test if fast bodies tunnel
        for (size_t i = 0; i < bodies.size(); i++)
        {
            for (size_t j = i + 1; j < bodies.size(); j++)
            {
                if (bodies[i].pp->inv_mass() == 0.0f && bodies[j].pp->inv_mass() == 0.0f) { continue; }
                if (bodies[i].pp->partner.pp == bodies[j].pp) { continue; }    // a held pair doesn't collide with itself
                Collision c;
                if (detect_collision(bodies[i], bodies[j], &c)) { handle_collision(bodies[i], bodies[j], c); }
            }
        }
    }
}