#pragma once
#include "Sound.hpp"
#include "Load.hpp"
#include <vector>

extern Load< std::vector< Sound::Sample > > hit_samples;
extern Load< Sound::Sample > player_hit_sample;
extern Load< Sound::Sample > escaping_sample;
extern Load< Sound::Sample > jet_sample;
extern Load< Sound::Sample > bgm_sample;