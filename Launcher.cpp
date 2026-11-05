#define UNICODE
#define _UNICODE
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <iphlpapi.h>
#include <algorithm>
#include <cmath>
#include <cwctype>
#include <string>
#include <vector>

#pragma comment(lib, "Ws2_32.lib")
#pragma comment(lib, "Iphlpapi.lib")
#pragma comment(lib, "Gdi32.lib")
#pragma comment(lib, "User32.lib")

namespace {

constexpr unsigned short Port = 11451;
enum class Page { Home, Host, Join };
Page page = Page::Home;
HWND window = nullptr, ip_edit = nullptr, ip_list = nullptr;
HFONT title_font = nullptr, body_font = nullptr, button_font = nullptr;
HBRUSH field_brush = nullptr;
HANDLE job = nullptr;
std::wstring directory, error;
bool game_open = false;
bool ip_copied = false;

struct Address {
	std::wstring ip;
	std::wstring adapter;
};
std::vector< Address > addresses;

struct Child {
	HANDLE process = nullptr;
	HANDLE output = nullptr;
	std::string log;
	ULONGLONG started = 0;
	bool ready = false;
};
Child server, client;

RECT host_button = {220, 230, 600, 286};
RECT join_button = {220, 310, 600, 366};
RECT copy_button = {220, 300, 400, 344};
RECT enter_button = {420, 300, 600, 344};
RECT connect_button = {220, 300, 600, 344};
RECT back_button = {220, 380, 600, 424};

void stop(Child &child) {
	//Only handles created by this launcher are ever stopped.
	if (child.process) {
		if (WaitForSingleObject(child.process, 0) == WAIT_TIMEOUT) {
			TerminateProcess(child.process, 0);
			WaitForSingleObject(child.process, 1000);
		}
		CloseHandle(child.process);
	}
	if (child.output) CloseHandle(child.output);
	child = Child();
}

bool start(Child &child, wchar_t const *filename, std::wstring const &arguments) {
	std::wstring executable = directory + filename;
	if (GetFileAttributesW(executable.c_str()) == INVALID_FILE_ATTRIBUTES) {
		error = std::wstring(L"Missing ") + filename + L". Build the game and keep all dist files together.";
		return false;
	}
	SECURITY_ATTRIBUTES security = {sizeof(security), nullptr, TRUE};
	HANDLE write = nullptr;
	if (!CreatePipe(&child.output, &write, &security, 0)) {
		error = L"Could not create the game process. Please try again.";
		return false;
	}
	SetHandleInformation(child.output, HANDLE_FLAG_INHERIT, 0);
	HANDLE input = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &security, OPEN_EXISTING, 0, nullptr);
	STARTUPINFOW startup = {};
	startup.cb = sizeof(startup);
	startup.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
	startup.wShowWindow = std::wstring(filename) == L"client.exe" ? SW_SHOW : SW_HIDE;
	startup.hStdInput = input;
	startup.hStdOutput = write;
	startup.hStdError = write;
	PROCESS_INFORMATION info = {};
	std::wstring command = L"\"" + executable + L"\" " + arguments;
	BOOL created = CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, TRUE,
		CREATE_NO_WINDOW | CREATE_SUSPENDED, nullptr, directory.c_str(), &startup, &info);
	DWORD launch_error = created ? 0 : GetLastError();
	CloseHandle(write);
	if (input != INVALID_HANDLE_VALUE) CloseHandle(input);
	if (!created) {
		CloseHandle(child.output);
		child.output = nullptr;
		error = L"Could not start the program. Windows error: " + std::to_wstring(launch_error);
		return false;
	}
	if (!AssignProcessToJobObject(job, info.hProcess)) {
		TerminateProcess(info.hProcess, 1);
		CloseHandle(info.hProcess);
		CloseHandle(info.hThread);
		CloseHandle(child.output);
		child.output = nullptr;
		error = L"Could not manage the game process. Please restart the launcher.";
		return false;
	}
	child.process = info.hProcess;
	child.started = GetTickCount64();
	ResumeThread(info.hThread);
	CloseHandle(info.hThread);
	return true;
}

