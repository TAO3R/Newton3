#include "PlayMode.hpp"

#include "DrawLines.hpp"
#include "ColorTextureProgram.hpp"
#include "gl_errors.hpp"
#include "data_path.hpp"
#include "hex_dump.hpp"

#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/matrix_transform.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/string_cast.hpp>

#include <random>
#include <array>

PlayMode::PlayMode(Client &client_) : client(client_) {

	{ //upload sprite textures (order must match SpriteId):
		Sprite s = sl.load_sprite("default.png");
		sl.add_sprite(s);
		s = sl.load_sprite("hands.png");
		sl.add_sprite(s);
		sl.upload_sprites();
	}

	glGenBuffers(1, &vbo);

	glGenVertexArrays(1, &vao);
	glBindVertexArray(vao);
	glBindBuffer(GL_ARRAY_BUFFER, vbo);

	//unit quad centered on the origin (two triangles); each sprite is placed via CLIP_FROM_OBJECT:
	SpriteVertex const quad[6] = {
		{ glm::vec2(-0.5f,-0.5f), glm::vec2(0.0f, 0.0f), glm::u8vec4(0xff) },
		{ glm::vec2( 0.5f,-0.5f), glm::vec2(1.0f, 0.0f), glm::u8vec4(0xff) },
		{ glm::vec2( 0.5f, 0.5f), glm::vec2(1.0f, 1.0f), glm::u8vec4(0xff) },
		{ glm::vec2(-0.5f,-0.5f), glm::vec2(0.0f, 0.0f), glm::u8vec4(0xff) },
		{ glm::vec2( 0.5f, 0.5f), glm::vec2(1.0f, 1.0f), glm::u8vec4(0xff) },
		{ glm::vec2(-0.5f, 0.5f), glm::vec2(0.0f, 1.0f), glm::u8vec4(0xff) },
	};
	glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);

	glVertexAttribPointer(
		color_texture_program->Position_vec4,
		2,
		GL_FLOAT,
		GL_FALSE,
		sizeof(SpriteVertex),
		(GLbyte *)0 + offsetof(SpriteVertex, position)
	);
	glEnableVertexAttribArray(color_texture_program->Position_vec4);

	glVertexAttribPointer(
		color_texture_program->TexCoord_vec2,
		2,
		GL_FLOAT,
		GL_FALSE,
		sizeof(SpriteVertex),
		(GLbyte *)0 + offsetof(SpriteVertex, texCoord)
	);
	glEnableVertexAttribArray(color_texture_program->TexCoord_vec2);

	glVertexAttribPointer(
		color_texture_program->Color_vec4,
		4,
		GL_UNSIGNED_BYTE,
		GL_TRUE,
		sizeof(SpriteVertex),
		(GLbyte *)0 + offsetof(SpriteVertex, color)
	);
	glEnableVertexAttribArray(color_texture_program->Color_vec4);

	glBindBuffer(GL_ARRAY_BUFFER, 0);

	glBindVertexArray(0);

	GL_ERRORS();
}

PlayMode::~PlayMode() {
}

bool PlayMode::handle_event(SDL_Event const &evt, glm::uvec2 const &window_size) {

	if (evt.type == SDL_EVENT_KEY_DOWN) {
		if (evt.key.repeat) {
			//ignore repeats
		} else if (evt.key.key == SDLK_A) {
			controls.left.downs += 1;
			controls.left.pressed = true;
			return true;
		} else if (evt.key.key == SDLK_D) {
			controls.right.downs += 1;
			controls.right.pressed = true;
			return true;
		} else if (evt.key.key == SDLK_W) {
			controls.up.downs += 1;
			controls.up.pressed = true;
			return true;
		} else if (evt.key.key == SDLK_S) {
			controls.down.downs += 1;
			controls.down.pressed = true;
			return true;
		} else if (evt.key.key == SDLK_SPACE) {
			controls.jump.downs += 1;
			controls.jump.pressed = true;
			return true;
		}
	} else if (evt.type == SDL_EVENT_KEY_UP) {
		if (evt.key.key == SDLK_A) {
			controls.left.pressed = false;
			return true;
		} else if (evt.key.key == SDLK_D) {
			controls.right.pressed = false;
			return true;
		} else if (evt.key.key == SDLK_W) {
			controls.up.pressed = false;
			return true;
		} else if (evt.key.key == SDLK_S) {
			controls.down.pressed = false;
			return true;
		} else if (evt.key.key == SDLK_SPACE) {
			controls.jump.pressed = false;
			return true;
		}
	}

	return false;
}

