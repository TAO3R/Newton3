#include "Game.hpp"

#include "Connection.hpp"

#include <stdexcept>
#include <iostream>
#include <cstring>
#include <cmath>


void Player::Controls::send_controls_message(Connection *connection_) const {
	assert(connection_);
	auto &connection = *connection_;

	uint32_t size = 5;
	connection.send(Message::C2S_Controls);
	connection.send(uint8_t(size));
	connection.send(uint8_t(size >> 8));
	connection.send(uint8_t(size >> 16));

	auto send_button = [&](Button const &b) {
		if (b.downs & 0x80) {
			std::cerr << "Wow, you are really good at pressing buttons!" << std::endl;
		}
		connection.send(uint8_t( (b.pressed ? 0x80 : 0x00) | (b.downs & 0x7f) ) );
	};

	send_button(left);
	send_button(right);
	send_button(up);
	send_button(down);
	send_button(jump);
}

bool Player::Controls::recv_controls_message(Connection *connection_) {
	assert(connection_);
	auto &connection = *connection_;

	auto &recv_buffer = connection.recv_buffer;

	//expecting [type, size_low0, size_mid8, size_high8]:
	if (recv_buffer.size() < 4) return false;
	if (recv_buffer[0] != uint8_t(Message::C2S_Controls)) return false;
	uint32_t size = (uint32_t(recv_buffer[3]) << 16)
	              | (uint32_t(recv_buffer[2]) << 8)
	              |  uint32_t(recv_buffer[1]);
	if (size != 5) throw std::runtime_error("Controls message with size " + std::to_string(size) + " != 5!");
	
	//expecting complete message:
	if (recv_buffer.size() < 4 + size) return false;

	auto recv_button = [](uint8_t byte, Button *button) {
		button->pressed = (byte & 0x80);
		uint32_t d = uint32_t(button->downs) + uint32_t(byte & 0x7f);
		if (d > 255) {
			std::cerr << "got a whole lot of downs" << std::endl;
			d = 255;
		}
		button->downs = uint8_t(d);
	};

	recv_button(recv_buffer[4+0], &left);
	recv_button(recv_buffer[4+1], &right);
	recv_button(recv_buffer[4+2], &up);
	recv_button(recv_buffer[4+3], &down);
	recv_button(recv_buffer[4+4], &jump);

	//delete message from buffer:
	recv_buffer.erase(recv_buffer.begin(), recv_buffer.begin() + 4 + size);

	return true;
}


//-----------------------------------------

Game::Game() {
	//test object in the middle of the arena:
	objects.emplace_back();
	Object &hands = objects.back();
	hands.sprite = SpriteId::Hands;
	//collider is the circle inside the sprite (hands.png is square, so height == width):
	hands.pp.collider = PhysicsEngine::CircleCollider{ 0.5f * hands.width };

	//map props: free-floating, grabbable, box collider covering the sprite (all prop pngs are square):
	auto add_prop = [&](SpriteId sprite, glm::vec2 const &position, float scale) {
		objects.emplace_back();
		Object &prop = objects.back();
		prop.sprite = sprite;
		prop.position = position;
		prop.scale = glm::vec2(scale);
		prop.pp.grabbable = true;
		prop.pp.collider = PhysicsEngine::BoxCollider{ glm::vec2(0.5f * prop.width * scale) };
	};
	for (float side : {-1.0f, 1.0f}) {
		add_prop(SpriteId::Cart, glm::vec2(0.85f, side * 0.65f), 1.3f);
		add_prop(SpriteId::Turbine, glm::vec2(-0.35f, side * 0.45f), 1.8f);
		add_prop(SpriteId::PC, glm::vec2(-1.15f, side * 0.75f), 1.5f);
	}
	add_prop(SpriteId::Probe, glm::vec2(0.35f, 0.0f), 1.4f);

	//escape chamber: fixed in place
	objects.emplace_back();
	Object &chamber = objects.back();
	chamber.sprite = SpriteId::Chamber;
	chamber.position = EscapePosition;
	chamber.scale = glm::vec2(EscapeHeight / (2.0f * PlayerRadius));
	chamber.pp.type = PhysicsEngine::MovementType::Static;
	chamber.pp.collider = PhysicsEngine::CircleCollider{ 0.5f * EscapeHeight };
}

Player *Game::spawn_player() {
	players.emplace_back();
	Player &player = players.back();

	//Connection placeholder until the new player system is merged.

	//collider matches the player's circle:
	player.pp.collider = PhysicsEngine::CircleCollider{ PlayerRadius };

	return &player;
}

void Game::remove_player(Player *player) {
	bool found = false;
	for (auto pi = players.begin(); pi != players.end(); ++pi) {
		if (&*pi == player) {
			//drop anything held first, so nothing keeps a pointer to this player:
			PhysicsEngine::release({&player->position, &player->pp});
			players.erase(pi);
			found = true;
			break;
		}
	}
	assert(found);
}

