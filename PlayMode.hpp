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

	//Call on the client each update; pass false when grabbing is unavailable.
	//Position is the local player's center in world coordinates.
	void set_grab_prompt(glm::vec2 const &player_position, bool visible);

	bool show_grab_prompt = false;
	glm::vec2 grab_prompt_position = glm::vec2(0.0f);

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

	//Local UI preview: game, victory, defeat, timeout.
	int result_preview = 0;

	// sprites
	SpriteLoader sl;

	//client-only debug: outline each sprite's quad (toggle with tab):
	bool show_sprite_bounds = false;

	GLuint vbo = 0, vao = 0;
};
