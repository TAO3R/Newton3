#include "GP26IntroMode.hpp"

#include "gl_compile_program.hpp"
#include "gl_errors.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <iostream>
#include <algorithm>
#include <chrono>
#include <array>
#include <random>

//from GP22IntroMode:
static void stroke(std::vector< GP26IntroMode::Vertex > *attribs_,
		std::vector< glm::vec2 > const &path, std::vector< float > const &param, float r,
		glm::u8vec4 col_begin, std::vector< std::pair< float, glm::u8vec4 > > const &col_mid, glm::u8vec4 col_end) {
	assert(attribs_);
	auto &attribs = *attribs_;

	assert(param.size() == path.size());

	if (path.size() < 2) return;

	glm::vec2 min = glm::vec2(std::numeric_limits< float >::infinity());
	glm::vec2 max = glm::vec2(-std::numeric_limits< float >::infinity());

	std::vector< std::pair< float, glm::u8vec4 > > param_col;
	param_col.reserve(col_mid.size() + 2);
	param_col.emplace_back(0.0f, col_begin);
	param_col.insert(param_col.end(), col_mid.begin(), col_mid.end());
	param_col.emplace_back(param.back(), col_end);

	uint32_t pci = 0;

	bool first = true;
	auto emit = [&](glm::vec2 const &vert, float p) {
		while (pci + 1 < param_col.size() && param_col[pci+1].first <= p) ++pci;

		float amt = (p - param_col[pci].first) / (param_col[pci+1].first - param_col[pci].first);
		glm::u8vec4 col = glm::u8vec4(
			glm::mix(glm::vec4(param_col[pci].second), glm::vec4(param_col[pci+1].second), amt)
		);

		min = glm::min(min, vert);
		max = glm::max(max, vert);
		if (first && !attribs.empty()) attribs.emplace_back(attribs.back());
		attribs.emplace_back(vert, col);
		if (first && attribs.size() != 1) attribs.emplace_back(attribs.back());
		first = false;
	};

	//mitered line strip:
	glm::vec2 prev_along;
	{
		glm::vec2 along = glm::normalize(path[1] - path[0]);
		glm::vec2 perp = glm::vec2(-along.y, along.x);
		emit(path[0] - r * perp, param[0]);
		emit(path[0] + r * perp, param[0]);
		prev_along = along;
	}
	for (uint32_t i = 1; i + 1 < path.size(); ++i) {
		glm::vec2 along = glm::normalize(path[i+1] - path[i]);
		glm::vec2 perp = glm::vec2(-along.y, along.x);

		glm::vec2 avg_along = glm::normalize(along + prev_along);
		glm::vec2 avg_perp = glm::vec2(-avg_along.y, avg_along.x);

		float r_ = r / glm::max(glm::dot(avg_perp, perp), 0.5f);

		emit(path[i] - r_ * avg_perp, param[i]);
		emit(path[i] + r_ * avg_perp, param[i]);

		prev_along = along;
	}
	{
		glm::vec2 along = glm::normalize(path.back() - path[path.size()-2]);
		glm::vec2 perp = glm::vec2(-along.y, along.x);
		emit(path.back() - r * perp, param.back());
		emit(path.back() + r * perp, param.back());
	}
}


static std::unique_ptr< Sound::Sample > music_sample;