void read_output(Child &child, char const *marker) {
	if (!child.output) return;
	DWORD available = 0;
	while (PeekNamedPipe(child.output, nullptr, 0, nullptr, &available, nullptr) && available) {
		char buffer[2048];
		DWORD count = 0;
		if (!ReadFile(child.output, buffer, std::min<DWORD>(available, sizeof(buffer)), &count, nullptr) || !count) break;
		child.log.append(buffer, count);
		if (child.log.find(marker) != std::string::npos) child.ready = true;
		if (child.log.size() > 8192) child.log.erase(0, child.log.size() - 8192);
	}
}

void find_addresses() {
	addresses.clear();
	ULONG size = 16000;
	std::vector< unsigned char > buffer(size);
	auto adapters = reinterpret_cast< IP_ADAPTER_ADDRESSES * >(buffer.data());
	ULONG result = GetAdaptersAddresses(AF_INET, GAA_FLAG_INCLUDE_GATEWAYS, nullptr, adapters, &size);
	if (result == ERROR_BUFFER_OVERFLOW) {
		buffer.resize(size);
		adapters = reinterpret_cast< IP_ADAPTER_ADDRESSES * >(buffer.data());
		result = GetAdaptersAddresses(AF_INET, GAA_FLAG_INCLUDE_GATEWAYS, nullptr, adapters, &size);
	}
	if (result == NO_ERROR) {
		for (auto adapter = adapters; adapter; adapter = adapter->Next) {
			if (adapter->OperStatus != IfOperStatusUp || adapter->IfType == IF_TYPE_SOFTWARE_LOOPBACK || !adapter->FirstGatewayAddress) continue;
			std::wstring name = adapter->FriendlyName ? adapter->FriendlyName : L"Network";
			std::wstring lower = name;
			std::transform(lower.begin(), lower.end(), lower.begin(), [](wchar_t c) { return wchar_t(std::towlower(c)); });
			if (lower.find(L"vmware") != std::wstring::npos || lower.find(L"virtual") != std::wstring::npos || lower.find(L"vethernet") != std::wstring::npos) continue;
			for (auto address = adapter->FirstUnicastAddress; address; address = address->Next) {
				if (address->Address.lpSockaddr->sa_family != AF_INET) continue;
				auto ipv4 = reinterpret_cast< sockaddr_in * >(address->Address.lpSockaddr);
				wchar_t ip[INET_ADDRSTRLEN];
				if (!InetNtopW(AF_INET, &ipv4->sin_addr, ip, INET_ADDRSTRLEN)) continue;
				if (std::wstring(ip).starts_with(L"169.254.") || std::wstring(ip).starts_with(L"127.")) continue;
				addresses.push_back({ip, name});
			}
		}
	}
	SendMessageW(ip_list, CB_RESETCONTENT, 0, 0);
	for (auto const &address : addresses) {
		std::wstring label = address.ip + L"  (" + address.adapter + L")";
		SendMessageW(ip_list, CB_ADDSTRING, 0, reinterpret_cast< LPARAM >(label.c_str()));
	}
	if (!addresses.empty()) SendMessageW(ip_list, CB_SETCURSEL, 0, 0);
	else error = L"No Wi-Fi or Ethernet IPv4 found. Connect to a network; local testing still works.";
}

bool port_available() {
	SOCKET socket = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (socket == INVALID_SOCKET) return false;
	BOOL exclusive = TRUE;
	setsockopt(socket, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, reinterpret_cast< char * >(&exclusive), sizeof(exclusive));
	sockaddr_in address = {};
	address.sin_family = AF_INET;
	address.sin_port = htons(Port);
	address.sin_addr.s_addr = INADDR_ANY;
	bool available = bind(socket, reinterpret_cast< sockaddr * >(&address), sizeof(address)) == 0;
	closesocket(socket);
	return available;
}

void show_page(Page next) {
	page = next;
	ip_copied = false;
	error.clear();
	ShowWindow(ip_edit, page == Page::Join ? SW_SHOW : SW_HIDE);
	ShowWindow(ip_list, page == Page::Host ? SW_SHOW : SW_HIDE);
	if (page == Page::Join) SetFocus(ip_edit);
	InvalidateRect(window, nullptr, FALSE);
}

