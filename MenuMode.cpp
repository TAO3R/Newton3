#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <iphlpapi.h>
#pragma comment(lib, "Iphlpapi.lib")
#undef APIENTRY
#endif

#include <array>
#include <cmath>

#include "PlayMode.hpp"
#include "DrawLines.hpp"
#include <glm/gtc/matrix_transform.hpp>

#include "data_path.hpp"
#include <stdexcept>

#ifdef _WIN32
namespace {
bool network_ready() {
	static int result = []() { WSADATA data; return WSAStartup(MAKEWORD(2, 2), &data); }();
	return result == 0;
}
std::shared_ptr< void > own_handle(HANDLE handle) {
	return std::shared_ptr< void >(handle, [](void *value) { if (value) CloseHandle(value); });
}
std::string local_addresses() {
	ULONG size = 16000;
	std::vector< unsigned char > buffer(size);
	auto adapters = reinterpret_cast< IP_ADAPTER_ADDRESSES * >(buffer.data());
	ULONG result = GetAdaptersAddresses(AF_INET, GAA_FLAG_INCLUDE_GATEWAYS, nullptr, adapters, &size);
	if (result == ERROR_BUFFER_OVERFLOW) {
		buffer.resize(size);
		adapters = reinterpret_cast< IP_ADAPTER_ADDRESSES * >(buffer.data());
		result = GetAdaptersAddresses(AF_INET, GAA_FLAG_INCLUDE_GATEWAYS, nullptr, adapters, &size);
	}
	std::string ips;
	if (result == NO_ERROR) for (auto adapter = adapters; adapter; adapter = adapter->Next) {
		if (adapter->OperStatus != IfOperStatusUp || adapter->IfType == IF_TYPE_SOFTWARE_LOOPBACK || !adapter->FirstGatewayAddress) continue;
		std::wstring name = adapter->FriendlyName ? adapter->FriendlyName : L"";
		if (name.find(L"VMware") != std::wstring::npos || name.find(L"Virtual") != std::wstring::npos || name.find(L"vEthernet") != std::wstring::npos) continue;
		for (auto address = adapter->FirstUnicastAddress; address; address = address->Next) {
			if (address->Address.lpSockaddr->sa_family != AF_INET) continue;
			auto ipv4 = reinterpret_cast< sockaddr_in * >(address->Address.lpSockaddr);
			char ip[INET_ADDRSTRLEN];
			if (!inet_ntop(AF_INET, &ipv4->sin_addr, ip, sizeof(ip))) continue;
			std::string value(ip);
			if (value.starts_with("127.") || value.starts_with("169.254.")) continue;
			if (!ips.empty()) ips += ", ";
			ips += value;
		}
	}
	return ips.empty() ? "127.0.0.1 (local only)" : ips;
}
}
#endif

MenuMode::~MenuMode() {
	cancel_connection();
	if (Mode::window) SDL_StopTextInput(Mode::window);
}

void MenuMode::cancel_connection() {
	server_lifetime.reset();
	server_process.reset();
}

void MenuMode::start_host() {
	cancel_connection();
	panel_message.clear();
#ifdef _WIN32
	if (!network_ready()) { panel_message = "Network initialization failed."; return; }
	//Do not attach to a room owned by another client or an old server.
	SOCKET probe = socket(AF_INET6, SOCK_STREAM, IPPROTO_TCP);
	BOOL exclusive = TRUE;
	setsockopt(probe, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, reinterpret_cast< char * >(&exclusive), sizeof(exclusive));
	DWORD dual_stack = 0;
	setsockopt(probe, IPPROTO_IPV6, IPV6_V6ONLY, reinterpret_cast< char * >(&dual_stack), sizeof(dual_stack));
	sockaddr_in6 address = {};
	address.sin6_family = AF_INET6;
	address.sin6_port = htons(11451);
	bool available = probe != INVALID_SOCKET && bind(probe, reinterpret_cast< sockaddr * >(&address), sizeof(address)) == 0;
	if (probe != INVALID_SOCKET) closesocket(probe);
	if (!available) { panel_message = "Port 11451 is in use. Join the existing room instead."; return; }

	HANDLE job = CreateJobObjectW(nullptr, nullptr);
	server_lifetime = own_handle(job);
	JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits = {};
	limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
	if (!job || !SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof(limits))) {
		cancel_connection(); panel_message = "Could not prepare the room."; return;
	}
	STARTUPINFOA startup = {};
	startup.cb = sizeof(startup);
	PROCESS_INFORMATION process = {};
	std::string executable = data_path("server.exe");
	std::string command = "\"" + executable + "\" 11451";
	BOOL created = CreateProcessA(executable.c_str(), command.data(), nullptr, nullptr, FALSE,
		CREATE_NO_WINDOW | CREATE_SUSPENDED, nullptr, data_path("").c_str(), &startup, &process);
	if (!created) { cancel_connection(); panel_message = "Could not start server.exe. Keep it beside client.exe."; return; }
	if (!AssignProcessToJobObject(job, process.hProcess)) {
		TerminateProcess(process.hProcess, 1);
		CloseHandle(process.hThread); CloseHandle(process.hProcess);
		cancel_connection(); panel_message = "Could not start the room process."; return;
	}
	server_process = own_handle(process.hProcess);
	ResumeThread(process.hThread);
	CloseHandle(process.hThread);
	room_address = "HOST IP: " + local_addresses();
	screen = Screen::Starting;
	network_time = 0.0f;
	start_attempts = 0;
