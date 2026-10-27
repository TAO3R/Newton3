#include "Game.hpp"

#include "Connection.hpp"

#include <stdexcept>
#include <iostream>
#include <cstring>
#include <cmath>
#include <algorithm>


void Player::Controls::send_controls_message(Connection *connection_) const {
	assert(connection_);
	auto &connection = *connection_;

	uint32_t size = 9 + sizeof(aim);
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
	send_button(jet);
	send_button(grab);
	send_button(toss);
	send_button(board);
	send_button(restart);
	connection.send(aim);
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
	uint32_t expected = 9 + sizeof(aim);
	if (size != expected) throw std::runtime_error("Controls message with size " + std::to_string(size) + " != " + std::to_string(expected) + "!");
	
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
	recv_button(recv_buffer[4+4], &jet);
	recv_button(recv_buffer[4+5], &grab);
	recv_button(recv_buffer[4+6], &toss);
	recv_button(recv_buffer[4+7], &board);
	recv_button(recv_buffer[4+8], &restart);
	std::memcpy(&aim, &recv_buffer[4+9], sizeof(aim));

	//delete message from buffer:
	recv_buffer.erase(recv_buffer.begin(), recv_buffer.begin() + 4 + size);

	return true;
}


//-----------------------------------------

Game::Game() {
	//map props: free-floating, grabbable, box collider covering the sprite (all prop pngs are square):
	auto add_prop = [&](SpriteId sprite, glm::vec2 const &position, float scale, float mass, glm::vec2 const &drift) {
		objects.emplace_back();
		Object &prop = objects.back();
		prop.sprite = sprite;
		prop.position = position;
		prop.scale = glm::vec2(scale);
		prop.pp.mass = mass;
		prop.pp.velocity = drift;
		prop.pp.grabbable = true;
		prop.pp.collider = PhysicsEngine::BoxCollider{ glm::vec2(0.5f * prop.width * scale) };
	};
	//props are a bit heavier than a player (1.0); a few start with a slow drift so the room isn't frozen:
	add_prop(SpriteId::Cart,    glm::vec2( 0.85f,  0.65f), 1.3f, 1.5f, glm::vec2(-0.03f, 0.0f));
	add_prop(SpriteId::Cart,    glm::vec2( 0.85f, -0.65f), 1.3f, 1.5f, glm::vec2(0.0f));
	add_prop(SpriteId::Turbine, glm::vec2(-0.35f,  0.45f), 1.8f, 1.4f, glm::vec2(0.0f));
	add_prop(SpriteId::Turbine, glm::vec2(-0.35f, -0.45f), 1.8f, 1.4f, glm::vec2(0.02f, 0.03f));
	add_prop(SpriteId::PC,      glm::vec2(-1.15f,  0.75f), 1.5f, 1.3f, glm::vec2(0.0f));
	add_prop(SpriteId::PC,      glm::vec2(-1.15f, -0.75f), 1.5f, 1.3f, glm::vec2(0.0f));
	add_prop(SpriteId::Probe,   glm::vec2( 0.35f,  0.0f),  1.4f, 1.2f, glm::vec2(0.0f, -0.04f));

	//escape chamber: fixed in place, players can hold on to it (and push off with a throw)
	objects.emplace_back();
	Object &chamber = objects.back();
	chamber.sprite = SpriteId::Chamber;
	chamber.position = EscapePosition;
	chamber.scale = glm::vec2(EscapeHeight / chamber.width);
	chamber.pp.type = PhysicsEngine::MovementType::Static;
	chamber.pp.grabbable = true;
	chamber.pp.collider = PhysicsEngine::CircleCollider{ 0.5f * EscapeHeight };
}

Player *Game::spawn_player() {
	players.emplace_back();
	Player &player = players.back();

	player.id = next_player_id++;
	player.name = players.size() == 1 ? "P1" : "P2";
	player.color = players.size() == 1 ? glm::vec3(0.35f, 0.75f, 1.0f) : glm::vec3(1.0f, 0.65f, 0.3f);
	player.position = glm::vec2(1.3f, players.size() == 1 ? 0.75f : -0.75f);

	//collider matches the player's circle:
	player.pp.collider = PhysicsEngine::CircleCollider{ PlayerRadius };
	player.jets_left = JetCount;
	player.pp.grip = GripImpulse;

	if (players.size() == 2) start_round();
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
	if (match_state == MatchState::Playing) match_state = MatchState::Disconnected;
}

