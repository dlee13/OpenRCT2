/*****************************************************************************
* Copyright (c) 2014 Ted John
* OpenRCT2, an open source clone of Roller Coaster Tycoon 2.
*
* This file is part of OpenRCT2.
*
* OpenRCT2 is free software: you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation, either version 3 of the License, or
* (at your option) any later version.

* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.

* You should have received a copy of the GNU General Public License
* along with this program.  If not, see <http://www.gnu.org/licenses/>.
*****************************************************************************/

#include "../config.h"
#include "../interface/colour.h"
#include "../interface/themes.h"
#include "../interface/widget.h"
#include "../interface/window.h"
#include "../localisation/localisation.h"
#include "../network/http.h"
#include "../network/network.h"
#include "../sprites.h"
#include "../windows/dropdown.h"
#include "../windows/tooltip.h"
#include "../util/util.h"
#include "error.h"

#define WWIDTH_MIN 500
#define WHEIGHT_MIN 300
#define WWIDTH_MAX 1200
#define WHEIGHT_MAX 800
#define ITEM_HEIGHT (3 + 9 + 3)

typedef struct {
	char *address;
	utf8 *name;
	bool requiresPassword;
	utf8 *description;
	char *version;
	bool favourite;
	uint8 players;
	uint8 maxplayers;
} server_entry;

static char _playerName[32 + 1];
static server_entry *_serverEntries = NULL;
static int _numServerEntries = 0;
static SDL_mutex *_mutex = 0;
static uint32 _numPlayersOnline = 0;

enum {
	WIDX_BACKGROUND,
	WIDX_TITLE,
	WIDX_CLOSE,
	WIDX_PLAYER_NAME_INPUT,
	WIDX_LIST,
	WIDX_FETCH_SERVERS,
	WIDX_ADD_SERVER,
	WIDX_START_SERVER
};

enum {
	WIDX_LIST_REMOVE,
	WIDX_LIST_SPECTATE
};

static rct_widget window_server_list_widgets[] = {
	{ WWT_FRAME,			0,	0,		340,	0,		90,		0xFFFFFFFF,					STR_NONE },					// panel / background
	{ WWT_CAPTION,			0,	1,		338,	1,		14,		STR_SERVER_LIST,			STR_WINDOW_TITLE_TIP },		// title bar
	{ WWT_CLOSEBOX,			0,	327,	337,	2,		13,		STR_CLOSE_X,				STR_CLOSE_WINDOW_TIP },		// close x button
	{ WWT_TEXT_BOX,			1,	100,	344,	20,		31,		(uint32)_playerName,		STR_NONE },					// player name text box
	{ WWT_SCROLL,			1,	6,		337,	37,		50,		STR_NONE,					STR_NONE },					// server list
	{ WWT_DROPDOWN_BUTTON,	1,	6,		106,	53,		64,		STR_FETCH_SERVERS,			STR_NONE },					// fetch servers button
	{ WWT_DROPDOWN_BUTTON,	1,	112,	212,	53,		64,		STR_ADD_SERVER,				STR_NONE },					// add server button
	{ WWT_DROPDOWN_BUTTON,	1,	218,	318,	53,		64,		STR_START_SERVER,			STR_NONE },					// start server button
	{ WIDGETS_END },
};

static void window_server_list_close(rct_window *w);
static void window_server_list_mouseup(rct_window *w, int widgetIndex);
static void window_server_list_resize(rct_window *w);
static void window_server_list_dropdown(rct_window *w, int widgetIndex, int dropdownIndex);
static void window_server_list_update(rct_window *w);
static void window_server_list_scroll_getsize(rct_window *w, int scrollIndex, int *width, int *height);
static void window_server_list_scroll_mousedown(rct_window *w, int scrollIndex, int x, int y);
static void window_server_list_scroll_mouseover(rct_window *w, int scrollIndex, int x, int y);
static void window_server_list_textinput(rct_window *w, int widgetIndex, char *text);
static void window_server_list_invalidate(rct_window *w);
static void window_server_list_paint(rct_window *w, rct_drawpixelinfo *dpi);
static void window_server_list_scrollpaint(rct_window *w, rct_drawpixelinfo *dpi, int scrollIndex);