void PlayMode::update(float elapsed) {

	//queue data for sending to server:
	controls.send_controls_message(&client.connection);

	//reset button press counters:
	controls.left.downs = 0;
	controls.right.downs = 0;
	controls.up.downs = 0;
	controls.down.downs = 0;
	controls.jump.downs = 0;

	//send/receive data:
	client.poll([this](Connection *c, Connection::Event event){
		if (event == Connection::OnOpen) {
			std::cout << "[" << c->socket << "] opened" << std::endl;
		} else if (event == Connection::OnClose) {
			std::cout << "[" << c->socket << "] closed (!)" << std::endl;
			throw std::runtime_error("Lost connection to server!");
		} else { assert(event == Connection::OnRecv);
			//std::cout << "[" << c->socket << "] recv'd data. Current buffer:\n" << hex_dump(c->recv_buffer); std::cout.flush(); //DEBUG
			bool handled_message;
			try {
				do {
					handled_message = false;
					if (game.recv_state_message(c)) handled_message = true;
				} while (handled_message);
			} catch (std::exception const &e) {
				std::cerr << "[" << c->socket << "] malformed message from server: " << e.what() << std::endl;
				//quit the game:
				throw e;
			}
		}
	}, 0.0);
}

void PlayMode::draw(glm::uvec2 const &drawable_size) {

	static std::array< glm::vec2, 16 > const circle = [](){
		std::array< glm::vec2, 16 > ret;
		for (uint32_t a = 0; a < ret.size(); ++a) {
			float ang = a / float(ret.size()) * 2.0f * float(M_PI);
			ret[a] = glm::vec2(std::cos(ang), std::sin(ang));
		}
		return ret;
	}();

	glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT);
	glDisable(GL_DEPTH_TEST);
	
	//figure out view transform to center the arena:
	float aspect = float(drawable_size.x) / float(drawable_size.y);
	float scale = std::min(
		2.0f * aspect / (Game::ArenaMax.x - Game::ArenaMin.x + 2.0f * Game::PlayerRadius),
		2.0f / (Game::ArenaMax.y - Game::ArenaMin.y + 2.0f * Game::PlayerRadius)
	);
	glm::vec2 offset = -0.5f * (Game::ArenaMax + Game::ArenaMin);

	glm::mat4 world_to_clip = glm::mat4(
		scale / aspect, 0.0f, 0.0f, offset.x,
		0.0f, scale, 0.0f, offset.y,
		0.0f, 0.0f, 1.0f, 0.0f,
		0.0f, 0.0f, 0.0f, 1.0f
	);

	{ //draw sprites (before lines, so lines and names end up on top):
		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

		glUseProgram(color_texture_program->program);
		glBindVertexArray(vao);
		glActiveTexture(GL_TEXTURE0);

		for (auto const &object : game.objects) {
			if (size_t(object.sprite) >= sl.sprites.size()) continue; //bad id from server
			Sprite const &sprite = sl.sprites[size_t(object.sprite)];

			//world-space size: one player diameter tall, width follows the image's aspect ratio:
			float height = 2.0f * Game::PlayerRadius;
			float width = height * float(sprite.size.x) / float(sprite.size.y);

			glm::mat4 clip_from_object = world_to_clip
				* glm::translate(glm::mat4(1.0f), glm::vec3(object.position, 0.0f))
				* glm::rotate(glm::mat4(1.0f), object.rotation, glm::vec3(0.0f, 0.0f, 1.0f))
				* glm::scale(glm::mat4(1.0f), glm::vec3(width * object.scale.x, height * object.scale.y, 1.0f));
			glUniformMatrix4fv(color_texture_program->CLIP_FROM_OBJECT_mat4, 1, GL_FALSE, glm::value_ptr(clip_from_object));

			glBindTexture(GL_TEXTURE_2D, sprite.tex);
			glDrawArrays(GL_TRIANGLES, 0, 6);
		}

		glBindTexture(GL_TEXTURE_2D, 0);
		glBindVertexArray(0);
		glUseProgram(0);
		glDisable(GL_BLEND);
	}

	{
		DrawLines lines(world_to_clip);

		//helper:
		auto draw_text = [&](glm::vec2 const &at, std::string const &text, float H) {
			lines.draw_text(text,
				glm::vec3(at.x, at.y, 0.0),
				glm::vec3(H, 0.0f, 0.0f), glm::vec3(0.0f, H, 0.0f),
				glm::u8vec4(0x00, 0x00, 0x00, 0x00));
			float ofs = (1.0f / scale) / drawable_size.y;
			lines.draw_text(text,
				glm::vec3(at.x + ofs, at.y + ofs, 0.0),
				glm::vec3(H, 0.0f, 0.0f), glm::vec3(0.0f, H, 0.0f),
				glm::u8vec4(0xff, 0xff, 0xff, 0x00));
		};

		lines.draw(glm::vec3(Game::ArenaMin.x, Game::ArenaMin.y, 0.0f), glm::vec3(Game::ArenaMax.x, Game::ArenaMin.y, 0.0f), glm::u8vec4(0xff, 0x00, 0xff, 0xff));
		lines.draw(glm::vec3(Game::ArenaMin.x, Game::ArenaMax.y, 0.0f), glm::vec3(Game::ArenaMax.x, Game::ArenaMax.y, 0.0f), glm::u8vec4(0xff, 0x00, 0xff, 0xff));
		lines.draw(glm::vec3(Game::ArenaMin.x, Game::ArenaMin.y, 0.0f), glm::vec3(Game::ArenaMin.x, Game::ArenaMax.y, 0.0f), glm::u8vec4(0xff, 0x00, 0xff, 0xff));
		lines.draw(glm::vec3(Game::ArenaMax.x, Game::ArenaMin.y, 0.0f), glm::vec3(Game::ArenaMax.x, Game::ArenaMax.y, 0.0f), glm::u8vec4(0xff, 0x00, 0xff, 0xff));

		for (auto const &player : game.players) {
			glm::u8vec4 col = glm::u8vec4(player.color.x*255, player.color.y*255, player.color.z*255, 0xff);
			if (&player == &game.players.front()) {
				//mark current player (which server sends first):
				lines.draw(
					glm::vec3(player.position + Game::PlayerRadius * glm::vec2(-0.5f,-0.5f), 0.0f),
					glm::vec3(player.position + Game::PlayerRadius * glm::vec2( 0.5f, 0.5f), 0.0f),
					col
				);
				lines.draw(
					glm::vec3(player.position + Game::PlayerRadius * glm::vec2(-0.5f, 0.5f), 0.0f),
					glm::vec3(player.position + Game::PlayerRadius * glm::vec2( 0.5f,-0.5f), 0.0f),
					col
				);
			}
			for (uint32_t a = 0; a < circle.size(); ++a) {
				lines.draw(
					glm::vec3(player.position + Game::PlayerRadius * circle[a], 0.0f),
					glm::vec3(player.position + Game::PlayerRadius * circle[(a+1)%circle.size()], 0.0f),
					col
				);
			}

			draw_text(player.position + glm::vec2(0.0f, -0.1f + Game::PlayerRadius), player.name, 0.09f);
		}
	}
	GL_ERRORS();
}