void join() {
	if (client.process) return;
	wchar_t buffer[128];
	GetWindowTextW(ip_edit, buffer, 128);
	std::wstring ip = buffer;
	ip.erase(0, ip.find_first_not_of(L" \t\r\n"));
	auto last = ip.find_last_not_of(L" \t\r\n");
	if (last != std::wstring::npos) ip.erase(last + 1);
	IN_ADDR address;
	if (InetPtonW(AF_INET, ip.c_str(), &address) != 1) {
		error = L"Enter the host's IPv4 address, for example 192.168.1.23.";
		return;
	}
	error.clear();
	start(client, L"client.exe", ip + L" 11451");
}

void copy_ip() {
	LRESULT selected = SendMessageW(ip_list, CB_GETCURSEL, 0, 0);
	if (selected < 0 || size_t(selected) >= addresses.size()) return;
	auto const &ip = addresses[size_t(selected)].ip;
	if (!OpenClipboard(window)) { error = L"Could not copy the address. Please try again."; return; }
	HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, (ip.size() + 1) * sizeof(wchar_t));
	if (memory) {
		void *destination = GlobalLock(memory);
		if (destination) {
			memcpy(destination, ip.c_str(), (ip.size() + 1) * sizeof(wchar_t));
			GlobalUnlock(memory);
			EmptyClipboard();
			if (SetClipboardData(CF_UNICODETEXT, memory)) ip_copied = true;
			else GlobalFree(memory);
		} else GlobalFree(memory);
	}
	CloseClipboard();
}

void text(HDC dc, std::wstring const &value, RECT area, HFONT font, COLORREF color) {
	SelectObject(dc, font);
	SetTextColor(dc, color);
	SetBkMode(dc, TRANSPARENT);
	DrawTextW(dc, value.c_str(), -1, &area, DT_CENTER | DT_WORDBREAK | DT_NOPREFIX);
}

void button(HDC dc, RECT area, wchar_t const *label, bool enabled = true) {
	HBRUSH brush = CreateSolidBrush(enabled ? RGB(24, 53, 66) : RGB(24, 27, 30));
	FillRect(dc, &area, brush);
	DeleteObject(brush);
	RECT inside = area;
	inside.top += (area.bottom - area.top - 25) / 2;
	text(dc, label, inside, button_font, enabled ? RGB(220, 242, 255) : RGB(95, 100, 108));
}

void paint(HDC target) {
	RECT bounds;
	GetClientRect(window, &bounds);
	HDC dc = CreateCompatibleDC(target);
	HBITMAP bitmap = CreateCompatibleBitmap(target, bounds.right, bounds.bottom);
	HGDIOBJ old = SelectObject(dc, bitmap);
	FillRect(dc, &bounds, reinterpret_cast< HBRUSH >(GetStockObject(BLACK_BRUSH)));
	unsigned random = 466;
	for (int i = 0; i < 90; ++i) {
		random = random * 1664525u + 1013904223u;
		int x = int(random % 820);
		random = random * 1664525u + 1013904223u;
		int y = int(random % 560);
		int light = 45 + int(120.0 * (0.5 + 0.5 * std::sin(double(GetTickCount64()) * 0.001 + double(i))));
		SetPixelV(dc, x, y, RGB(light, light, light));
	}
	text(dc, L"NEWTON3", {0, 65, 820, 130}, title_font, RGB(225, 245, 255));
	text(dc, page == Page::Home ? L"TWO ASTRONAUTS. ONE WAY OUT." : page == Page::Host ? L"HOST GAME" : L"JOIN GAME",
		{20, 148, 800, 180}, body_font, RGB(115, 190, 215));
	bool busy = client.process != nullptr;
	if (page == Page::Home) {
		button(dc, host_button, L"HOST GAME");
		button(dc, join_button, L"JOIN GAME");
		text(dc, L"Connect both computers to the same Wi-Fi or hotspot.", {50, 420, 770, 455}, body_font, RGB(160, 170, 180));
	} else if (page == Page::Host) {
		text(dc, busy ? L"Opening game..." : server.ready ? L"Server ready. Share your IP with the other player." : server.process ? L"Starting server..." : L"Server unavailable. Return and try again.",
			{40, 195, 780, 230}, body_font, RGB(205, 225, 235));
		button(dc, copy_button, ip_copied ? L"COPIED!" : L"COPY IP", !addresses.empty());
		button(dc, enter_button, L"ENTER GAME", server.ready && !busy);
		button(dc, back_button, L"BACK", !busy);
		text(dc, L"Port 11451  |  Only one host is needed.", {40, 350, 780, 375}, body_font, RGB(155, 165, 175));
	} else {
		text(dc, L"Host IPv4 address", {220, 195, 600, 225}, body_font, RGB(205, 225, 235));
		button(dc, connect_button, busy ? L"CONNECTING..." : L"JOIN", !busy);
		button(dc, back_button, L"BACK", !busy);
		text(dc, L"Paste with Ctrl+V. Press Enter to join. Port: 11451", {40, 350, 780, 375}, body_font, RGB(155, 165, 175));
	}
	text(dc, error, {55, 458, 765, 535}, body_font, RGB(255, 150, 130));
	BitBlt(target, 0, 0, bounds.right, bounds.bottom, dc, 0, 0, SRCCOPY);
	SelectObject(dc, old);
	DeleteObject(bitmap);
	DeleteDC(dc);
}