static rct_window_event_list window_server_list_events = {
	window_server_list_close,
	window_server_list_mouseup,
	window_server_list_resize,
	NULL,
	window_server_list_dropdown,
	NULL,
	window_server_list_update,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	window_server_list_scroll_getsize,
	window_server_list_scroll_mousedown,
	NULL,
	window_server_list_scroll_mouseover,
	window_server_list_textinput,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	window_server_list_invalidate,
	window_server_list_paint,
	window_server_list_scrollpaint
};

enum {
	DDIDX_JOIN,
	DDIDX_FAVOURITE
};

static int _hoverButtonIndex = -1;

static void server_list_get_item_button(int buttonIndex, int x, int y, int width, int *outX, int *outY);
static void server_list_load_server_entries();
static void server_list_save_server_entries();
static void dispose_server_entry_list();
static void dispose_server_entry(server_entry *serverInfo);
static server_entry* add_server_entry(char *address);
static void remove_server_entry(int index);
static void join_server(char *address);
static void fetch_servers();
#ifndef DISABLE_HTTP
static void fetch_servers_callback(http_json_response* response);
#endif

void window_server_list_open()
{
	rct_window* window;

	// Check if window is already open
	window = window_bring_to_front_by_class(WC_SERVER_LIST);
	if (window != NULL)
		return;

	if (_mutex == 0) {
		_mutex = SDL_CreateMutex();
	}

	window = window_create_centred(WWIDTH_MIN, WHEIGHT_MIN, &window_server_list_events, WC_SERVER_LIST, WF_10 | WF_RESIZABLE);

	window->widgets = window_server_list_widgets;
	window->enabled_widgets = (
		(1 << WIDX_CLOSE) |
		(1 << WIDX_PLAYER_NAME_INPUT) |
		(1 << WIDX_FETCH_SERVERS) |
		(1 << WIDX_ADD_SERVER) |
		(1 << WIDX_START_SERVER)
	);
	window_init_scroll_widgets(window);
	window->no_list_items = 0;
	window->selected_list_item = -1;
	window->frame_no = 0;
	window->min_width = 320;
	window->min_height = 90;
	window->max_width = window->min_width;
	window->max_height = window->min_height;

	window->page = 0;
	window->list_information_type = 0;
	window->colours[0] = 1;
	window->colours[1] = 26;
	window->colours[2] = 26;

	window_set_resize(window, WWIDTH_MIN, WHEIGHT_MIN, WWIDTH_MAX, WHEIGHT_MAX);

	safe_strcpy(_playerName, gConfigNetwork.player_name, sizeof(_playerName));

	server_list_load_server_entries();
	window->no_list_items = _numServerEntries;

	fetch_servers();
}

static void window_server_list_close(rct_window *w)
{
	dispose_server_entry_list();
	if (_mutex) {
		SDL_LockMutex(_mutex);
		SDL_DestroyMutex(_mutex);
		_mutex = 0;
	}
}

static void window_server_list_mouseup(rct_window *w, int widgetIndex)
{
	switch (widgetIndex) {
	case WIDX_CLOSE:
		window_close(w);
		break;
	case WIDX_PLAYER_NAME_INPUT:
		window_start_textbox(w, widgetIndex, 1170, (uint32)_playerName, 63);
		break;
	case WIDX_LIST:{
		int serverIndex = w->selected_list_item;
		if (serverIndex >= 0 && serverIndex < _numServerEntries) {
			if (strcmp(_serverEntries[serverIndex].version, NETWORK_STREAM_ID) != 0) {
				RCT2_GLOBAL(RCT2_ADDRESS_COMMON_FORMAT_ARGS, void *) = _serverEntries[serverIndex].version;
				window_error_open(STR_UNABLE_TO_CONNECT_TO_SERVER, STR_MULTIPLAYER_INCORRECT_SOFTWARE_VERSION);
				break;
			}
			char *serverAddress = _serverEntries[serverIndex].address;
			join_server(serverAddress);
		}
		}break;
	case WIDX_FETCH_SERVERS:
		fetch_servers();
		break;
	case WIDX_ADD_SERVER:
		window_text_input_open(w, widgetIndex, STR_ADD_SERVER, STR_ENTER_HOSTNAME_OR_IP_ADDRESS, STR_NONE, 0, 128);
		break;
	case WIDX_START_SERVER:
		window_server_start_open();
		break;
	}
}