bool Game::in_boarding_area(glm::vec2 const &position) {
	return position.x >= EscapeAreaMin.x && position.x <= EscapeAreaMax.x
		&& position.y >= EscapeAreaMin.y && position.y <= EscapeAreaMax.y;
}

void Game::start_round() {
	//Keep player addresses stable: server connections point into this list.
	for (auto &player : players) PhysicsEngine::release({&player.position, &player.pp});
	objects = Game().objects;
	int slot = 0;
	for (auto &player : players) {
		uint32_t id = player.id;
		player = Player();
		player.id = id;
		player.name = slot == 0 ? "P1" : "P2";
		player.color = slot == 0 ? glm::vec3(0.35f, 0.75f, 1.0f) : glm::vec3(1.0f, 0.65f, 0.3f);
		player.position = glm::vec2(1.3f, slot++ == 0 ? 0.75f : -0.75f);
		player.pp.collider = PhysicsEngine::CircleCollider{PlayerRadius};
		player.pp.grip = GripImpulse;
		player.jets_left = JetCount;
	}
	time_left = RoundTime;
	winner = 0;
	match_state = MatchState::Playing;
}

void Game::on_grab_range_enter(Player &player, Object &object) {
	player.grab_candidates.push_back(&object);
	player.can_grab = true;
}

void Game::on_grab_range_exit(Player &player, Object &object) {
	auto &candidates = player.grab_candidates;
	candidates.erase(std::remove(candidates.begin(), candidates.end(), &object), candidates.end());
	player.can_grab = !candidates.empty();
}

