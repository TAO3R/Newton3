#include "TextRenderer.hpp"
#include "gl_compile_program.hpp"
#include <stdexcept>
#include <iostream>
#include <vector>
#include <algorithm>


TextRenderer::TextRenderer(std::string const &font_path) {
	if (FT_Init_FreeType(&library)) {
		throw std::runtime_error("Failed to initialize FreeType.");
	}
	if (FT_New_Face(library, font_path.c_str(), 0, &face)) {
		FT_Done_FreeType(library);
		library = nullptr;
		throw std::runtime_error("Failed to load font: " + font_path);
	}
	if (FT_Set_Pixel_Sizes(face, 0, 48)) {
		FT_Done_Face(face);
		FT_Done_FreeType(library);
		face = nullptr;
		library = nullptr;
		throw std::runtime_error("Failed to set font size: " + font_path);
	}
	hb_font = hb_ft_font_create(face, nullptr);

	//Render glyph coverage with a configurable text color.
	program = gl_compile_program(
		"#version 330\n"
		"layout(location = 0) in vec2 Position;\n"
		"layout(location = 1) in vec2 TexCoord;\n"
		"out vec2 texCoord;\n"
		"void main() {\n"
		"    gl_Position = vec4(Position, 0.0, 1.0);\n"
		"    texCoord = TexCoord;\n"
		"}\n"
	,
		"#version 330\n"
		"in vec2 texCoord;\n"
		"uniform sampler2D TextTexture;\n"
		"uniform vec3 TextColor;\n"
		"out vec4 fragColor;\n"
		"void main() {\n"
		"    float coverage = texture(TextTexture, texCoord).r;\n"
		"    fragColor = vec4(TextColor, coverage);\n"
		"}\n"
	);
	text_texture_uniform = glGetUniformLocation(program, "TextTexture");
	text_color_uniform = glGetUniformLocation(program, "TextColor");
	glGenVertexArrays(1,&vao);
	glGenBuffers(1, &vbo);
	glBindVertexArray(vao);
	glBindBuffer(GL_ARRAY_BUFFER, vbo);

	glEnableVertexAttribArray(0);
	glVertexAttribPointer(
		0,
		2,
		GL_FLOAT,
		GL_FALSE,
		4 * sizeof(float),
		reinterpret_cast<void *>(0)
	);
	glEnableVertexAttribArray(1);
	glVertexAttribPointer(
		1,
		2,
		GL_FLOAT,
		GL_FALSE,
		4 * sizeof(float),
		reinterpret_cast<void *>(
			2 * sizeof(float)
		)
	);
	glBindBuffer(GL_ARRAY_BUFFER,0);
	glBindVertexArray(0);
}

TextRenderer::~TextRenderer() {
	if (vbo) glDeleteBuffers(1, &vbo);
	if (vao) glDeleteVertexArrays(1, &vao);
	if (program) glDeleteProgram(program);
	if (hb_font) {
		hb_font_destroy(hb_font);
		hb_font = nullptr;
	}
	if (face) {
		FT_Done_Face(face);
		face = nullptr;
	}
	if (library) {
		FT_Done_FreeType(library);
		library = nullptr;
	}
}