void update() {
	read_output(server, "NEWTON3_SERVER_READY");
	read_output(client, "NEWTON3_CLIENT_READY");
	if (server.process && WaitForSingleObject(server.process, 0) == WAIT_OBJECT_0) {
		stop(server);
		stop(client);
		game_open = false;
		ShowWindow(window, SW_SHOW);
		error = L"Server stopped. Check that port 11451 is free, then try Host again.";
	} else if (server.process && !server.ready && GetTickCount64() - server.started > 10000) {
		stop(server);
		error = L"Server could not start. Check Windows Firewall and try again.";
	}
	if (client.process) {
		if (WaitForSingleObject(client.process, 0) == WAIT_OBJECT_0) {
			DWORD code = 0;
			GetExitCodeProcess(client.process, &code);
			bool was_ready = client.ready;
			stop(client);
			game_open = false;
			ShowWindow(window, SW_SHOW);
			SetForegroundWindow(window);
			if (!was_ready || code != 0) error = L"Could not join the game. Check the IP, host, firewall and matching game version.";
		} else if (client.ready && !game_open) {
			game_open = true;
			ShowWindow(window, SW_HIDE);
		} else if (!client.ready && GetTickCount64() - client.started > 12000) {
			stop(client);
			error = L"Connection timed out. Check the host IP and that both PCs use the same network.";
		}
	}
	EnableWindow(ip_edit, !client.process);
	InvalidateRect(window, nullptr, FALSE);
}

LRESULT CALLBACK handle_window(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
	if (message == WM_PAINT) {
		PAINTSTRUCT paint_info;
		HDC dc = BeginPaint(hwnd, &paint_info);
		paint(dc);
		EndPaint(hwnd, &paint_info);
		return 0;
	}
	if (message == WM_ERASEBKGND) return 1;
	if (message == WM_CTLCOLORSTATIC || message == WM_CTLCOLOREDIT || message == WM_CTLCOLORLISTBOX) {
		HDC dc = reinterpret_cast< HDC >(wparam);
		SetTextColor(dc, RGB(235, 245, 255));
		SetBkColor(dc, RGB(18, 28, 35));
		return reinterpret_cast< LRESULT >(field_brush);
	}
	if (message == WM_COMMAND && reinterpret_cast< HWND >(lparam) == ip_list && HIWORD(wparam) == CBN_SELCHANGE) {
		ip_copied = false;
		return 0;
	}
	if (message == WM_TIMER) { update(); return 0; }
	if (message == WM_LBUTTONUP) {
		POINT point = {int(short(LOWORD(lparam))), int(short(HIWORD(lparam)))};
		if (page == Page::Home) {
			if (PtInRect(&host_button, point)) {
				show_page(Page::Host);
				find_addresses();
				if (!port_available()) error = L"Port 11451 is already in use. Close the old server before hosting.";
				else start(server, L"server.exe", L"11451");
			} else if (PtInRect(&join_button, point)) show_page(Page::Join);
		} else if (!client.process) {
			if (PtInRect(&back_button, point)) { stop(server); show_page(Page::Home); }
			else if (page == Page::Host && PtInRect(&copy_button, point)) copy_ip();
			else if (page == Page::Host && server.ready && PtInRect(&enter_button, point)) {
				error.clear();
				start(client, L"client.exe", L"127.0.0.1 11451");
			} else if (page == Page::Join && PtInRect(&connect_button, point)) join();
		}
		InvalidateRect(hwnd, nullptr, FALSE);
		return 0;
	}
	if (message == WM_KEYDOWN && wparam == VK_ESCAPE && !client.process) { stop(server); show_page(Page::Home); return 0; }
	if (message == WM_DESTROY) { PostQuitMessage(0); return 0; }
	return DefWindowProcW(hwnd, message, wparam, lparam);
}

} //namespace

