#pragma once

#include "Sprite.hpp"

#include <string>
#include <vector>

//loads png files into sprites and uploads them as GL textures:
struct SpriteLoader {
	SpriteLoader();
	~SpriteLoader();

	std::vector< Sprite > sprites; //indexed by SpriteId

	Sprite load_sprite(std::string name);

	void add_sprite(Sprite &s);
	void upload_sprites();
	void remove_sprites(Sprite *s);
};
