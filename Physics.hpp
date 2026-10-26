#pragma once

#include <cstdint>

struct Physics
{
    enum class Type : uint8_t
    {
        Static = 0,
        Dynamic = 1,
        Kinematic = 2,
        Count = 3
    } type;

    float const elasticity;
    float const mass;
    
};