static void window_server_list_resize(rct_window *w)
{
	window_set_resize(w, WWIDTH_MIN, WHEIGHT_MIN, WWIDTH_MAX, WHEIGHT_MAX);
}

static void window_server_list_dropdown(rct_window *w, int widgetIndex, int dropdownIndex)
{
	int serverIndex = w->selected_list_item;
	if (serverIndex < 0) return;
	if (serverIndex >= _numServerEntries) return;

	char *serverAddress = _serverEntries[serverIndex].address;

	switch (dropdownIndex) {
	case DDIDX_JOIN:
		if (strcmp(_serverEntries[serverIndex].version, NETWORK_STREAM_ID) != 0) {
			RCT2_GLOBAL(RCT2_ADDRESS_COMMON_FORMAT_ARGS, void *) = _serverEntries[serverIndex].version;
			window_error_open(STR_UNABLE_TO_CONNECT_TO_SERVER, STR_MULTIPLAYER_INCORRECT_SOFTWARE_VERSION);
			break;
		}
		join_server(serverAddress);
		break;
	case DDIDX_FAVOURITE:
		_serverEntries[serverIndex].favourite = !_serverEntries[serverIndex].favourite;
		server_list_save_server_entries();
		break;
	}
}

static void window_server_list_update(rct_window *w)
{
	if (gCurrentTextBox.window.classification == w->classification && gCurrentTextBox.window.number == w->number) {
		window_update_textbox_caret();
		widget_invalidate(w, WIDX_PLAYER_NAME_INPUT);
	}
}

static void window_server_list_scroll_getsize(rct_window *w, int scrollIndex, int *width, int *height)
{
	*width = 0;
	*height = w->no_list_items * ITEM_HEIGHT;
}

static void window_server_list_scroll_mousedown(rct_window *w, int scrollIndex, int x, int y)
{
	int serverIndex = w->selected_list_item;
	if (serverIndex < 0) return;
	if (serverIndex >= _numServerEntries) return;

	char *serverAddress = _serverEntries[serverIndex].address;

	rct_widget *listWidget = &w->widgets[WIDX_LIST];
	int ddx = w->x + listWidget->left + x + 2 - w->scrolls[0].h_left;
	int ddy = w->y + listWidget->top + y + 2 - w->scrolls[0].v_top;

	gDropdownItemsFormat[0] = STR_JOIN_GAME;
	if (_serverEntries[serverIndex].favourite) {
		gDropdownItemsFormat[1] = STR_REMOVE_FROM_FAVOURITES;
	} else {
		gDropdownItemsFormat[1] = STR_ADD_TO_FAVOURITES;
	}
	window_dropdown_show_text(ddx, ddy, 0, COLOUR_GREY, 0, 2);
}

char *gVersion = NULL;

static void window_server_list_scroll_mouseover(rct_window *w, int scrollIndex, int x, int y)
{
	// Item
	int index = y / ITEM_HEIGHT;
	if (index < 0 || index >= w->no_list_items) {
		index = -1;
	}

	int hoverButtonIndex = -1;
	if (index != -1) {
		int width = w->widgets[WIDX_LIST].right - w->widgets[WIDX_LIST].left;
		int sy = index * ITEM_HEIGHT;
		for (int i = 0; i < 2; i++) {
			int bx, by;

			server_list_get_item_button(i, 0, sy, width, &bx, &by);
			if (x >= bx && y >= by && x < bx + 24 && y < by + 24) {
				hoverButtonIndex = i;
				break;
			}
		}
	}

	int width = w->widgets[WIDX_LIST].right - w->widgets[WIDX_LIST].left;
	int right = width - 3 - 14 - 10;
	if (x < right)
	{
		w->widgets[WIDX_LIST].tooltip = STR_NONE;
		window_tooltip_close();
	}

	if (w->selected_list_item != index || _hoverButtonIndex != hoverButtonIndex) {
		w->selected_list_item = index;
		_hoverButtonIndex = hoverButtonIndex;
		window_tooltip_close();
		window_invalidate(w);
	}
}