#else
	panel_message = "Hosting is currently supported on Windows.";
#endif
}

void MenuMode::connect_room(std::string const &ip) {
	panel_message.clear();
#ifdef _WIN32
	in_addr address = {};
	if (inet_pton(AF_INET, ip.c_str(), &address) != 1) {
		panel_message = "Enter an IPv4 address, for example 192.168.1.23."; return;
	}
#endif
	try {
		auto next = std::make_shared< PlayMode >(ip, "11451");
		next->server_lifetime = std::move(server_lifetime);
		next->room_address = room_address;
		SDL_StopTextInput(Mode::window);
		Mode::set_current(next);
	} catch (std::exception const &) {
		if (screen == Screen::Starting) return; //The host startup loop retries this connection.
		bool host = bool(server_lifetime);
		cancel_connection();
		screen = host ? Screen::Host : Screen::Join;
		panel_message = "Could not connect. Check the host IP, Wi-Fi and firewall.";
	}
}

void MenuMode::update_connection(float elapsed) {
#ifdef _WIN32
	if (WaitForSingleObject(server_process.get(), 0) != WAIT_TIMEOUT) {
		cancel_connection(); screen = Screen::Host;
		panel_message = "Server stopped. Check port 11451 and try again.";
		return;
	}
#endif
	network_time += elapsed;
	if (network_time < 0.25f) return; //Give the child time to start, without sleeping the menu.
	network_time = 0.0f;
	connect_room("127.0.0.1");
	if (Mode::current.get() == this && ++start_attempts >= 4) {
		cancel_connection(); screen = Screen::Host;
		panel_message = "Could not connect to the new room. Please try again.";
	}
}

bool MenuMode::handle_event(SDL_Event const &evt, glm::uvec2 const &window_size)
{
	auto keep_alive = shared_from_this(); //A successful connection replaces this mode.
	if (screen == Screen::Play) {
		if (evt.type == SDL_EVENT_KEY_DOWN && evt.key.key == SDLK_R) { selection_time = 0.0f; selection_pod = -1; }
		return PlayMode::handle_event(evt, window_size);
	}
	if (screen == Screen::Join && evt.type == SDL_EVENT_TEXT_INPUT) {
		std::string typed = evt.text.text;
		if (typed.find_first_not_of("0123456789.") == std::string::npos) {
			if (replace_ip) { join_ip.clear(); replace_ip = false; }
			if (join_ip.size() + typed.size() <= 15) join_ip += typed;
			panel_message.clear();
		}
		return true;
	}
	if (evt.type == SDL_EVENT_KEY_DOWN && !evt.key.repeat) {
		if (evt.key.key == SDLK_ESCAPE) {
			if (screen == Screen::Starting) { cancel_connection(); screen = Screen::Host; }
			else screen = screen == Screen::Host || screen == Screen::Join ? Screen::Multiplayer : Screen::Play;
			selected = 0; panel_message.clear();
			if (screen == Screen::Join) SDL_StartTextInput(Mode::window);
			else SDL_StopTextInput(Mode::window);
		} else if (screen == Screen::Starting) {
			return true;
		} else if (screen == Screen::Join && evt.key.key == SDLK_BACKSPACE) {
			if (replace_ip) join_ip.clear(); else if (!join_ip.empty()) join_ip.pop_back();
			replace_ip = false; panel_message.clear();
		} else if (screen == Screen::Join && evt.key.key == SDLK_V && (evt.key.mod & SDL_KMOD_CTRL)) {
			char *clipboard = SDL_GetClipboardText();
			std::string value = clipboard ? clipboard : "";
			SDL_free(clipboard);
			if (value.size() <= 15 && value.find_first_not_of("0123456789.") == std::string::npos) {
				join_ip = value; replace_ip = false; panel_message.clear();
			} else panel_message = "Paste an IPv4 address without a port.";
		} else if (evt.key.key == SDLK_UP || evt.key.key == SDLK_DOWN) {
			int count = screen == Screen::Multiplayer ? 3 : 2;
			selected = (selected + (evt.key.key == SDLK_UP ? count - 1 : 1)) % count;
		} else if (evt.key.key == SDLK_RETURN) {
			if (screen == Screen::Multiplayer) {
				screen = selected == 0 ? Screen::Host : selected == 1 ? Screen::Join : Screen::Play;
				panel_message.clear();
				if (screen == Screen::Join) { replace_ip = true; SDL_StartTextInput(Mode::window); }
			} else if (screen == Screen::Solo) {
				if (selected == 0) panel_message = "Single-player mode is not available yet.";
				else screen = Screen::Play;
			} else if (screen == Screen::Host) {
				if (selected == 0) start_host(); else screen = Screen::Multiplayer;
			} else if (screen == Screen::Join) {
				if (selected == 0) { room_address.clear(); connect_room(join_ip); }
				else { screen = Screen::Multiplayer; SDL_StopTextInput(Mode::window); }
			}
			selected = 0;
		}
	}
	return evt.type != SDL_EVENT_QUIT;
}