void Game::update(float elapsed) {
	if (match_state != MatchState::Playing) {
		bool restart = false;
		for (auto &player : players) {
			restart = restart || player.controls.restart.downs > 0;
			player.controls = Player::Controls();
		}
		if (restart && players.size() == 2) start_round();
		return;
	}
	float round_step = std::min(elapsed, time_left);

	//player actions this tick (step applies them once, then clears the lists):
	std::vector< PhysicsEngine::Grab > grabs;
	std::vector< PhysicsEngine::Throw > throws;

	for (auto &player : players) {
		PhysicsEngine::Body body{&player.position, &player.pp};

		//jet: one push toward the aim point per press, while jets last:
		glm::vec2 to_aim = player.controls.aim - player.position;
		for (uint8_t i = 0; i < player.controls.jet.downs && player.jets_left > 0; ++i) {
			if (to_aim == glm::vec2(0.0f)) break; //no direction, don't spend the jet
			if (!PhysicsEngine::apply_impulse(body, glm::normalize(to_aim) * JetImpulse)) break; //anchored: can't move
			player.jets_left -= 1;
		}

		//throw what's held toward the aim point (the thrower recoils the other way):
		bool tossed = false;
		if (player.controls.toss.downs > 0 && player.pp.partner.pp && to_aim != glm::vec2(0.0f)) {
			throws.push_back({body, glm::normalize(to_aim) * ThrowImpulse});
			tossed = true;
		}

		//grab on press, let go once the key is up:
		if (player.controls.grab.downs > 0) {
			grabs.push_back({body, GrabRange});
		} else if (!tossed && !player.controls.grab.pressed && player.pp.partner.pp) {
			throws.push_back({body, glm::vec2(0.0f)});
		}

		//reset 'downs' since controls have been handled:
		player.controls.left.downs = 0;
		player.controls.right.downs = 0;
		player.controls.up.downs = 0;
		player.controls.down.downs = 0;
		player.controls.jet.downs = 0;
		player.controls.grab.downs = 0;
		player.controls.toss.downs = 0;
		player.controls.board.downs = 0;
		player.controls.restart.downs = 0;
	}

	{ //physics: step every player and object, keeping them inside the arena
		std::vector< PhysicsEngine::Body > bodies;
		for (auto &p : players) bodies.push_back({&p.position, &p.pp});
		for (auto &o : objects) bodies.push_back({&o.position, &o.pp});

		int steps = int(std::round(elapsed / PhysicsEngine::physics_dt));
		for (int s = 0; s < steps; ++s) {
			PhysicsEngine::step(bodies, grabs, throws);
			PhysicsEngine::confine(bodies, ArenaMin, ArenaMax);
		}
	}

	//grab range: compare what each player could grab now with last tick, firing enter / exit for the difference
	for (auto &player : players) {
		PhysicsEngine::Body holder{&player.position, &player.pp};
		std::vector< Object * > in_range;
		if (!player.pp.partner.pp) { //already holding something: can't grab another
			for (auto &object : objects) {
				PhysicsEngine::Body target{&object.position, &object.pp};
				if (PhysicsEngine::can_be_grabbed(target) && PhysicsEngine::in_reach(holder, target, GrabRange)) {
					in_range.push_back(&object);
				}
			}
		}

		//walk a copy, since exit edits grab_candidates:
		for (Object *object : std::vector< Object * >(player.grab_candidates)) {
			if (std::find(in_range.begin(), in_range.end(), object) == in_range.end()) on_grab_range_exit(player, *object);
		}
		for (Object *object : in_range) {
			auto const &candidates = player.grab_candidates;
			if (std::find(candidates.begin(), candidates.end(), object) == candidates.end()) on_grab_range_enter(player, *object);
		}
	}
	//After physics, only a slow player holding F inside the area may board.
	float first_finish = round_step + 1.0f;
	bool tied = false;
	for (auto &player : players) {
		player.holding = player.pp.partner.pp != nullptr;
		player.anchored = player.holding && player.pp.partner.pp->type == PhysicsEngine::MovementType::Static;
		bool eligible = in_boarding_area(player.position)
			&& glm::dot(player.pp.velocity, player.pp.velocity) <= BoardingSpeed * BoardingSpeed;
		if (eligible && player.controls.board.pressed) {
			float finish = BoardingTime - player.boarding;
			player.boarding = std::min(BoardingTime, player.boarding + round_step);
			if (finish <= round_step + 0.00001f) {
				if (finish < first_finish - 0.00001f) {
					first_finish = finish;
					winner = player.id;
					tied = false;
				} else if (std::abs(finish - first_finish) <= 0.00001f) tied = true;
			}
		} else player.boarding = 0.0f;
	}
	time_left = std::max(0.0f, time_left - round_step);
	if (winner != 0) {
		match_state = tied ? MatchState::Draw : MatchState::Escaped;
		if (tied) winner = 0;
	} else if (time_left <= 0.0f) match_state = MatchState::Timeout;
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
		connection.send(player.id);
		connection.send(player.boarding);
		connection.send(uint8_t(player.holding));
		connection.send(uint8_t(player.anchored));
		connection.send(player.position);
		connection.send(player.pp.velocity);
		connection.send(player.color);
		connection.send(player.sprite);
		connection.send(player.jets_left);
		connection.send(uint8_t(player.can_grab ? 1 : 0));

		//NOTE: can't just 'send(name)' because player.name is not plain-old-data type.
		//effectively: truncates player name to 255 chars
		uint8_t len = uint8_t(std::min< size_t >(255, player.name.size()));
		connection.send(len);
		connection.send_buffer.insert(connection.send_buffer.end(), player.name.begin(), player.name.begin() + len);
	};

	connection.send(match_state);
	connection.send(time_left);
	connection.send(winner);

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

	read(&match_state);
	read(&time_left);
	read(&winner);

	players.clear();
	uint8_t player_count;
	read(&player_count);
	for (uint8_t i = 0; i < player_count; ++i) {
		players.emplace_back();
		Player &player = players.back();
		read(&player.id);
		read(&player.boarding);
		uint8_t holding, anchored;
		read(&holding);
		read(&anchored);
		player.holding = holding != 0;
		player.anchored = anchored != 0;
		read(&player.position);
		read(&player.pp.velocity);
		read(&player.color);
		read(&player.sprite);
		read(&player.jets_left);
		uint8_t can_grab;
		read(&can_grab);
		player.can_grab = (can_grab != 0);
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