GP26IntroMode::GP26IntroMode(std::function< void() > const &on_finished_) : on_finished(on_finished_) {

	//this little synth thing based on:
	// https://github.com/15-466/15-466-f22-intro/blob/main/GP22IntroMode.cpp

	{ // ------ sound ------
		std::vector< float > data(10 * 48000, 0.0f);

		//triangle wave (lightly phase-modulated):
		[[maybe_unused]] auto triangle = [](float t) -> float {
			float pm = std::sin(t * 0.999f * 2.0f * float(M_PI));
			t += 0.1f * pm;
			t += 0.25f;
			return std::abs(4.0f * (t - std::floor(t)) - 2.0f) - 1.0f;
		};
		//sine wave (lightly phase-modulated + sub-bass):
		[[maybe_unused]] auto sine = [](float t) -> float {
			float pm = std::sin(t * 1.01f * 2.0f * float(M_PI));
			float sub = std::sin(0.5f * t * 2.0f * float(M_PI));
			return 0.5f * std::sin((t + 0.3f * pm) * 2.0f * float(M_PI)) + 0.5f * sub;
		};
		//square wave (lightly pulse-width-modulated):
		[[maybe_unused]] auto square = [](float t) -> float {
			float w = 0.5f + 0.1f * std::sin(t * 0.1f);
			return 0.5f * (t - std::floor(t) > w ? 1.0f : -1.0f);
		};
		//sawtooth wave:
		[[maybe_unused]] auto saw = [](float t) -> float {
			return (t - std::floor(t)) - 0.5f;
		};
		//"super saw" (stack of detuned saws):
		//loosely based on: https://blog.faderpro.com/techniques/supersaw-how-make-iconic-sound/
		[[maybe_unused]] auto supersaw = [&saw](float t) -> float {
			float w = 0.0f;
			w += saw(t);
			float f = 1.0f;
			for (uint32_t i = 1; i < 7; i += 2) {
				f *= 1.0f + 0.21f * (std::pow(2.0f, 1.0f / 12.0f) - 1.0f); //detune by 21% of one semitone
				w += saw(f * t);
				w += saw(f / t);
			}
			t *= 0.25f;
			constexpr float v = 0.7f;
			w += v * saw(t);
			f = 1.0f;
			for (uint32_t i = 1; i < 3; ++i) {
				f *= 1.0f + 0.07f * (std::pow(2.0f, 1.0f / 12.0f) - 1.0f); //detune by 7% of one semitone
				w += v * saw(f * t);
				w += v * saw(f / t);
			}

			return w;
		};
		//FM bell-like sound(?):
		//loosely based on: https://www.pluginboutique.com/articles/1873-FM-Synthesis-Cookbook-Five-Classic-FM-Sounds-and-How-They-Work
		[[maybe_unused]] auto bell = [](float t) -> float {
			float ratio = 5.71f;
			float mod = std::sin((ratio * t) * float(M_PI) * 2.0f) * 4.0f;
			mod *= std::exp2(-t / 30.0f);
			float w = std::sin((t + mod) * float(M_PI) * 2.0f);
			return 0.5f * w;
		};

		struct ADSR {
			float a; //attack
			float d; //decay
			float s; //sustain
			float r; //release
		};

		//play synth at given time for given length at given frequency with given wave:
		auto tone = [&](float start, float len, float hz, float vol, std::function< float(float) > const &wave, ADSR const &adsr){
			int32_t begin = int32_t(start * 48000);
			int32_t end = begin + int32_t((len + adsr.r) * 48000);
			end = std::min(end, int32_t(data.size()));

			for (int32_t sample = begin; sample < end; ++sample) {
				float t = (sample - begin + 0.5f) / 48000.0f;

				//envelope value:
				float env;
				if (t < adsr.a) env = t / adsr.a;
				else if (t < adsr.a + adsr.d) env = ((t - adsr.a) / adsr.d) * (adsr.s - 1.0f) + 1.0f;
				else if (t <= len) env = adsr.s;
				else env = std::min(1.0f, (t - len) / adsr.r) * (0.0f - adsr.s) + adsr.s;

				//simple, single-oscillator synth:
				float osc1 = wave(t * hz);

				data[sample] += vol * env * osc1;
			}
		};

		//midi note number to frequency (based on A4 being 440hz)
		[[maybe_unused]] auto midi2hz = [](float midi) -> float {
			return 440.0f * std::exp2( (midi - 69.0f) / 12.0f );
		};

		[[maybe_unused]] auto C = [](int32_t oct) { return 12.0f + 12.0f * oct; };
		[[maybe_unused]] auto Cs= [](int32_t oct) { return 13.0f + 12.0f * oct; };
		[[maybe_unused]] auto D = [](int32_t oct) { return 14.0f + 12.0f * oct; };
		[[maybe_unused]] auto Ds= [](int32_t oct) { return 15.0f + 12.0f * oct; };
		[[maybe_unused]] auto E = [](int32_t oct) { return 16.0f + 12.0f * oct; };
		[[maybe_unused]] auto F = [](int32_t oct) { return 17.0f + 12.0f * oct; };
		[[maybe_unused]] auto Fs= [](int32_t oct) { return 18.0f + 12.0f * oct; };
		[[maybe_unused]] auto G = [](int32_t oct) { return 19.0f + 12.0f * oct; };
		[[maybe_unused]] auto Gs= [](int32_t oct) { return 20.0f + 12.0f * oct; };
		[[maybe_unused]] auto A = [](int32_t oct) { return 21.0f + 12.0f * oct; };
		[[maybe_unused]] auto As= [](int32_t oct) { return 22.0f + 12.0f * oct; };
		[[maybe_unused]] auto B = [](int32_t oct) { return 23.0f + 12.0f * oct; };


		[[maybe_unused]] ADSR env0{
			.a = 0.15f,
			.d = 3.5f,
			.s = 0.0f,
			.r = 0.0f,
		};

		[[maybe_unused]] ADSR env1{
			.a = 0.5f,
			.d = 0.0f,
			.s = 1.0f,
			.r = 1.0f,
		};

		[[maybe_unused]] ADSR env2{
			.a = 2.0f,
			.d = 0.1f,
			.s = 1.0f,
			.r = 1.5f,
		};

		[[maybe_unused]] ADSR env3{
			.a = 2.5f,
			.d = 2.5f,
			.s = 0.0f,
			.r = 0.0f,
		};

		[[maybe_unused]] ADSR envR{
			.a = 0.5f,
			.d = 4.5f,
			.s = 0.0f,
			.r = 0.0f,
		};


		const float ofs = 0.7f;

		tone(ofs + 1.0f, 3.5f, midi2hz(Cs(4)), 0.2f, supersaw, env2);
		tone(ofs + 0.0f, 5.0f, midi2hz(Gs(3)), 0.7f, bell, env0);


		{ //gently re-center output (remove dc):
			float smoothed = 0.0f;
			for (float &s : data) {
				smoothed += 1e-4f * (s - smoothed);
				s -= smoothed;
			}
		}


		{ //run a basic 'digital reverb' over stuff:
			//use a delay line with a few different taps and feedbacks:
			std::array< float, 2 * 48000 > delay;
			for (auto &s : delay) s = 0.0f; //clear delay line
			uint32_t head = 0;
			auto tap = [&](float offset) -> float {
				return delay[(head + int32_t(delay.size()) - int32_t(std::floor(offset * 48000.0f))) % delay.size()];
			};
			float smoothed = 0.0f;
			for (float &s : data) {
				float wet = 1.0f * s + (
					+ 6.0f * tap(0.49f)
					+ 6.0f * tap(0.21f)
					+ 3.0f * tap(0.13f)
					+ 1.0f * tap(0.09f)
					) / 16.0f;
				smoothed += 0.9f * (wet - smoothed); //smooth off high frequencies before writing reverb buffer
				delay[head] = smoothed;
				head = (head + 1) % delay.size();
				s = 0.25f * tap(0.25f) + 0.6f * s;
			}
		}

		{ //do a low-pass filter on reverb output:
			constexpr float FAC = 0.05f;
			float smoothed = 0.0f;
			for (float &s : data) {
				smoothed += FAC * (s - smoothed);
				s = smoothed;
			}
			smoothed = 0.0f;
			for (float &s : data) {
				smoothed += FAC * (s - smoothed);
				s = smoothed;
			}
		}

		//some tones get mixed without reverb
		tone(ofs + 0.0f, 5.0f, midi2hz(F(0)), 0.7f, bell, env0);
		tone(ofs + 0.0f, 5.0f, midi2hz(F(3)), 0.7f, bell, env0);
		tone(0.0f, 1.5f, midi2hz(Cs(1)), 0.1f, square, envR);

		{ //do a low-pass filter on mixed output:
			constexpr float FAC = 0.6f;
			float smoothed = 0.0f;
			for (float &s : data) {
				smoothed += FAC * (s - smoothed);
				s = smoothed;
			}
			smoothed = 0.0f;
			for (float &s : data) {
				smoothed += FAC * (s - smoothed);
				s = smoothed;
			}
		}

		{ //fade out the last bit, just in case:
			constexpr uint32_t fade = 4*48000;
			for (uint32_t i = 0; i < fade; ++i) {
				float amt = (i + 0.5f) / float(fade);
				data[data.size()-1-i] *= amt;
			}
		}

		/*{ //DEBUG:
			std::ofstream dump("music-dump.f32", std::ios::binary);
			dump.write(reinterpret_cast< const char * >(data.data()), data.size() * 4);
		}*/


		if (!music_sample) { //<-- when playing repeatedly for testing, want to avoid deallocating sample
			music_sample = std::make_unique< Sound::Sample >(data);
		}

		music = Sound::play(*music_sample);
	}

	//--------------------------------------------

		// ------ shader ------
	//(based on ColorProgram.cpp)
	color.program = gl_compile_program(
		"#version 330\n"
		"uniform mat4 CLIP_FROM_OBJECT;\n"
		"in vec4 Position;\n"
		"in vec4 Color;\n"
		"out vec4 color;\n"
		"void main() {\n"
		"	gl_Position = CLIP_FROM_OBJECT * Position;\n"
		"	color = Color;\n"
		"}\n"
	,
		//fragment shader:
		"#version 330\n"
		"in vec4 color;\n"
		"out vec4 fragColor;\n"
		"void main() {\n"
		"	fragColor = color;\n"
		"}\n"
	);

	//look up the locations of vertex attributes:
	GLuint Position_vec4 = glGetAttribLocation(color.program, "Position");
	GLuint Color_vec4 = glGetAttribLocation(color.program, "Color");

	//look up the locations of uniforms:
	color.CLIP_FROM_OBJECT_mat4 = glGetUniformLocation(color.program, "CLIP_FROM_OBJECT");

	// ------ vertex buffer + vertex array object ------
	// (based on DrawLines.cpp)

	glGenVertexArrays(1, &vertex_buffer_for_color_program);
	glBindVertexArray(vertex_buffer_for_color_program);

	glGenBuffers(1, &vertex_buffer);
	glBindBuffer(GL_ARRAY_BUFFER, vertex_buffer);

	glVertexAttribPointer(
		Position_vec4, //attribute
		2, //size
		GL_FLOAT, //type
		GL_FALSE, //normalized
		sizeof(GP26IntroMode::Vertex), //stride
		(GLbyte *)0 + offsetof(GP26IntroMode::Vertex, Position) //offset
	);
	glEnableVertexAttribArray(Position_vec4);

	glVertexAttribPointer(
		Color_vec4, //attribute
		4, //size
		GL_UNSIGNED_BYTE, //type
		GL_TRUE, //normalized
		sizeof(GP26IntroMode::Vertex), //stride
		(GLbyte *)0 + offsetof(GP26IntroMode::Vertex, Color) //offset
	);
	glEnableVertexAttribArray(Color_vec4);

	glBindBuffer(GL_ARRAY_BUFFER, 0);

	glBindVertexArray(0);

	GL_ERRORS();

	// ------ logo ------
	{ //quick-n-hack-y svg path interpreter from GP22 intro; updated with some more features:
		std::vector< std::vector< glm::vec2 > > paths;
		glm::vec2 last = glm::vec2(0.0f, 0.0f);

		[[maybe_unused]] auto svg_L = [&paths,&last](std::vector< float > const &coords) {
			assert(!coords.empty() && coords.size() % 2 == 0);
			assert(!paths.empty());
			auto &path = paths.back();
			for (uint32_t i = 0; i + 1 < coords.size(); i += 2) {
				path.emplace_back(coords[i], coords[i+1]);
			}
			assert(!path.empty());
			last = path.back();
		};
		[[maybe_unused]] auto svg_M = [&paths,&last](std::vector< float > const &coords) {
			assert(!coords.empty() && coords.size() % 2 == 0);
			paths.emplace_back();
			auto &path = paths.back();
			path.emplace_back(coords[0], coords[1]);
			for (uint32_t i = 2; i + 1 < coords.size(); i += 2) {
				path.emplace_back(coords[i], coords[i+1]);
			}
			assert(!path.empty());
			last = path.back();
		};
		[[maybe_unused]] auto svg_m = [&svg_M,&last](std::vector< float > coords) {
			assert(coords.size() >= 2);
			coords[0] += last.x;
			coords[1] += last.y;
			for (uint32_t i = 2; i < coords.size(); ++i) {
				coords[i] += coords[i-2];
			}
			svg_M(coords);
		};
		[[maybe_unused]] auto svg_l = [&svg_L,&last](std::vector< float > coords) {
			assert(coords.size() >= 2);
			coords[0] += last.x;
			coords[1] += last.y;
			for (uint32_t i = 2; i < coords.size(); ++i) {
				coords[i] += coords[i-2];
			}
			svg_L(coords);
		};

		[[maybe_unused]] auto svg_H = [&paths,&last](std::vector< float > const &coords) {
			assert(!coords.empty());
			assert(!paths.empty());
			auto &path = paths.back();
			assert(!path.empty());
			for (uint32_t i = 0; i < coords.size(); i += 1) {
				path.emplace_back(coords[i], path.back().y);
			}
			last = path.back();
		};
		[[maybe_unused]] auto svg_V = [&paths,&last](std::vector< float > const &coords) {
			assert(!coords.empty());
			assert(!paths.empty());
			auto &path = paths.back();
			assert(!path.empty());
			for (uint32_t i = 0; i < coords.size(); i += 1) {
				path.emplace_back(path.back().x, coords[i]);
			}
			last = path.back();
		};
		[[maybe_unused]] auto svg_h = [&svg_H,&last](std::vector< float > coords) {
			assert(!coords.empty());
			coords[0] += last.x;
			for (uint32_t i = 1; i < coords.size(); ++i) {
				coords[i] += coords[i-1];
			}
			svg_H(coords);
		};
		[[maybe_unused]] auto svg_v = [&svg_V,&last](std::vector< float > coords) {
			assert(!coords.empty());
			coords[0] += last.y;
			for (uint32_t i = 1; i < coords.size(); ++i) {
				coords[i] += coords[i-1];
			}
			svg_V(coords);
		};


		[[maybe_unused]] auto svg_C = [&paths,&last](std::vector< float > const &coords) {
			assert(coords.size() >= 6 && (coords.size() % 6 == 0));
			assert(!paths.empty() && !paths.back().empty());
			auto &path = paths.back();
			for (uint32_t i = 0; i + 5 < coords.size(); i += 6) {
				glm::vec2 p0 = path.back();
				glm::vec2 p1 = glm::vec2(coords[i+0], coords[i+1]);
				glm::vec2 p2 = glm::vec2(coords[i+2], coords[i+3]);
				glm::vec2 p3 = glm::vec2(coords[i+4], coords[i+5]);

				for (uint32_t ti = 0; ti < 30; ++ti) {
					float t = (ti + 1.0f) / 30.0f;
					glm::vec2 p01 = glm::mix(p0, p1, t);
					glm::vec2 p12 = glm::mix(p1, p2, t);
					glm::vec2 p23 = glm::mix(p2, p3, t);
					glm::vec2 p012 = glm::mix(p01, p12, t);
					glm::vec2 p123 = glm::mix(p12, p23, t);
					glm::vec2 p = glm::mix(p012, p123, t);
					path.emplace_back(p);
				}
			}
			assert(!path.empty());
			last = path.back();
		};
		[[maybe_unused]] auto svg_c = [&svg_C,&last](std::vector< float > coords) {
			assert(coords.size() >= 6 && (coords.size() % 6 == 0));
			coords[0] += last.x;
			coords[1] += last.y;
			coords[2] += last.x;
			coords[3] += last.y;
			coords[4] += last.x;
			coords[5] += last.y;
			for (uint32_t i = 6; i + 5 < coords.size(); i += 6) {
				coords[i+0] += coords[i-2];
				coords[i+1] += coords[i-1];
				coords[i+2] += coords[i-2];
				coords[i+3] += coords[i-1];
				coords[i+4] += coords[i-2];
				coords[i+5] += coords[i-1];
			}
			svg_C(coords);
		};

		//the letters:

		//G:
		svg_M({50.15979f,55.0f});
		svg_h({15.0f});
		svg_v({20.0f});
		svg_h({-25.0f});
		svg_c({-10.0f,0.0f,-15.0f,-5.0f,-15.0f,-15.0f});
		svg_V({20.0f});
		svg_c({0.0f,-10.0f,5.0f,-15.0f,15.0f,-15.0f});
		svg_l({30.0f,0.0f});
		svg_c({10.0f,0.0f,15.0f,5.0f,15.0f,15.0f});
		svg_v({10.0f});

		//M:
		svg_M({105.53391f,45.0f});
		svg_h({35.0f});
		svg_c({10.0f,0.0f,15.0f,-5.0f,15.0f,-15.0f});
		svg_V({20.0f});
		svg_c({0.0f,-10.0f,-5.0f,-15.0f,-15.0f,-15.0f});
		svg_h({-35.0f});
		svg_v({40.0f,35.0f});

		//2 (top part):
		svg_M({170.10525f,15.0f});
		svg_h({30.0f});
		svg_c({17.05638f,0.150086f,25.0f,8.11247f,25.0f,25.0f,0.0f,11.62594f,-9.38639f,17.613383f,-10.70379f,18.668836f});
		svg_L({183.1705f,83.830844f});

		//(bottom part):
		svg_M({180.10525f,85.0f});
		svg_h({50.0f});

		//6:
		svg_M({270.47937f,15.0f});
		svg_h({-25.0f});
		svg_v({20.0f,50.0f});
		svg_h({25.0f});
		svg_c({10.0f,0.0f,15.0f,-5.0f,15.0f,-15.0f});
		svg_V({50.0f});
		svg_c({0.0f,-10.0f,-5.0f,-15.0f,-15.0f,-15.0f});
		svg_h({-25.0f});

		//lines:

		svg_M({0.0f,95.0f});
		svg_h({159.94547f});
		svg_v({10.0f});
		svg_H({310.0f});

		svg_M({20.0f,115.0f});
		svg_h({119.94547f});
		svg_v({10.0f});
		svg_H({290.0f});

		svg_M({40.0f,135.0f});
		svg_h({79.94546f});
		svg_v({10.0f});
		svg_H({270.0f});

		svg_M({60,155});
		svg_h({40.0f});
		svg_v({10.0f});
		svg_h({150.0f});

		svg_M({80.0f,175.0f});
		svg_v({10.0f});
		svg_h({150.0f});


		assert(!paths.empty());
		{ //offset and scale:
			glm::vec2 offset = glm::vec2(-155.0f,-95.0f);
			float scale = 1.0f;
			for (auto &path : paths) {
				for (auto &v : path) {
					v = scale * (v + offset);
					v.y = -v.y;
				}
			}
		}
		/*

		std::reverse(paths[2].begin(), paths[2].end());

		{ //last two letters get rotated copies:
			auto rc = [&](uint32_t si) {
				paths.emplace_back(paths.at(si));
				for (auto &v : paths.back()) {
					v = -v;
				}
			};
			rc(2);
			rc(3);
		}
		*/

		//assert(paths.size() == 6);

		letters = paths;
		positions = letters;
		velocities = letters;

		std::mt19937 mt(0x12341234);
		for (auto &p : positions) {
			for (auto &pt : p) {
				pt.x = (mt() / float(mt.max()) - 0.5f);
				pt.y = (mt() / float(mt.max()) - 0.5f);
			}
		}
		for (auto &v : velocities) {
			for (auto &vl : v) {
				vl.x = (mt() / float(mt.max()) - 0.5f);
				vl.y = (mt() / float(mt.max()) - 0.5f);
				vl *= 1000.0f;
			}
		}

	}


}

