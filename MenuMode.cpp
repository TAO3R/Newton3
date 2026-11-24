#include <array>
#include <cmath>

#include "PlayMode.hpp"
#include "DrawLines.hpp"
#include <glm/gtc/matrix_transform.hpp>

bool MenuMode::handle_event(SDL_Event const &evt, glm::uvec2 const &window_size)
{
	if (screen == Screen::Play) {
		if (evt.type == SDL_EVENT_KEY_DOWN && evt.key.key == SDLK_R) { selection_time = 0.0f; selection_pod = -1; }
		return PlayMode::handle_event(evt, window_size);
	}
	//Panel input must not reach the player; window close still reaches the main loop.
	if (evt.type == SDL_EVENT_KEY_DOWN && !evt.key.repeat) {
		if (evt.key.key == SDLK_ESCAPE) {
			screen = screen == Screen::Host || screen == Screen::Join ? Screen::Multiplayer : Screen::Play;
			selected = 0;
			panel_message.clear();
		} else if (evt.key.key == SDLK_UP || evt.key.key == SDLK_DOWN) {
			int count = screen == Screen::Multiplayer ? 3 : screen == Screen::Solo ? 2 : 1;
			selected = (selected + (evt.key.key == SDLK_UP ? count - 1 : 1)) % count;
		} else if (evt.key.key == SDLK_RETURN) {
			if (screen == Screen::Multiplayer) {
				screen = selected == 0 ? Screen::Host : selected == 1 ? Screen::Join : Screen::Play;
			} else if (screen == Screen::Solo) {
				if (selected == 0) panel_message = "Single-player mode is not available yet.";
				else screen = Screen::Play;
			} else screen = Screen::Multiplayer;
			selected = 0;
		}
	}
	return evt.type != SDL_EVENT_QUIT;
}

void MenuMode::update(float elapsed)
{
	if (screen != Screen::Play) return;
	PlayMode::update(elapsed);
	if (game.players.empty()) return;
	Player &local = game.players.front();
	int pod = -1;
	if (glm::dot(local.pp.velocity, local.pp.velocity) <= Game::BoardingSpeed * Game::BoardingSpeed) {
		for (auto const &object : game.objects) {
			if (object.sprite == SpriteId::Chamber && glm::distance(local.position, object.position) < 0.4f) {
				pod = object.position.y > 0.0f ? 0 : 1;
				break;
			}
		}
	}
	if (pod != selection_pod || pod < 0) selection_time = 0.0f;
	selection_pod = pod;
	if (pod < 0) return;
	selection_time += elapsed;
	if (selection_time >= Game::BoardingTime) {
		screen = pod == 0 ? Screen::Solo : Screen::Multiplayer;
		selected = 0;
		panel_message.clear();
		controls = Player::Controls();
		local.controls = Player::Controls();
		selection_time = 0.0f;
		selection_pod = -1;
	}
}