TextTexture TextRenderer::make_text(std::string const &text) {

	// Create a HarfBuzz buffer and shape the text
	hb_buffer_t *buffer = hb_buffer_create();
	hb_buffer_add_utf8(buffer, text.c_str(), -1, 0, -1);
	hb_buffer_guess_segment_properties(buffer);
	hb_shape(hb_font, buffer, nullptr, 0);
	unsigned int glyph_count = 0;
	hb_glyph_info_t *glyph_info = hb_buffer_get_glyph_infos(buffer, &glyph_count);
	hb_glyph_position_t *glyph_pos = hb_buffer_get_glyph_positions(buffer, &glyph_count);

	float pen_x = 0.0f;
	int min_x = 0;
	int max_x = 0;
	int min_y = 0;
	int max_y = 0;
	bool has_pixels = false;

	//First pass: Calculate the bounding box of the text
	for (unsigned int i = 0; i < glyph_count; ++i) {

		FT_UInt glyph_index = glyph_info[i].codepoint;
		FT_Load_Glyph(face, glyph_index, FT_LOAD_DEFAULT);
		FT_Render_Glyph(face->glyph, FT_RENDER_MODE_NORMAL);
		FT_GlyphSlot glyph = face->glyph;
		
		float x_offset = glyph_pos[i].x_offset / 64.0f;
		float y_offset = glyph_pos[i].y_offset / 64.0f;
		int glyph_left =static_cast<int>(pen_x + x_offset + glyph->bitmap_left);
		int glyph_top = -static_cast<int>(y_offset) - glyph->bitmap_top;
		int glyph_right = glyph_left + static_cast<int>(glyph->bitmap.width);
		int glyph_bottom = glyph_top + static_cast<int>(glyph->bitmap.rows);
		if (glyph->bitmap.width > 0 &&glyph->bitmap.rows > 0) {
			if (!has_pixels) {
				min_x = glyph_left;
				max_x = glyph_right;
				min_y = glyph_top;
				max_y = glyph_bottom;
				has_pixels = true;
			} 
			else {
				min_x = std::min(min_x, glyph_left);
				max_x = std::max(max_x, glyph_right);
				min_y = std::min(min_y, glyph_top);
				max_y = std::max(max_y, glyph_bottom);
			}
		}
		pen_x += glyph_pos[i].x_advance / 64.0f;
	}
	if (!has_pixels) {
		hb_buffer_destroy(buffer);
		return TextTexture();
	}
	int canvas_width =  max_x - min_x;
	int canvas_height = max_y - min_y;
	std::vector<unsigned char> canvas(canvas_width * canvas_height, 0);

	//Second pass: Render each glyph to the canvas
	pen_x = 0.0f;
	for (unsigned int i = 0; i < glyph_count; ++i) {
		FT_UInt glyph_index = glyph_info[i].codepoint;
		FT_Load_Glyph(face, glyph_index, FT_LOAD_DEFAULT);
		FT_Render_Glyph(face->glyph, FT_RENDER_MODE_NORMAL);
		FT_GlyphSlot glyph = face->glyph;

		float x_offset = glyph_pos[i].x_offset / 64.0f;
		float y_offset = glyph_pos[i].y_offset / 64.0f;
		int glyph_left = static_cast<int>(pen_x + x_offset + glyph->bitmap_left);
		int glyph_top = -static_cast<int>(y_offset) - glyph->bitmap_top;

		for (unsigned int y = 0; y < glyph->bitmap.rows; ++y) {
			for (unsigned int x = 0; x < glyph->bitmap.width; ++x) {
				unsigned char value =
					glyph->bitmap.buffer[y * glyph->bitmap.pitch + x];
				int canvas_x = glyph_left + x - min_x;
				int canvas_y = glyph_top + y - min_y;
				if (canvas_x >= 0 && canvas_x < canvas_width &&
					canvas_y >= 0 && canvas_y < canvas_height) {
					canvas[canvas_y * canvas_width + canvas_x] = value;
				}
			}
		}
		pen_x += glyph_pos[i].x_advance / 64.0f;
	}
	hb_buffer_destroy(buffer);

	//GL rendering part
	GLuint texture = 0;
	glGenTextures(1, &texture);
	glBindTexture(GL_TEXTURE_2D, texture);
	GLint unpack_alignment;
	glGetIntegerv(GL_UNPACK_ALIGNMENT, &unpack_alignment);
	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, canvas_width, canvas_height, 0, GL_RED, GL_UNSIGNED_BYTE, canvas.data());
	glPixelStorei(GL_UNPACK_ALIGNMENT, unpack_alignment);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

	//write result to struct and return
	TextTexture result;
	result.texture = texture;
	result.width = canvas_width;
	result.height = canvas_height;
	return result;
}


void TextRenderer::draw_text(TextTexture const &text, glm::vec2 const &position, float scale, glm::vec3 const &color,glm::uvec2 const &drawable_size) {
	if (text.texture == 0 || drawable_size.x == 0 || drawable_size.y == 0) {
		return;
	}

	float pixel_x = position.x; 
	float pixel_y = position.y;
	float pixel_w = static_cast<float>(text.width) * scale;
	float pixel_h = static_cast<float>(text.height) * scale;

	float left = 2.0f * pixel_x / static_cast<float>(drawable_size.x) - 1.0f;
	float right = 2.0f * (pixel_x + pixel_w) / static_cast<float>(drawable_size.x) - 1.0f;
	float top = 1.0f - 2.0f * pixel_y / static_cast<float>(drawable_size.y);
	float bottom = 1.0f - 2.0f * (pixel_y + pixel_h) / static_cast<float>(drawable_size.y);

	float vertices[] = {
		left,  top,    0.0f, 0.0f,
		right, top,    1.0f, 0.0f,
		right, bottom, 1.0f, 1.0f,

		left,  bottom, 0.0f, 1.0f,
		right, bottom, 1.0f, 1.0f,
		left,  top,    0.0f, 0.0f
	};

	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glUseProgram(program);
	glUniform3f(text_color_uniform,color.r, color.g,color.b);
	glUniform1i(text_texture_uniform, 0);
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, text.texture);
	glBindVertexArray(vao);
	glBindBuffer(GL_ARRAY_BUFFER, vbo);
	glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_DYNAMIC_DRAW);
	glDrawArrays(GL_TRIANGLES, 0, 6);
	glBindVertexArray(0);
	glUseProgram(0);
}

void TextRenderer::destroy_text(TextTexture &text) {
	if (text.texture != 0) {
		glDeleteTextures(1, &text.texture);
		text.texture = 0;
	}

	text.width = 0;
	text.height = 0;
}