GP26IntroMode::~GP26IntroMode() {
}

void GP26IntroMode::benchmark() {
	std::cout << "benchmark" << std::endl;

	glFlush();
	glFinish();

	int w,h;
	SDL_GetWindowSizeInPixels(Mode::window, &w, &h);
	glm::uvec2 drawable_size = glm::uvec2(w, h);
	glViewport(0, 0, drawable_size.x, drawable_size.y);


	for (uint32_t iter = 0; iter < 4; ++iter) {
		double total = 0.0;
		double min = std::numeric_limits< double >::infinity();
		double max =-std::numeric_limits< double >::infinity();
		uint32_t frames = 10 * 30; //not as complete but faster to wait for
		for (uint32_t f = 0; f < frames; ++f) {
			auto before = std::chrono::high_resolution_clock::now();
			time = f / float(frames) * 10.0f;
			draw(drawable_size);
			glFlush();
			glFinish();
			auto after = std::chrono::high_resolution_clock::now();
			double ms = std::chrono::duration< double >(after - before).count() * 1000.0;
			total += ms;
			min = std::min(min, ms);
			max = std::max(max, ms);
		}
		std::cout << min << " " << total / double(frames) << " " << max << std::endl;
	}
}

bool GP26IntroMode::handle_event(SDL_Event const &evt, glm::uvec2 const &window_size) {
	if (evt.type == SDL_EVENT_KEY_DOWN) {
		if (evt.key.key == SDLK_PRINTSCREEN) {
			//ignore the "printscreen" key:
			return false;
		} else {
			finish();
			return true;
		}
	}
	return false;
}

