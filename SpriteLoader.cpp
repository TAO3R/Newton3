#include "SpriteLoader.hpp"

#include "data_path.hpp"
#include "gl_errors.hpp"

#include <cassert>

SpriteLoader::SpriteLoader() {
}

SpriteLoader::~SpriteLoader() {
}

Sprite SpriteLoader::load_sprite(std::string name) {
	Sprite s;
	load_png(data_path(name), &(s.size), &(s.data), s.origin);

	return s;
}

void SpriteLoader::add_sprite(Sprite &s) {
	sprites.push_back(s);
}

void SpriteLoader::upload_sprites() {
	for (Sprite &s : sprites) {
		if (s.tex != 0) continue;

		GLuint tex;
		glGenTextures(1, &tex); //generates '1' texture names and let 'tex' receive that name

		glBindTexture(GL_TEXTURE_2D, tex); //bind a named texture object 'tex' to a specific texturing target
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, s.size.x, s.size.y, 0, GL_RGBA, GL_UNSIGNED_BYTE, s.data.data());
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glBindTexture(GL_TEXTURE_2D, 0);

		s.tex = tex;

		//clear pixel data on cpu
	}

	GL_ERRORS();
}

void SpriteLoader::remove_sprites(Sprite *s) {
	bool found = false;
	for (auto si = sprites.begin(); si != sprites.end(); ++si) {
		if (&*si == s) {
			sprites.erase(si);
			found = true;
			break;
		}
	}

	assert(found);
}
