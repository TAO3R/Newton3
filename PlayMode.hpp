#include "Mode.hpp"

#include "Connection.hpp"
#include "Game.hpp"
#include "SpriteLoader.hpp"
#include "TextRenderer.hpp"

#include <map>

#include <glm/glm.hpp>

#include <vector>
#include <deque>

struct PlayMode : Mode {
	PlayMode(Client &client);
	virtual ~PlayMode();

	//functions called by main loop:
	virtual bool handle_event(SDL_Event const &, glm::uvec2 const &window_size) override;
	virtual void update(float elapsed) override;
	virtual void draw(glm::uvec2 const &drawable_size) override;

	//----- game state -----

	//input tracking for local player:
	Player::Controls controls;

	//latest game state (from server):
	Game game;

	//last message from server:
	std::string server_message;

	//connection to server:
	Client &client;

	//Cached Outfit text textures, shared by scene labels and future UI.
	TextRenderer text_renderer;
	std::map< std::string, TextTexture > text_cache;

	// sprites
	SpriteLoader sl;

	GLuint vbo = 0, vao = 0;
};
