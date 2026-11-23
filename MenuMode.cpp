#include <array>

#include "PlayMode.hpp"
#include "DrawLines.hpp"

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
		"MOUSE AIM", "[E] HOLD TO GRAB", "[Q] THROW", "[SPACE] JET", "[R] RESET"
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
		draw_label(holding ? "[Q] THROW" : "[E] HOLD TO GRAB", glm::vec2(center_x, 264.0f * ui_scale),
			10.0f, cyan, 109.0f * ui_scale);
		if (holding) draw_label("RELEASE [E] TO LET GO", glm::vec2(center_x, 283.0f * ui_scale),
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
}