void MenuMode::draw(glm::uvec2 const &drawable_size)
{
	PlayMode::draw(drawable_size);
	if (drawable_size.x == 0 || drawable_size.y == 0) return;
	glm::mat4 world_to_clip = glm::inverse(world_from_clip);
	float ui_scale = std::min(float(drawable_size.x) / 1280.0f, float(drawable_size.y) / 720.0f);
	glm::vec3 cyan(0.65f, 0.94f, 0.96f);
	//Positions are the horizontal center and top, in screen pixels.
	auto draw_label = [&](std::string const &text, glm::vec2 position, float height, glm::vec3 color, float max_width) {
		auto found = text_cache.find(text);
		if (found == text_cache.end()) found = text_cache.emplace(text, text_renderer.make_text(text)).first;
		TextTexture const &label = found->second;
		if (label.height == 0 || label.width == 0) return;
		float text_scale = std::min(height * ui_scale / float(label.height), max_width / float(label.width));
		position.x -= 0.5f * float(label.width) * text_scale;
		text_renderer.draw_text(label, position, text_scale, color, drawable_size);
	};

	draw_label("NEWTON³", glm::vec2(0.5f * float(drawable_size.x), 14.0f * ui_scale),
		40.0f, glm::vec3(1.0f), float(drawable_size.x));
	draw_label("Every action has a reaction.", glm::vec2(0.5f * float(drawable_size.x), 66.0f * ui_scale),
		12.0f, glm::vec3(0.75f), float(drawable_size.x));

	std::array< char const *, 5 > controls_help = {
		"[A] ROTATE CCW", "[D] ROTATE CW", "[S] GRAB / THROW", "[SPACE] JET", "[R] RESET"
	};
	for (size_t i = 0; i < controls_help.size(); ++i) {
		float t = (float(i) - 2.0f) / 2.0f;
		glm::vec2 position(0.5f * float(drawable_size.x) + t * 800.0f * ui_scale * 0.4f,
			float(drawable_size.y) - (45.0f + 35.0f * t * t) * ui_scale);
		draw_label(controls_help[i], position, 11.0f, cyan, float(drawable_size.x));
	}

	if (!game.players.empty()) {
		Player const &local = game.players.front();
		bool holding = local.holding || local.anchored;
		float center_x = float(drawable_size.x) - 140.0f * ui_scale;
		draw_label(holding ? "RELEASE [S] TO THROW" : "[S] HOLD TO GRAB", glm::vec2(center_x, 264.0f * ui_scale),
			10.0f, cyan, 109.0f * ui_scale);
	}

	for (auto const &object : game.objects) {
		if (object.sprite != SpriteId::Chamber) continue;
		glm::vec4 clip = world_to_clip * glm::vec4(object.position, 0.0f, 1.0f);
		glm::vec2 center((clip.x * 0.5f + 0.5f) * float(drawable_size.x),
			(0.5f - clip.y * 0.5f) * float(drawable_size.y));
		bool solo = object.position.y > 0.0f;
		draw_label(solo ? "SINGLE PLAYER" : "MULTIPLAYER", center - glm::vec2(0.0f, 85.0f * ui_scale),
			16.0f, cyan, 200.0f * ui_scale);
		draw_label(solo ? "Learn at your own pace" : "Two players. One way out.", center - glm::vec2(0.0f, 61.0f * ui_scale),
			11.0f, glm::vec3(0.65f, 0.72f, 0.75f), 200.0f * ui_scale);

		bool nearby = !game.players.empty() && glm::distance(game.players.front().position, object.position) < 0.4f;
		if (nearby && screen == Screen::Play) {
			std::string hint = "STOP TO SELECT";
			if (selection_pod == (solo ? 0 : 1)) hint = "SELECTING " + std::to_string(int(std::ceil(Game::BoardingTime - selection_time))) + "s";
			draw_label(hint, center + glm::vec2(0.0f, 60.0f * ui_scale), 12.0f, cyan, 200.0f * ui_scale);
		}
		glm::u8vec4 color = nearby ? glm::u8vec4(255, 238, 180, 255) : glm::u8vec4(150, 143, 115, 255);
		DrawLines corners(world_to_clip);
		for (float x : {-1.0f, 1.0f}) {
			for (float y : {-1.0f, 1.0f}) {
				glm::vec3 corner(object.position + glm::vec2(x * 0.22f, y * 0.21f), 0.0f);
				corners.draw(corner, corner - glm::vec3(x * 0.06f, 0.0f, 0.0f), color);
				corners.draw(corner, corner - glm::vec3(0.0f, y * 0.06f, 0.0f), color);
			}
		}
	}
	if (screen != Screen::Play) {
		glm::vec2 center = glm::vec2(drawable_size) * 0.5f;
		glm::vec2 size = glm::vec2(480.0f, 300.0f) * ui_scale;
		glm::vec2 top_left = center - size * 0.5f;
		glEnable(GL_SCISSOR_TEST);
		glScissor(int(top_left.x), int(float(drawable_size.y) - top_left.y - size.y), int(size.x), int(size.y));
		glClearColor(0.015f, 0.025f, 0.035f, 1.0f);
		glClear(GL_COLOR_BUFFER_BIT);
		glDisable(GL_SCISSOR_TEST);
		{
			DrawLines border(glm::ortho(0.0f, float(drawable_size.x), float(drawable_size.y), 0.0f));
			std::array< glm::vec2, 4 > corners = {top_left, top_left + glm::vec2(size.x, 0.0f), top_left + size, top_left + glm::vec2(0.0f, size.y)};
			for (size_t i = 0; i < corners.size(); ++i) border.draw(glm::vec3(corners[i], 0.0f), glm::vec3(corners[(i + 1) % 4], 0.0f), glm::u8vec4(166, 240, 245, 255));
		}
		char const *title = screen == Screen::Solo ? "SINGLE PLAYER" : screen == Screen::Host ? "CREATE ROOM" : screen == Screen::Join ? "JOIN ROOM" : "MULTIPLAYER";
		draw_label(title, glm::vec2(center.x, top_left.y + 28.0f * ui_scale), 24.0f, cyan, size.x - 32.0f * ui_scale);
		std::array< char const *, 3 > options = {"CREATE ROOM", "JOIN ROOM", "BACK"};
		int count = 3;
		if (screen == Screen::Solo) { options = {"START", "BACK", ""}; count = 2; }
		if (screen == Screen::Host || screen == Screen::Join) {
			options = {"BACK", "", ""}; count = 1;
			draw_label(screen == Screen::Host ? "Room hosting is not available yet." : "Room joining is not available yet.",
				glm::vec2(center.x, top_left.y + 76.0f * ui_scale), 12.0f, glm::vec3(0.75f), size.x - 32.0f * ui_scale);
		}
		for (int i = 0; i < count; ++i) {
			std::string text = std::string(i == selected ? "> " : "") + options[size_t(i)];
			draw_label(text, glm::vec2(center.x, top_left.y + (115.0f + 36.0f * float(i)) * ui_scale),
				16.0f, i == selected ? glm::vec3(1.0f, 0.93f, 0.7f) : cyan, size.x - 32.0f * ui_scale);
		}
		draw_label(panel_message, glm::vec2(center.x, top_left.y + 220.0f * ui_scale), 12.0f, glm::vec3(0.9f, 0.75f, 0.55f), size.x - 32.0f * ui_scale);
		draw_label("UP / DOWN - SELECT    ENTER - CONFIRM    ESC - BACK", glm::vec2(center.x, top_left.y + 265.0f * ui_scale),
			10.0f, glm::vec3(0.75f), size.x - 32.0f * ui_scale);
	}

}
