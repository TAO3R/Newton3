#pragma once

#include "GL.hpp"
#include "load_save_png.hpp"

#include <glm/glm.hpp>

#include <string>
#include <vector>

//an image loaded from a png and (once uploaded) stored in a GL texture:
struct Sprite {
	std::string name;

	glm::uvec2 size;
	std::vector< glm::u8vec4 > data;
	OriginLocation origin = LowerLeftOrigin;
	GLuint tex = 0;
};

//vertex format for drawing sprites with color_texture_program:
struct SpriteVertex {
	glm::vec2 position;
	glm::vec2 texCoord;
	glm::u8vec4 color;
};