void Game::update(float elapsed) {
	//Keep input counters bounded while waiting for the new simulation.
	for (auto &player : players) {
		player.controls.left.downs = 0;
		player.controls.right.downs = 0;
		player.controls.up.downs = 0;
		player.controls.down.downs = 0;
		player.controls.jump.downs = 0;
	}

	{ //physics: step every player and object, keeping them inside the arena
		std::vector< PhysicsEngine::Body > bodies;
		for (auto &p : players) bodies.push_back({&p.position, &p.pp});
		for (auto &o : objects) bodies.push_back({&o.position, &o.pp});

		//player actions this tick (step applies them once, then clears the lists):
		std::vector< PhysicsEngine::Grab > grabs;
		std::vector< PhysicsEngine::Throw > throws;
		//(grab / release / throw input goes here: push a Grab on grab press, a Throw on release or throw)

		int steps = int(std::round(elapsed / PhysicsEngine::physics_dt));
		for (int s = 0; s < steps; ++s) {
			PhysicsEngine::step(bodies, grabs, throws);
			PhysicsEngine::confine(bodies, ArenaMin, ArenaMax);
		}
	}
}


void Game::send_state_message(Connection *connection_, Player *connection_player) const {
	assert(connection_);
	auto &connection = *connection_;

	connection.send(Message::S2C_State);
	//will patch message size in later, for now placeholder bytes:
	connection.send(uint8_t(0));
	connection.send(uint8_t(0));
	connection.send(uint8_t(0));
	size_t mark = connection.send_buffer.size(); //keep track of this position in the buffer


	//send player info helper:
	auto send_player = [&](Player const &player) {
		connection.send(player.position);
		connection.send(player.pp.velocity);
		connection.send(player.color);
		connection.send(player.sprite);

		//NOTE: can't just 'send(name)' because player.name is not plain-old-data type.
		//effectively: truncates player name to 255 chars
		uint8_t len = uint8_t(std::min< size_t >(255, player.name.size()));
		connection.send(len);
		connection.send_buffer.insert(connection.send_buffer.end(), player.name.begin(), player.name.begin() + len);
	};

	//player count:
	connection.send(uint8_t(players.size()));
	if (connection_player) send_player(*connection_player);	// localhost
	for (auto const &player : players) {
		if (&player == connection_player) continue;
		send_player(player);
	}

	//send object info helper:
	auto send_object = [&](Object const &object) {
		connection.send(object.position);
		connection.send(object.rotation);
		connection.send(object.scale);
		connection.send(object.sprite);
		connection.send(object.width);
	};

	//object count:
	connection.send(uint8_t(objects.size()));
	for (auto const &object : objects) {
		send_object(object);
	}

	//compute the message size and patch into the message header:
	uint32_t size = uint32_t(connection.send_buffer.size() - mark);
	connection.send_buffer[mark-3] = uint8_t(size);
	connection.send_buffer[mark-2] = uint8_t(size >> 8);
	connection.send_buffer[mark-1] = uint8_t(size >> 16);
}

bool Game::recv_state_message(Connection *connection_) {
	assert(connection_);
	auto &connection = *connection_;
	auto &recv_buffer = connection.recv_buffer;

	if (recv_buffer.size() < 4) return false;
	if (recv_buffer[0] != uint8_t(Message::S2C_State)) return false;
	uint32_t size = (uint32_t(recv_buffer[3]) << 16)
	              | (uint32_t(recv_buffer[2]) << 8)
	              |  uint32_t(recv_buffer[1]);
	uint32_t at = 0;
	//expecting complete message:
	if (recv_buffer.size() < 4 + size) return false;

	//copy bytes from buffer and advance position:
	auto read = [&](auto *val) {
		if (at + sizeof(*val) > size) {
			throw std::runtime_error("Ran out of bytes reading state message.");
		}
		std::memcpy(val, &recv_buffer[4 + at], sizeof(*val));
		at += sizeof(*val);
	};

	players.clear();
	uint8_t player_count;
	read(&player_count);
	for (uint8_t i = 0; i < player_count; ++i) {
		players.emplace_back();
		Player &player = players.back();
		read(&player.position);
		read(&player.pp.velocity);
		read(&player.color);
		read(&player.sprite);
		uint8_t name_len;
		read(&name_len);
		//n.b. would probably be more efficient to directly copy from recv_buffer, but I think this is clearer:
		player.name = "";
		for (uint8_t n = 0; n < name_len; ++n) {
			char c;
			read(&c);
			player.name += c;
		}
	}

	objects.clear();
	uint8_t object_count;
	read(&object_count);
	for (uint8_t i = 0; i < object_count; ++i) {
		objects.emplace_back();
		Object &object = objects.back();
		read(&object.position);
		read(&object.rotation);
		read(&object.scale);
		read(&object.sprite);
		read(&object.width);
	}

	if (at != size) throw std::runtime_error("Trailing data in state message.");

	//delete message from buffer:
	recv_buffer.erase(recv_buffer.begin(), recv_buffer.begin() + 4 + size);

	return true;
}