void GP26IntroMode::update(float elapsed) {

	float amt = std::clamp(1.0f - (time - 0.2f) / 0.8f, 0.0f, 1.0f);
	amt = std::pow(amt, 2.0f);
	circle_r = amt * 0.5f * glm::length(screen_max - screen_min);

	{ //"physics"-ish behavior
		for (size_t pts = 0; pts < positions.size(); ++pts) {
			std::vector< glm::vec2 > &p = positions[pts];
			std::vector< glm::vec2 > &v = velocities[pts];
			std::vector< glm::vec2 > const &t = letters[pts];
			assert(p.size() == t.size());
			assert(v.size() == p.size());
			for (size_t i = 0; i < p.size(); ++i) {
				v[i] *= std::exp2(-elapsed / 0.2f);
				v[i] += 200.0f * (t[i] - p[i]) * elapsed;
				p[i] += v[i] * elapsed;

				if (circle_r > 10.0f) {
					float len = glm::length(p[i]);
					if (len > circle_r) {
						v[i] = glm::vec2(0.0f); //TODO
						p[i] *= circle_r / len;
					}
				}
			}
		}
	}

	time += elapsed;
	if (time > 10.0f) {
		finish();
		return;
	}
}

void GP26IntroMode::draw(glm::uvec2 const &drawable_size) {
	//based on GP22IntroMode's draw

	//"safe" box: 486 x 324 (3:2)

	//requested visible bounds:
	glm::vec2 scene_min = glm::vec2(-243.0f, -162.0f);
	glm::vec2 scene_max = glm::vec2( 243.0f,  162.0f);

	//compute matrix for vertex shader:
	glm::mat4 clip_from_object;

	{ //make sure scene_min - scene_max fits in drawable_size:
		float aspect = drawable_size.x / float(drawable_size.y);
		float scale = glm::min(
			2.0f * aspect / (scene_max.x - scene_min.x),
			2.0f / (scene_max.y - scene_min.y)
		);
		clip_from_object = glm::mat4(
			scale / aspect, 0.0f, 0.0f, 0.0f,
			0.0f, scale, 0.0f, 0.0f,
			0.0f, 0.0f, 1.0f, 0.0f,
			scale / aspect * -0.5f * (scene_max.x + scene_min.x), scale * -0.5f * (scene_max.y + scene_min.y), 0.0f, 1.0f
		);

		screen_min = 0.5f * (scene_max + scene_min) - glm::vec2(aspect, 1.0f) / scale;
		screen_max = 0.5f * (scene_max + scene_min) + glm::vec2(aspect, 1.0f) / scale;
	}
	
	//compute triangles to draw:
	std::vector< Vertex > attribs;
	
	//--- gradient drawing helpers ---
	glm::vec2 grad_start = glm::vec2(0.0f, 0.0f);
	glm::vec2 grad_end = glm::vec2(0.0f, 100.0f);
	uint32_t c0 = 0xff00ff;
	uint32_t c1 = 0xff00ff;

	//split pts so that eqn.xy * pt + eqn.z >= 0.0f goes in 'pos', others go in 'neg':
	// degenerate polygons just dropped
	auto split = [](std::vector< glm::vec2 > const &pts, glm::vec3 eqn,
		std::vector< glm::vec2 > *neg, std::vector< glm::vec2 > *pos) {
		if (pts.size() < 3) return;

		glm::vec2 prev = pts.back();
		float prev_val = glm::dot(prev, glm::vec2(eqn)) + eqn.z;

		for (glm::vec2 const &next : pts) {
			float next_val = glm::dot(next, glm::vec2(eqn)) + eqn.z;
			if ((prev_val >= 0.0f) != (next_val >= 0.0f)) {
				glm::vec2 mid = glm::mix(prev, next, (0.0f - prev_val) / (next_val - prev_val));
				if (neg) neg->emplace_back(mid);
				if (pos) pos->emplace_back(mid);
			}
			if (next_val >= 0.0f) {
				if (pos) pos->emplace_back(next);
			} else {
				if (neg) neg->emplace_back(next);
			}
			prev = next;
			prev_val = next_val;
		}
	};
	auto grad_poly = [&](std::vector< glm::vec2 > const &pts) {
		if (pts.size() < 3) return;

		glm::vec2 grad_along = grad_end - grad_start;
		std::vector< glm::vec2 > const0, temp, med, const1;
		split(pts, glm::vec3(grad_along, glm::dot(grad_along,-grad_start)), &const0, &temp);
		split(temp, glm::vec3(grad_along, glm::dot(grad_along,-grad_end)), &med, &const1);

		glm::vec3 amt_eqn = glm::vec3(grad_along, glm::dot(grad_along, -grad_start));
		amt_eqn /= glm::dot(amt_eqn, glm::vec3(grad_end, 1.0f));

		// https://entropymine.com/imageworsener/srgbformula/
		auto srgb2linear = [](uint32_t v255) {
			float v = float(v255) / 255.0f;
			if (v <= 0.04045f) v /= 12.92f;
			else v = std::pow((v + 0.055f) / 1.055f, 2.4f);
			return v * 255.0f;
		};

		glm::vec4 color0( srgb2linear((c0 >> 24) & 0xff), srgb2linear((c0 >> 16) & 0xff), srgb2linear((c0 >> 8) & 0xff), c0 & 0xff );
		glm::vec4 color1( srgb2linear((c1 >> 24) & 0xff), srgb2linear((c1 >> 16) & 0xff), srgb2linear((c1 >> 8) & 0xff), c1 & 0xff );

		auto color = [&](glm::vec2 const &pt) {
			float amt = glm::dot(amt_eqn, glm::vec3(pt, 1.0f));
			glm::vec4 c = glm::mix(color0, color1, std::clamp(amt, 0.0f, 1.0f));
			return glm::u8vec4(uint8_t(c.r), uint8_t(c.g), uint8_t(c.b), uint8_t(c.a));
		};

		auto poly = [&](std::vector< glm::vec2 > const &pts) {
			if (!attribs.empty()) attribs.emplace_back(attribs.back());
			for (size_t i = 0; 2 * i + 1 < pts.size(); i += 1) {
				attribs.emplace_back(pts[i], color(pts[i]));
				if (i == 0 && attribs.size() > 1) attribs.emplace_back(attribs.back());
				attribs.emplace_back(pts[pts.size()-1-i], color(pts[pts.size()-1-i]));
			}
			if (pts.size() % 2 != 0) {
				attribs.emplace_back(pts[pts.size()/2], color(pts[pts.size()/2]));
				attribs.emplace_back(attribs.back());
			}
		};

		poly(const0);
		poly(med);
		poly(const1);
	};

	{ //--- gradient background ---
		auto vibe = [](float t) {
			return glm::vec2(
				0.1f * std::cos(t / 0.3f * 2.0f * float(M_PI)) + 0.5f * std::cos(2.0f + t / 2.0f * 2.0f * float(M_PI)),
				0.1f * std::sin(t / 0.3f * 2.0f * float(M_PI)) + 0.5f * std::sin(2.0f + t / 2.0f * 2.0f * float(M_PI))
			);
		};

		{ //blue layer
			grad_start = glm::vec2(181.0f,-64.0f);
			grad_end = glm::vec2(grad_start.x - 20.0f, screen_max.y);
			c0 = 0xe4f2f1ff;
			c1 = 0xa1fff8ff;

			grad_poly({
				glm::vec2(screen_min.x, screen_min.y),
				glm::vec2(screen_max.x, screen_min.y),
				glm::vec2(screen_max.x, screen_max.y),
				glm::vec2(screen_min.x, screen_max.y),
			});
		}
		{ //red layer
			glm::vec2 pt = glm::vec2(181.0f,-64.0f) + vibe(time + 2.0f);
			glm::vec2 dir = glm::vec2(75.0f, 141.0f);

			grad_start = pt;
			grad_end = glm::normalize(glm::vec2(dir.y, -dir.x)) * 76.0f + grad_start;
			c0 = 0xec9898ff;
			c1 = 0xe67a7aff;

			glm::vec2 pt0 = pt + dir * 1.01f * ( (screen_min.y - pt.y) / dir.y );
			glm::vec2 pt1 = pt + dir * 1.01f * ( (screen_max.x - pt.x) / dir.x );

			grad_poly({
				pt0,
				glm::vec2(screen_max.x, screen_min.y),
				pt1,
			});
		}

		{ //brown layer
			glm::vec2 pt = glm::vec2(90.0f,-77.0f) + vibe(time);
			glm::vec2 dir = glm::vec2(134.0f, 21.0f);

			grad_start = pt;
			grad_end = pt + glm::vec2(66.0f,-158.0f);
			c0 = 0xffdda6ff;
			c1 = 0xab6a00ff;

			glm::vec2 pt0 = pt + dir * 1.01f * ( (screen_min.x - pt.x) / dir.x );
			glm::vec2 pt1 = pt + dir * 1.01f * ( (screen_max.x - pt.x) / dir.x );

			grad_poly({
				pt0,
				glm::vec2(screen_min.x, screen_min.y),
				glm::vec2(screen_max.x, screen_min.y),
				pt1,
			});
		}

		if (circle_r > 0.0f) { //circle
			
			const glm::vec2 c = 0.5f * (screen_max + screen_min);
			constexpr size_t steps = 32;

			if (!attribs.empty()) attribs.emplace_back(attribs.back());
			for (size_t i = 0; i < steps; ++i) {
				float a = (i + 0.5f) / float(steps) * float(M_PI);
				float x = c.x + circle_r * std::cos(a);
				float y = c.y + circle_r * std::sin(a);

				attribs.emplace_back(glm::vec2(x,y), glm::u8vec4(0x00, 0x00, 0x00, 0xff));
				if (i == 0 && attribs.size() > 1) attribs.emplace_back(attribs.back());
				attribs.emplace_back(glm::vec2(x,-y), glm::u8vec4(0x00, 0x00, 0x00, 0xff));
			}
		}
	}

	//---- fade-out wipe ----
	if (time > 9.0f) {
		float wipe = std::clamp( (time - 9.0f) / 0.4f, 0.0f, 1.0f );
		glm::vec2 pt = glm::vec2(screen_max.x, screen_min.y);
		glm::vec2 dir = glm::vec2(0.5f * (screen_min.x + screen_max.x), screen_max.y) - pt;
		dir = glm::normalize(dir);

		float min = glm::dot(dir, glm::vec2(screen_max.x, screen_min.y) - pt);
		float max = glm::dot(dir, glm::vec2(screen_min.x, screen_max.y) - pt);

		grad_start = pt + dir * (glm::mix(min, max + 80.0f, wipe) );
		grad_end = pt + dir * (glm::mix(min, max + 80.0f, wipe) - 80.0f);
		c0 = 0x00000000;
		c1 = 0x000000ff;

		grad_poly({
			glm::vec2(screen_min.x, screen_min.y),
			glm::vec2(screen_max.x, screen_min.y),
			glm::vec2(screen_max.x, screen_max.y),
			glm::vec2(screen_min.x, screen_max.y),
		});
	}

	/* show target, for debug
	for (std::vector< glm::vec2 > const &path : letters) {
		std::vector< float > param;
		param.reserve(path.size());
		for (size_t i = 0; i < path.size(); ++i) {
			param.emplace_back( float(i) / float(path.size()) );
		}
		stroke(&attribs,
			path, param,
			5.0f,
			glm::u8vec4(0xff, 0xff, 0xff, 0xff), std::vector< std::pair< float, glm::u8vec4 > >{}, glm::u8vec4(0x00, 0x00, 0x00, 0xff)
		);
	}
	*/

	for (std::vector< glm::vec2 > const &path : positions) {
		std::vector< float > param;
		param.reserve(path.size());
		for (size_t i = 0; i < path.size(); ++i) {
			param.emplace_back( float(i) / float(path.size()) );
		}
		stroke(&attribs,
			path, param,
			5.0f,
			glm::u8vec4(0x00, 0x00, 0x00, 0xff), std::vector< std::pair< float, glm::u8vec4 > >{}, glm::u8vec4(0x00, 0x00, 0x00, 0xff)
		);
	}

	//----- actually draw ----
	// (Based on DrawLines.cpp)

	//upload attribs:
	glBindBuffer(GL_ARRAY_BUFFER, vertex_buffer); //set vertex_buffer as current
	glBufferData(GL_ARRAY_BUFFER, attribs.size() * sizeof(attribs[0]), attribs.data(), GL_STREAM_DRAW); //upload attribs array
	glBindBuffer(GL_ARRAY_BUFFER, 0);

	//Clear background:
	glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
	glClear(GL_COLOR_BUFFER_BIT);

	//No depth test, please, but yes let's use alpha blending:
	glDisable(GL_DEPTH_TEST);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	glUseProgram(color.program);
	glUniformMatrix4fv(color.CLIP_FROM_OBJECT_mat4, 1, GL_FALSE, glm::value_ptr(clip_from_object));

	glBindVertexArray(vertex_buffer_for_color_program);
	glDrawArrays(GL_TRIANGLE_STRIP, GLint(0), GLsizei(attribs.size()));
	glBindVertexArray(0);

	glUseProgram(0);

	glDisable(GL_BLEND);

	GL_ERRORS();
}


void GP26IntroMode::finish() {
	if (loop_forever) {
		if (music) music->set_volume(0.0f, 1.0f / 10.0f);
		music = Sound::play(*music_sample);
		time = 0.0f;
	} else {
		music->set_volume(0.0f, 1.0f / 10.0f);
		std::function< void() > on_finished_ = on_finished;
		Mode::set_current(nullptr);
		if (on_finished_) on_finished_();
	}
}