void MenuMode::update(float elapsed)
{
	auto keep_alive = shared_from_this();
	if (screen == Screen::Starting) { update_connection(elapsed); return; }
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
		bool connecting = screen == Screen::Starting;
		char const *title = connecting ? "CONNECTING" : screen == Screen::Solo ? "SINGLE PLAYER" : screen == Screen::Host ? "CREATE ROOM" : screen == Screen::Join ? "JOIN ROOM" : "MULTIPLAYER";
		draw_label(title, glm::vec2(center.x, top_left.y + 28.0f * ui_scale), 24.0f, cyan, size.x - 32.0f * ui_scale);
		std::array< char const *, 3 > options = {"CREATE ROOM", "JOIN ROOM", "BACK"};
		int count = 3;
		if (screen == Screen::Solo) { options = {"START", "BACK", ""}; count = 2; }
		if (screen == Screen::Host) {
			options = {"CREATE AND ENTER", "BACK", ""}; count = 2;
			draw_label("Port 11451 - one host per room", glm::vec2(center.x, top_left.y + 76.0f * ui_scale),
				12.0f, glm::vec3(0.75f), size.x - 32.0f * ui_scale);
		}
		if (screen == Screen::Join) {
			options = {"CONNECT", "BACK", ""}; count = 2;
			draw_label("IP: " + join_ip + "_", glm::vec2(center.x, top_left.y + 76.0f * ui_scale),
				16.0f, cyan, size.x - 32.0f * ui_scale);
		}
		if (connecting) {
			count = 0;
			draw_label("Starting room...",
				glm::vec2(center.x, top_left.y + 115.0f * ui_scale), 16.0f, cyan, size.x - 32.0f * ui_scale);
			if (!room_address.empty()) draw_label(room_address, glm::vec2(center.x, top_left.y + 155.0f * ui_scale),
				12.0f, cyan, size.x - 32.0f * ui_scale);
		}
		for (int i = 0; i < count; ++i) {
			std::string text = std::string(i == selected ? "> " : "") + options[size_t(i)];
			draw_label(text, glm::vec2(center.x, top_left.y + (115.0f + 36.0f * float(i)) * ui_scale),
				16.0f, i == selected ? glm::vec3(1.0f, 0.93f, 0.7f) : cyan, size.x - 32.0f * ui_scale);
		}
		draw_label(panel_message, glm::vec2(center.x, top_left.y + 220.0f * ui_scale), 12.0f, glm::vec3(0.9f, 0.75f, 0.55f), size.x - 32.0f * ui_scale);
		draw_label(connecting ? "ESC - CANCEL" : screen == Screen::Join ? "TYPE IP / CTRL+V    ENTER - CONNECT    ESC - BACK" : "UP / DOWN - SELECT    ENTER - CONFIRM    ESC - BACK", glm::vec2(center.x, top_left.y + 265.0f * ui_scale),
			10.0f, glm::vec3(0.75f), size.x - 32.0f * ui_scale);
	}

}
