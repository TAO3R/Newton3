#pragma once

#include "Mode.hpp"
#include "Sound.hpp"
#include "GL.hpp"

#include <functional>

/*
 *
 * Intro for 15-466-f26, including the class logo and a little sound drop.
 *
 * Usage: construct with a function that sets your main mode as current as a parameter.
 *
 */

struct GP26IntroMode : Mode {
	GP26IntroMode(std::function< void() > const &on_finished);
	virtual ~GP26IntroMode();

	//functions called by main loop:
	virtual bool handle_event(SDL_Event const &, glm::uvec2 const &window_size) override;
	virtual void update(float elapsed) override;
	virtual void draw(glm::uvec2 const &drawable_size) override;

	//run through the animation and print out frame timings:
	void benchmark();

	//shader and vertex buffer basically the same as GP22IntroMode:
	struct {
		GLuint program = -1U;
		GLuint CLIP_FROM_OBJECT_mat4 = -1U;
	} color;

	GLuint vertex_buffer = -1U;
	GLuint vertex_buffer_for_color_program = -1U; //vertex array object

	struct Vertex {
		Vertex(glm::vec2 const &Position_, glm::u8vec4 Color_) : Position(Position_), Color(Color_) { }
		glm::vec2 Position;
		glm::u8vec4 Color;
	};

	std::vector< std::vector< glm::vec2 > > letters;
	std::vector< std::vector< glm::vec2 > > positions;
	std::vector< std::vector< glm::vec2 > > velocities;

	//computed in draw():
	glm::vec2 screen_min = glm::vec2(-1000.0f,-1000.0f);
	glm::vec2 screen_max = glm::vec2( 1000.0f, 1000.0f);

	float circle_r = 0.0f;

	float time = 0.0f;

	std::shared_ptr< Sound::PlayingSample > music;

	void finish(); //sets current mode to null, then calls [a local copy of] on_finished
	std::function< void() > on_finished;

	bool loop_forever = false; //Loop forever. For debugging/testing.
};
