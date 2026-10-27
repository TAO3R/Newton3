#pragma once

#include <glm/glm.hpp>
#include "Physics.hpp"

#include <string>
#include <list>
#include <vector>

struct Connection;
struct Object;

//Game state, separate from rendering.

//Currently set up for a "client sends controls" / "server sends whole state" situation.

enum class Message : uint8_t {
	C2S_Controls = 1, //Greg!
	S2C_State = 's',
	//...
};

//which sprite to draw (index into the client's loaded sprites):
enum class SpriteId : uint8_t {
	Default = 0,
	Hands   = 1,
	Cart    = 2,
	PC      = 3,
	Probe   = 4,
	Turbine = 5,
	Chamber = 6,
	Count   = 7
};

//used to represent a control input:
struct Button {
	uint8_t downs = 0; //times the button has been pressed
	bool pressed = false; //is the button pressed now
};

//state of one player in the game:
struct Player {
	//player inputs (sent from client):
	struct Controls {
		Button left, right, up, down;
		Button jet;  //push toward 'aim'
		Button grab; //held = holding on, released = let go
		Button toss; //throw what's held toward 'aim'

		glm::vec2 aim = glm::vec2(0.0f); //mouse position, world space

		void send_controls_message(Connection *connection) const;

		//returns 'false' if no message or not a controls message,
		//returns 'true' if read a controls message,
		//throws on malformed controls message
		bool recv_controls_message(Connection *connection);
	} controls;

	//player state (sent from server):
	glm::vec2 position = glm::vec2(0.0f, 0.0f);

	glm::vec3 color = glm::vec3(1.0f, 1.0f, 1.0f);
	std::string name = "";

	//sprite used to draw this player:
	SpriteId sprite = SpriteId::Hands;

	uint8_t jets_left = 0; //set on spawn
	bool can_grab = false; //something is in grab range right now (drives the grab prompt)

	//server only: objects in grab range, kept by Game::on_grab_range_enter / on_grab_range_exit
	std::vector< Object * > grab_candidates;

	// physics
	PhysicsEngine::PhysicsProp pp;
};

//state of one non-player object in the game (sent from server):
struct Object {
	//sprite used to draw this object:
	SpriteId sprite = SpriteId::Default;

	//transform:
	glm::vec2 position = glm::vec2(0.0f, 0.0f);
	float rotation = 0.0f; //radians, around the z-axis (out of the screen)
	glm::vec2 scale = glm::vec2(1.0f, 1.0f);
	float width = 0.12f;	// world-space width

	// physics
	PhysicsEngine::PhysicsProp pp;
};

struct Game {
	std::list< Player > players; //(using list so they can have stable addresses)
	Player *spawn_player(); //connection placeholder for the incoming player system
	void remove_player(Player *); //remove player from game (may also, e.g., play some despawn anim)

	std::list< Object > objects; //non-player objects

	Game();

	//state update function:
	void update(float elapsed);

	//constants:
	//the update rate on the server:
	inline static constexpr float Tick = 1.0f / 30.0f;

	//arena size:
	inline static constexpr glm::vec2 ArenaMin = glm::vec2(-1.6f, -1.0f);
	inline static constexpr glm::vec2 ArenaMax = glm::vec2( 1.6f,  1.0f);

	//escape chamber and boarding area:
	inline static constexpr glm::vec2 EscapePosition = glm::vec2(-1.35f, 0.0f);
	inline static constexpr float EscapeHeight = 0.30f;
	inline static constexpr glm::vec2 EscapeAreaMin = glm::vec2(-1.27f, -0.18f);
	inline static constexpr glm::vec2 EscapeAreaMax = glm::vec2(-0.99f, 0.18f);

	//player constants:
	inline static constexpr float PlayerRadius = 0.09f;
	inline static constexpr float JetImpulse = 0.6f; //per jet; an empty-handed player gains 0.6 units/s
	inline static constexpr uint8_t JetCount = 3;    //jets per player, never refilled
	inline static constexpr float GrabRange = 0.05f; //gap between colliders that still counts as in reach
	inline static constexpr float ThrowImpulse = 1.0f; //held object gets +J toward the aim, the thrower -J

	//grab range bookkeeping, called by update when an object comes into / leaves a player's reach:
	void on_grab_range_enter(Player &player, Object &object);
	void on_grab_range_exit(Player &player, Object &object);


	//---- communication helpers ----

	//used by client:
	//set game state from data in connection buffer
	// (return true if data was read)
	bool recv_state_message(Connection *connection);

	//used by server:
	//send game state.
	//  Will move "connection_player" to the front of the front of the sent list.
	void send_state_message(Connection *connection, Player *connection_player = nullptr) const;
};
