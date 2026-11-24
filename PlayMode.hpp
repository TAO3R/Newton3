#include "Mode.hpp"

#include "Connection.hpp"
#include "Game.hpp"
#include "SpriteLoader.hpp"
#include "TextRenderer.hpp"
#include "Sounds.hpp"

#include <map>

#include <glm/glm.hpp>

#include <vector>
#include <deque>

// UI layout in pixels at 1280 x 720; scaled with the window. Rebuild after editing.
// Compass center: increase Right to move left; increase Top to move down.
const float UICompassRight = 230.0f;
const float UICompassTop = 120.0f;
const float UICompassWidth = 150.0f;
const float UICompassTargetRadius = 50.0f;
const float UICompassDotRadius = 3.0f;

// Jet image center from left/top; text positions are left/top edges.
const float UIJetLeft = 160.0f;
const float UIJetTop = 120.0f;
const float UIJetWidth = 150.0f;
const float UIJetTextLeft = 220.0f;
const float UIJetTextTop = 86.0f;
const float UIJetTextGap = 25.0f;
const float UIJetTextHeight = 12.0f;

// Hands image center from right/top. Text tops are measured from the screen top.
const float UIHandsRight = 140.0f;
const float UIHandsTop = 245.0f;
const float UIHandsWidth = 125.0f;
const float UIHandsTitleTop = 196.0f;
const float UIHandsStateTop = 246.0f;
const float UIHandsTextHeight = 12.0f;
const float UIHandsHintTop = 264.0f;
const float UIHandsHintGap = 19.0f;
const float UIHandsHintHeight = 10.0f;
const float UIHandsPadding = 16.0f;

// Header text and boarding progress bar.
const float UIHeaderTop = 20.0f;
const float UITimerHeight = 32.0f;
const float UIMultiplayerTitleHeight = 22.0f;
const float UIStatusTop = 66.0f;
const float UIStatusHeight = 12.0f;
const float UIBoardingTop = 90.0f;
const float UIBoardingWidth = 180.0f;
const float UIBoardingHeight = 8.0f;

// Bottom controls: increase Bottom/Rise to move up; Width spreads labels apart.
const float UIControlsWidth = 800.0f;
const float UIControlsBottom = 45.0f;
const float UIControlsRise = 35.0f;
const float UIControlsHeight = 11.0f;

// Menu heading and pod labels; pod offsets are relative to each pod on screen.
const float UIMenuTitleTop = 14.0f;
const float UIMenuTitleHeight = 40.0f;
const float UIMenuSubtitleTop = 66.0f;
const float UIPodTitleAbove = 85.0f;
const float UIPodSubtitleAbove = 61.0f;
const float UIPodHintBelow = 60.0f;
const float UIPodTextWidth = 200.0f;

// Centered popup size, shared by menu panels and return confirmation.
const float UIPanelWidth = 480.0f;
const float UIPanelHeight = 300.0f;
// Menu panel offsets are measured from its top edge.
const float UIPanelTitleTop = 28.0f;
const float UIPanelTitleHeight = 24.0f;
const float UIPanelPadding = 32.0f;
const float UIPanelOptionsTop = 115.0f;
const float UIHostOptionsTop = 127.0f;
const float UIPanelOptionGap = 36.0f;
const float UIPanelOptionHeight = 16.0f;
const float UIPanelMessageTop = 220.0f;
const float UIPanelHelpTop = 265.0f;

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

	//inverse of the last drawn view (MenuMode recovers world_to_clip from it for its labels):
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
	bool confirm_return = false;
	bool leave_selected = false;

	GLuint vbo = 0, vao = 0;

	std::shared_ptr< void > server_lifetime; //Owned host process job; shared with the connected mode.
	std::string room_address;
	std::unique_ptr< Client >  client;	// null = local play
	float tick_acc = 0.0f;	// local fixed-step accumulator

	// sounds
	std::shared_ptr< Sound::PlayingSample > bgm;

	int last_hits = 0; // previous number of hits for player body
	Game::MatchState last_match_state = Game::MatchState::Waiting;
};

struct MenuMode : PlayMode
{
	MenuMode() : PlayMode(Game::Level::Menu) {}
	enum class Screen { Play, Solo, Multiplayer, Host, Join, Starting, HostReady };
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
	std::shared_ptr< PlayMode > hosted_game;
	virtual ~MenuMode();
	void start_host();
	void connect_room(std::string const &ip);
	void cancel_connection();
	void update_connection(float elapsed);
	virtual bool handle_event(SDL_Event const &, glm::uvec2 const &window_size) override;
	virtual void update(float elapsed) override;
	virtual void draw(glm::uvec2 const &drawable_size) override;
};
