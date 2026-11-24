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
	PlayMode(Game::Level level);
	PlayMode(std::string const &host, std::string const &port);
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

	//mouse in clip space, turned into controls.aim (world space) with the last drawn view:
	glm::vec2 mouse_clip = glm::vec2(0.0f);
	glm::mat4 world_from_clip = glm::mat4(1.0f);

	//latest game state (from server):
	Game game;

	//last message from server:
	std::string server_message;

	//Cached Outfit text textures, shared by scene labels and future UI.
	TextRenderer text_renderer;
	std::map< std::string, TextTexture > text_cache;

	// sprites
	SpriteLoader sl;

	//client-only debug: outline each sprite's quad (toggle with tab):
	bool show_sprite_bounds = false;

	GLuint vbo = 0, vao = 0;

	std::shared_ptr< void > server_lifetime; //Owned host process job; shared with the connected mode.
	std::string room_address;
	std::unique_ptr< Client >  client;	// null = local play
	float tick_acc = 0.0f;	// local fixed-step accumulator
};

struct MenuMode : PlayMode
{
	MenuMode() : PlayMode(Game::Level::Menu) {}
	enum class Screen { Play, Solo, Multiplayer, Host, Join, Starting };
	Screen screen = Screen::Play;
	float selection_time = 0.0f;
	int selection_pod = -1;
	int selected = 0;
	std::string panel_message;
	std::string join_ip = "127.0.0.1";
	bool replace_ip = true;
	float network_time = 0.0f;
	std::shared_ptr< void > server_process;
	int start_attempts = 0;
	virtual ~MenuMode();
	void start_host();
	void connect_room(std::string const &ip);
	void cancel_connection();
	void update_connection(float elapsed);
	virtual bool handle_event(SDL_Event const &, glm::uvec2 const &window_size) override;
	virtual void update(float elapsed) override;
	virtual void draw(glm::uvec2 const &drawable_size) override;
};