static void window_server_list_textinput(rct_window *w, int widgetIndex, char *text)
{
	if (text == NULL || text[0] == 0) return;

	switch (widgetIndex) {
	case WIDX_PLAYER_NAME_INPUT:
		if (strcmp(_playerName, text) == 0)
			return;

		if (strlen(text) == 0) {
			memset(_playerName, 0, sizeof(_playerName));
		} else {
			memset(_playerName, 0, sizeof(_playerName));
			safe_strcpy(_playerName, text, sizeof(_playerName));
		}

		if (strlen(_playerName) > 0) {
			SafeFree(gConfigNetwork.player_name);
			gConfigNetwork.player_name = _strdup(_playerName);
			config_save_default();
		}

		widget_invalidate(w, WIDX_PLAYER_NAME_INPUT);
		break;

	case WIDX_ADD_SERVER:
		add_server_entry(text);
		server_list_save_server_entries();
		window_invalidate(w);
		break;
	}
}

static void window_server_list_invalidate(rct_window *w)
{
	colour_scheme_update(w);

	RCT2_GLOBAL(RCT2_ADDRESS_COMMON_FORMAT_ARGS, char *) = gVersion;
	window_server_list_widgets[WIDX_BACKGROUND].right = w->width - 1;
	window_server_list_widgets[WIDX_BACKGROUND].bottom = w->height - 1;
	window_server_list_widgets[WIDX_TITLE].right = w->width - 2;
	window_server_list_widgets[WIDX_CLOSE].left = w->width - 2 - 11;
	window_server_list_widgets[WIDX_CLOSE].right = w->width - 2 - 11 + 10;

	int margin = 6;
	int buttonHeight = 11;
	int buttonTop = w->height - margin - buttonHeight - 13;
	int buttonBottom = buttonTop + buttonHeight;
	int listBottom = buttonTop - margin;

	window_server_list_widgets[WIDX_PLAYER_NAME_INPUT].right = w->width - 6;
	window_server_list_widgets[WIDX_LIST].left = 6;
	window_server_list_widgets[WIDX_LIST].right = w->width - 6;
	window_server_list_widgets[WIDX_LIST].bottom = listBottom;
	window_server_list_widgets[WIDX_FETCH_SERVERS].top = buttonTop;
	window_server_list_widgets[WIDX_FETCH_SERVERS].bottom = buttonBottom;
	window_server_list_widgets[WIDX_ADD_SERVER].top = buttonTop;
	window_server_list_widgets[WIDX_ADD_SERVER].bottom = buttonBottom;
	window_server_list_widgets[WIDX_START_SERVER].top = buttonTop;
	window_server_list_widgets[WIDX_START_SERVER].bottom = buttonBottom;

	w->no_list_items = _numServerEntries;
}

static void window_server_list_paint(rct_window *w, rct_drawpixelinfo *dpi)
{
	window_draw_widgets(w, dpi);

	gfx_draw_string_left(dpi, STR_PLAYER_NAME, NULL, COLOUR_WHITE, w->x + 6, w->y + w->widgets[WIDX_PLAYER_NAME_INPUT].top);
	char *version = NETWORK_STREAM_ID;
	gfx_draw_string_left(dpi, STR_NETWORK_VERSION, (void*)&version, COLOUR_WHITE, w->x + 324, w->y + w->widgets[WIDX_START_SERVER].top);

	gfx_draw_string_left(dpi, STR_X_PLAYERS_ONLINE, (void*)&_numPlayersOnline, COLOUR_WHITE, w->x + 8, w->y + w->height - 15);
}

