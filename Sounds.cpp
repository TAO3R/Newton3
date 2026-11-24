#include "Sounds.hpp"
#include "data_path.hpp"

Load< std::vector< Sound::Sample > > hit_samples(LoadTagDefault, []() -> std::vector< Sound::Sample > const * {
	auto *samples = new std::vector< Sound::Sample >();
	samples->emplace_back(data_path("hit01.wav"));
	samples->emplace_back(data_path("hit02.wav"));
	samples->emplace_back(data_path("hit03.wav"));
    samples->emplace_back(data_path("player_hit.wav"));
	return samples;
});

Load< Sound::Sample > player_hit_sample(LoadTagDefault, []() -> Sound::Sample const * {
	return new Sound::Sample(data_path("player_hit.wav"));
});

Load< Sound::Sample > escaping_sample(LoadTagDefault, []() -> Sound::Sample const * {
	return new Sound::Sample(data_path("rising_sound.wav"));
});

Load< Sound::Sample > jet_sample(LoadTagDefault, []() -> Sound::Sample const * {
	return new Sound::Sample(data_path("jet.wav"));
});

Load< Sound::Sample > bgm_sample(LoadTagDefault, []() -> Sound::Sample const * {
	return new Sound::Sample(data_path("bgm.wav"));
});