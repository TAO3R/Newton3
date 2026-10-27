#pragma once

#include <string>

#include <ft2build.h>
#include FT_FREETYPE_H

#include <hb.h>
#include <hb-ft.h>

#include "GL.hpp"
#include <glm/glm.hpp>

struct TextTexture {
	GLuint texture = 0;

	int width = 0;
	int height = 0;
};

struct TextRenderer {
	TextRenderer(std::string const &font_path);
	~TextRenderer();
	TextRenderer(TextRenderer const &) = delete;
	TextRenderer &operator=(TextRenderer const &) = delete;
	TextTexture make_text(std::string const &text);
	FT_Library library = nullptr;
	FT_Face face = nullptr;
	hb_font_t *hb_font = nullptr;
	
	//OpenGL resources for drawing text textures.
	GLuint program = 0;
	GLint text_texture_uniform = -1;
	GLint text_color_uniform = -1;
	GLuint vao = 0;
	GLuint vbo = 0;

	void draw_text(
	TextTexture const &text,
	glm::vec2 const &position,
	float scale,
	glm::vec3 const &color,
	glm::uvec2 const &drawable_size
	);
	
	void destroy_text(TextTexture &text);
};