static void window_server_list_scrollpaint(rct_window *w, rct_drawpixelinfo *dpi, int scrollIndex)
{
	uint32 colour;

	colour = ColourMapA[w->colours[1]].mid_light;
	colour = (colour << 24) | (colour << 16) | (colour << 8) | colour;
	gfx_clear(dpi, colour);

	int width = w->widgets[WIDX_LIST].right - w->widgets[WIDX_LIST].left;

	int y = 0;
	w->widgets[WIDX_LIST].tooltip = STR_NONE;
	for (int i = 0; i < w->no_list_items; i++) {
		if (y >= dpi->y + dpi->height) continue;
		// if (y + ITEM_HEIGHT < dpi->y) continue;

		server_entry *serverDetails = &_serverEntries[i];
		bool highlighted = i == w->selected_list_item;

		// Draw hover highlight
		if (highlighted) {
			gfx_fill_rect(dpi, 0, y, width, y + ITEM_HEIGHT, 0x02000031);
			gVersion = serverDetails->version;
			w->widgets[WIDX_LIST].tooltip = STR_NETWORK_VERSION_TIP;
		}

		int colour = w->colours[1];
		if (serverDetails->favourite) {
			colour = COLOUR_YELLOW;
		}

		// Draw server information
		if (highlighted && !str_is_null_or_empty(serverDetails->description)) {
			gfx_draw_string(dpi, serverDetails->description, colour, 3, y + 3);
		} else {
			gfx_draw_string(dpi, serverDetails->name, colour, 3, y + 3);
		}

		int right = width - 3 - 14;

		// Draw compatibility icon
		right -= 10;
		int compatibilitySpriteId;
		if (str_is_null_or_empty(serverDetails->version)) {
			// Server not online...
			compatibilitySpriteId = SPR_G2_RCT1_CLOSE_BUTTON_0;
		} else {
			// Server online... check version
			bool correctVersion = strcmp(serverDetails->version, NETWORK_STREAM_ID) == 0;
			compatibilitySpriteId = correctVersion ? SPR_G2_RCT1_OPEN_BUTTON_2 : SPR_G2_RCT1_CLOSE_BUTTON_2;
		}
		gfx_draw_sprite(dpi, compatibilitySpriteId, right, y + 1, 0);
		right -= 4;

		// Draw lock icon
		right -= 8;
		if (serverDetails->requiresPassword) {
			gfx_draw_sprite(dpi, SPR_G2_LOCKED, right, y + 4, 0);
		}
		right -= 6;

		// Draw number of players
		char players[32];
		players[0] = 0;
		if (serverDetails->maxplayers > 0) {
			sprintf(players, "%d/%d", serverDetails->players, serverDetails->maxplayers);
		}
		int numPlayersStringWidth = gfx_get_string_width(players);
		gfx_draw_string(dpi, players, w->colours[1], right - numPlayersStringWidth, y + 3);

		y += ITEM_HEIGHT;
	}
}

static void server_list_get_item_button(int buttonIndex, int x, int y, int width, int *outX, int *outY)
{
	*outX = width - 3 - 36 - (30 * buttonIndex);
	*outY = y + 2;
}

static char *freadstralloc(SDL_RWops *file)
{
	int capacity = 64;
	char *buffer = malloc(capacity);

	int length = 0;
	int c;
	for (;;) {
		c = 0;
		if (SDL_RWread(file, &c, 1, 1) != 1) break;
		if (c == 0) break;

		if (length >= capacity) {
			capacity *= 2;
			buffer = realloc(buffer, capacity);
		}
		buffer[length] = c;
		length++;
	}

	buffer = realloc(buffer, length + 1);
	buffer[length] = 0;
	return buffer;
}

static void server_list_load_server_entries()
{
	utf8 path[MAX_PATH];
	SDL_RWops *file;

	platform_get_user_directory(path, NULL);
	strcat(path, "servers.cfg");

	file = SDL_RWFromFile(path, "rb");
	if (file == NULL) {
		return;
	}

	SDL_LockMutex(_mutex);
	dispose_server_entry_list();

	// Read number of server entries
	SDL_RWread(file, &_numServerEntries, sizeof(uint32), 1);
	_serverEntries = malloc(_numServerEntries * sizeof(server_entry));

	// Load each server entry
	for (int i = 0; i < _numServerEntries; i++) {
		server_entry *serverInfo = &_serverEntries[i];

		serverInfo->address = freadstralloc(file);
		serverInfo->name = freadstralloc(file);
		serverInfo->requiresPassword = false;
		serverInfo->description = freadstralloc(file);
		serverInfo->version = _strdup("");
		serverInfo->favourite = true;
		serverInfo->players = 0;
		serverInfo->maxplayers = 0;
	}

	SDL_RWclose(file);
	SDL_UnlockMutex(_mutex);
}