int main(int argc, char **argv) {
	WSADATA winsock;
	if (WSAStartup(MAKEWORD(2, 2), &winsock) != 0) return 1;
	wchar_t path[32768];
	DWORD length = GetModuleFileNameW(nullptr, path, 32768);
	if (!length || length >= 32768) return 1;
	directory = std::wstring(path, length);
	directory.resize(directory.find_last_of(L"\\/") + 1);
	std::wstring font_file = directory + L"Outfit-Regular.ttf";
	AddFontResourceExW(font_file.c_str(), FR_PRIVATE, nullptr);
	title_font = CreateFontW(-52, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Outfit");
	body_font = CreateFontW(-18, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Outfit");
	button_font = CreateFontW(-22, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Outfit");
	field_brush = CreateSolidBrush(RGB(18, 28, 35));
	job = CreateJobObjectW(nullptr, nullptr);
	JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits = {};
	limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
	if (!job || !SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof(limits))) return 1;
	WNDCLASSW type = {};
	type.lpfnWndProc = handle_window;
	type.hInstance = GetModuleHandleW(nullptr);
	type.hCursor = LoadCursorW(nullptr, IDC_ARROW);
	type.lpszClassName = L"Newton3Launcher";
	RegisterClassW(&type);
	RECT bounds = {0, 0, 820, 560};
	DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
	AdjustWindowRect(&bounds, style, FALSE);
	window = CreateWindowExW(0, type.lpszClassName, L"Newton3 - Host / Join", style, CW_USEDEFAULT, CW_USEDEFAULT,
		bounds.right - bounds.left, bounds.bottom - bounds.top, nullptr, nullptr, type.hInstance, nullptr);
	if (!window) return 1;
	ip_edit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_TABSTOP | ES_AUTOHSCROLL,
		220, 235, 380, 36, window, nullptr, type.hInstance, nullptr);
	SendMessageW(ip_edit, WM_SETFONT, reinterpret_cast< WPARAM >(button_font), TRUE);
	SendMessageW(ip_edit, EM_SETLIMITTEXT, 64, 0);
	ip_list = CreateWindowExW(0, L"COMBOBOX", L"", WS_CHILD | WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL,
		160, 240, 500, 160, window, nullptr, type.hInstance, nullptr);
	SendMessageW(ip_list, WM_SETFONT, reinterpret_cast< WPARAM >(body_font), TRUE);
	ShowWindow(window, SW_SHOW);
	SetTimer(window, 1, 50, nullptr);
	MSG message;
	while (GetMessageW(&message, nullptr, 0, 0) > 0) {
		if (message.message == WM_KEYDOWN && message.wParam == VK_RETURN && page == Page::Join) {
			join();
			continue;
		}
		if (message.message == WM_KEYDOWN && message.wParam == VK_ESCAPE && !client.process) { stop(server); show_page(Page::Home); continue; }
		TranslateMessage(&message);
		DispatchMessageW(&message);
	}
	stop(client);
	stop(server);
	CloseHandle(job);
	DeleteObject(title_font);
	DeleteObject(body_font);
	DeleteObject(button_font);
	DeleteObject(field_brush);
	RemoveFontResourceExW(font_file.c_str(), FR_PRIVATE, nullptr);
	WSACleanup();
	return 0;
}