static void server_list_save_server_entries()
{
	utf8 path[MAX_PATH];
	SDL_RWops *file;

	platform_get_user_directory(path, NULL);
	strcat(path, "servers.cfg");

	file = SDL_RWFromFile(path, "wb");
	if (file == NULL) {
		log_error("Unable to save servers.");
		return;
	}

	SDL_LockMutex(_mutex);
	int count = 0;
	for (int i = 0; i < _numServerEntries; i++) {
		server_entry *serverInfo = &_serverEntries[i];
		if (serverInfo->favourite) {
			count++;
		}
	}
	// Write number of server entries
	SDL_RWwrite(file, &count, sizeof(uint32), 1);

	// Write each server entry
	for (int i = 0; i < _numServerEntries; i++) {
		server_entry *serverInfo = &_serverEntries[i];
		if (serverInfo->favourite) {
			SDL_RWwrite(file, serverInfo->address, strlen(serverInfo->address) + 1, 1);
			SDL_RWwrite(file, serverInfo->name, strlen(serverInfo->name) + 1, 1);
			SDL_RWwrite(file, serverInfo->description, strlen(serverInfo->description) + 1, 1);
		}
	}

	SDL_RWclose(file);
	SDL_UnlockMutex(_mutex);
}

static void dispose_server_entry_list()
{
	SDL_LockMutex(_mutex);
	if (_serverEntries != NULL) {
		for (int i = 0; i < _numServerEntries; i++) {
			dispose_server_entry(&_serverEntries[i]);
		}
		free(_serverEntries);
		_serverEntries = NULL;
	}
	_numServerEntries = 0;
	SDL_UnlockMutex(_mutex);
}

static void dispose_server_entry(server_entry *serverInfo)
{
	SafeFree(serverInfo->address);
	SafeFree(serverInfo->name);
	SafeFree(serverInfo->description);
	SafeFree(serverInfo->version);
}

static server_entry* add_server_entry(char *address)
{
	SDL_LockMutex(_mutex);
	for (int i = 0; i < _numServerEntries; i++) {
		if (strcmp(_serverEntries[i].address, address) == 0) {
			SDL_UnlockMutex(_mutex);
			return &_serverEntries[i];
		}
	}

	_numServerEntries++;
	if (_serverEntries == NULL) {
		_serverEntries = malloc(_numServerEntries * sizeof(server_entry));
	} else {
		_serverEntries = realloc(_serverEntries, _numServerEntries * sizeof(server_entry));
	}

	int index = _numServerEntries - 1;
	server_entry* newserver = &_serverEntries[index];
	newserver->address = _strdup(address);
	newserver->name = _strdup(address);
	newserver->requiresPassword = false;
	newserver->description = _strdup("");
	newserver->version = _strdup("");
	newserver->favourite = false;
	newserver->players = 0;
	newserver->maxplayers = 0;
	SDL_UnlockMutex(_mutex);
	return newserver;
}

static void remove_server_entry(int index)
{
	SDL_LockMutex(_mutex);
	if (_numServerEntries > index) {
		int serversToMove = _numServerEntries - index - 1;
		memmove(&_serverEntries[index], &_serverEntries[index + 1], serversToMove * sizeof(server_entry));

		_numServerEntries--;
		_serverEntries = realloc(_serverEntries, _numServerEntries * sizeof(server_entry));
	}
	SDL_UnlockMutex(_mutex);
}

static char *substr(char *start, int length)
{
	char *result = malloc(length + 1);
	memcpy(result, start, length);
	result[length] = 0;
	return result;
}

static void join_server(char *address)
{
	int port = gConfigNetwork.default_port;

	bool addresscopied = false;

	char *endbracket = strrchr(address, ']');
	char *startbracket = strrchr(address, '[');
	char *dot = strchr(address, '.');

	char *colon = strrchr(address, ':');
	if (colon != NULL && (endbracket != NULL || dot != NULL)) {
		address = substr(address, colon - address);
		sscanf(colon + 1, "%d", &port);
		addresscopied = true;
	}

	if (startbracket && endbracket) {
		address = substr(startbracket + 1, endbracket - startbracket - 1);
		addresscopied = true;
	}

	if (!network_begin_client(address, port)) {
		window_error_open(STR_UNABLE_TO_CONNECT_TO_SERVER, STR_NONE);
	}

	if (addresscopied) {
		free(address);
	}
}

static uint32 get_total_player_count()
{
	uint32 numPlayers = 0;
	for (int i = 0; i < _numServerEntries; i++) {
		server_entry *serverDetails = &_serverEntries[i];
		numPlayers += serverDetails->players;
	}
	return numPlayers;
}

static void fetch_servers()
{
#ifndef DISABLE_HTTP
	const char *masterServerUrl = OPENRCT2_MASTER_SERVER_URL;
	if (!str_is_null_or_empty(gConfigNetwork.master_server_url)) {
		masterServerUrl = gConfigNetwork.master_server_url;
	}

	SDL_LockMutex(_mutex);
	for (int i = 0; i < _numServerEntries; i++) {
		if (!_serverEntries[i].favourite) {
			remove_server_entry(i);
			i = 0;
		}
	}
	SDL_UnlockMutex(_mutex);

	http_json_request request;
	request.url = masterServerUrl;
	request.method = HTTP_METHOD_GET;
	request.body = NULL;
	http_request_json_async(&request, fetch_servers_callback);
#endif
}

#ifndef DISABLE_HTTP
static void fetch_servers_callback(http_json_response* response)
{
	if (response == NULL) {
		log_warning("Unable to connect to master server");
		return;
	}

	json_t *jsonStatus = json_object_get(response->root, "status");
	if (!json_is_number(jsonStatus)) {
		http_request_json_dispose(response);
		log_warning("Invalid response from master server");
		return;
	}

	int status = (int)json_integer_value(jsonStatus);
	if (status != 200) {
		http_request_json_dispose(response);
		log_warning("Master server failed to return servers");
		return;
	}

	json_t *jsonServers = json_object_get(response->root, "servers");
	if (!json_is_array(jsonServers)) {
		http_request_json_dispose(response);
		log_warning("Invalid response from master server");
		return;
	}

	int count = json_array_size(jsonServers);
	for (int i = 0; i < count; i++) {
		json_t *server = json_array_get(jsonServers, i);
		if (!json_is_object(server)) {
			continue;
		}

		json_t *port = json_object_get(server, "port");
		json_t *name = json_object_get(server, "name");
		json_t *description = json_object_get(server, "description");
		json_t *requiresPassword = json_object_get(server, "requiresPassword");
		json_t *version = json_object_get(server, "version");
		json_t *players = json_object_get(server, "players");
		json_t *maxPlayers = json_object_get(server, "maxPlayers");
		json_t *ip = json_object_get(server, "ip");
		json_t *ip4 = json_object_get(ip, "v4");
		json_t *ip6 = json_object_get(ip, "v6");
		json_t *addressIp = json_array_get(ip4, 0);

		if (name == NULL || version == NULL)
		{
			log_verbose("Cowardly refusing to add server without name or version specified.");
			continue;
		}

		char address[256];
		snprintf(address, sizeof(address), "%s:%d", json_string_value(addressIp), (int)json_integer_value(port));

		SDL_LockMutex(_mutex);
		server_entry* newserver = add_server_entry(address);
		SafeFree(newserver->name);
		SafeFree(newserver->description);
		SafeFree(newserver->version);
		newserver->name = _strdup(json_string_value(name));
		newserver->requiresPassword = json_boolean_value(requiresPassword);
		newserver->description = _strdup(description == NULL ? "" : json_string_value(description));
		newserver->version = _strdup(json_string_value(version));
		newserver->players = (uint8)json_integer_value(players);
		newserver->maxplayers = (uint8)json_integer_value(maxPlayers);
		SDL_UnlockMutex(_mutex);
	}
	http_request_json_dispose(response);

	_numPlayersOnline = get_total_player_count();

	rct_window *window = window_find_by_class(WC_SERVER_LIST);
	if (window != NULL) {
		window_invalidate(window);
	}
}
#endif
