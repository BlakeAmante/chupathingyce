/*
MENU_STYLE.C

The Chupathingy menu style (display.menus = "chupathingy"): the Xbox's
menus, their screens, order and buttons as they are, with some screens
drawn larger over them in the style of Online Games (browser_screen.c):
clearer lists and more of what is picked on screen. The Xbox's widgets stay
underneath and keep working: they still have the focus, take the buttons
and run their handlers, and this file only draws what they hold and moves
their focus. A prototype: the main menu and the multiplayer map and
gametype pickers so far.

  - The main menu keeps the Xbox's logo and items, and adds a line about the
    focused item and a bar along the foot (this version, the online games
    and players on the game list, the A prompt).
  - The map picker (the Xbox's three cards) is a list of every map, the
    Xbox's and the Halo PC maps found, with each one's player count, and the
    focused map's picture, name, description and details beside it.
  - The gametype picker the same: every saved gametype in a list, and the
    focused one's picture, description and rules.

On the pickers, up and down move through the list as left and right do on
the Xbox's cards (ui_widget.c asks menu_style_event), and the pointer picks
a row, and A on a picked row (menu_style_pointer). Everything is drawn with
the overlay (ui_overlay.c), the pictures by the game into its cut-outs.
*/

#ifdef HALO_GAME_BROWSER

#include "cseries.h"
#include "cseries/cseries_windows.h"
#include "bitmaps/bitmap_group.h"
#include "cutscene/cinematics.h"
#include "game/game_engine.h"
#include "input/input.h"
#include "interface/event_manager.h"
#include "interface/ui_widget.h"
#include "rasterizer/rasterizer.h"
#include "saved games/playlist_profile.h"
#include "tag_files/tag_files.h"
#include "tag_files/tag_groups.h"
#include "text/text_group.h"

#include "halo_map_families.h"
#include "halo_ui_map_list.h"
#include "halo_ui_pointer.h"
#include "../src/browser.h"
#include "../src/ui_overlay.h"

#include <stdio.h>
#include <string.h>

/* ---------- widgets, as ui_widget.c has them */

#ifdef HALO_64BIT
#include "interface/ui_widget_instance.h"
#else
struct widget_instance
{
	long definition_tag_index;
	char const *name;
	short local_player_index;
	short horizontal_offset;
	short vertical_offset;
	short type;
	boolean visible;
	boolean render_regardless_of_controller_index;
	boolean disabled;
	boolean pause_game_time;
	boolean delete_recursion_lock;
	boolean widget_is_error_dialog;
	boolean close_if_local_player_controller_present;
	byte pad17;
	long creation_time;
	unsigned long milliseconds_to_auto_close;
	unsigned long auto_close_fade_time;
	real alpha_modifier;
	struct widget_instance *previous;
	struct widget_instance *next;
	struct widget_instance *parent;
	struct widget_instance *child;
	struct widget_instance *focused_child;
	union
	{
		struct
		{
			wchar_t *text;
			short string_list_index;
		} text_box;
		struct
		{
			short selected_index;
			short last_list_tab_direction;
			void *list_items;
			word number_of_items;
			struct widget_instance *extended_description;
			wchar_t *item_text;
		} list;
	} parameters;
	struct
	{
		short current_frame_index;
		short first_frame_index;
		short last_frame_index;
		short number_of_sprite_frames;
	} animation;
};
#endif

/* (the platform's) */
char const *config_string(char const *name);
char const *build_identity_version(void);
void platform_log(char const *format, ...);
long halo_screen_width(void);

/* ---------- constants */

enum
{
	_screen_none,
	_screen_main_menu,
	_screen_map_select,
	_screen_gametype_select,
};

enum
{
	/* ui_widget.c's widget types */
	WIDGET_TYPE_TEXT_BOX = 1,
	WIDGET_TYPE_SPINNER_LIST = 2,

	/* the pickers, in the menus' 640x480: the Xbox's band (its cards and
	arrows) covered, a list on the left and the details on the right */
	COVER_X = 44, COVER_Y = 72, COVER_WIDTH = 552, COVER_HEIGHT = 336,
	LIST_X = 46, LIST_Y = 80, LIST_WIDTH = 244, LIST_HEAD = 18, LIST_ROW = 22, LIST_ROWS = 13,
	DETAIL_X = 294, DETAIL_Y = 80, DETAIL_WIDTH = 302, DETAIL_HEIGHT = 320,
	PICTURE_X = 306, PICTURE_Y = 90, PICTURE_WIDTH = 180, PICTURE_HEIGHT = 149,
	STATS_X = 500,

	MAXIMUM_ROWS = 512,
	MAXIMUM_GAMETYPES = 256,
	/* the game list's games, for the main menu's foot (asked this often) */
	ONLINE_INTERVAL = 2000,
};

/* the browser's (browser_screen.c), over the Xbox's band */
enum
{
	COLOR_COVER_TOP = 0x0B1F3FF2,
	COLOR_COVER_BOTTOM = 0x07142BF2,
	COLOR_PANEL = 0x0A1B36FF,
	COLOR_PANEL_EDGE = 0x2C5C9EFF,
	COLOR_HEAD_TOP = 0x123266FF,
	COLOR_HEAD_BOTTOM = 0x0C2347FF,
	COLOR_ROW_RULE = 0x16294AFF,
	COLOR_ROW_FOCUS = 0x1E4F8FCC,
	COLOR_ROW_FOCUS_EDGE = 0x62B4FFFF,
	COLOR_TEXT = 0xE6EEFCFF,
	COLOR_DIM = 0x8FA6C8FF,
	COLOR_LABEL = 0x4AA3FFFF,
	COLOR_HEAD = 0x6F9BD6FF,
	COLOR_PROMPT = 0x4AA3FFFF,
	COLOR_PC = 0xF2C14EFF,
	COLOR_PC_FILL = 0xF2C14E1F,
	COLOR_PC_EDGE = 0xF2C14E99,
	COLOR_BAR_TOP = 0x081830D8,
	COLOR_BAR_BOTTOM = 0x030A16E8,
	COLOR_RULE = 0x2C5C9EFF,
};

/* ---------- globals */

static struct
{
	short chosen;
	short screen;
	char const *screen_tag;
	/* the picker's list (a spinner list of the Xbox's three cards) */
	struct widget_instance *list;
	/* the list's first row drawn, and whether the list was drawn (for the
	pointer) */
	short first_row;
	boolean list_drawn;
	/* rows the pointer asked to move the focus by, a press a frame */
	short pending_steps;
	/* the gametypes of the open picker, read when it opened */
	void *gametype_items;
	short gametype_count;
	struct gametype_entry
	{
		boolean valid;
		struct game_variant variant;
	} gametypes[MAXIMUM_GAMETYPES];
	/* the game list's games and players, for the main menu */
	unsigned long online_time;
	int online_games, online_players;
} menu_style = { -1 };

static struct browser_game online_list[BROWSER_MAXIMUM_GAMES];

/* ---------- text */

/* UTF-16 text up to its end (or a line's: stop_at_line) as UTF-8 */
static char const *utf8_from(
	wchar_t const *text,
	char *out,
	long size)
{
	long used = 0;

	out[0] = 0;
	if (!text)
		return out;
	for (; *text && used < size - 4; text++)
	{
		unsigned int character = (unsigned short)*text;

		if (character == '\r')
			continue;
		if (character < 0x80)
			out[used++] = (char)character;
		else if (character < 0x800)
		{
			out[used++] = (char)(0xC0 | (character >> 6));
			out[used++] = (char)(0x80 | (character & 0x3F));
		}
		else
		{
			out[used++] = (char)(0xE0 | (character >> 12));
			out[used++] = (char)(0x80 | ((character >> 6) & 0x3F));
			out[used++] = (char)(0x80 | (character & 0x3F));
		}
	}
	out[used] = 0;
	return out;
}

/* the line'th line of text (lines end with \n), into line; FALSE past the
last */
static boolean text_line(
	char const *text,
	long number,
	char *line,
	long size)
{
	char const *end;

	while (number-- > 0)
	{
		text = strchr(text, '\n');
		if (!text)
			return FALSE;
		text++;
	}
	end = strchr(text, '\n');
	if (!end)
		end = text + strlen(text);
	if ((long)(end - text) >= size)
		end = text + size - 1;
	memcpy(line, text, (size_t)(end - text));
	line[end - text] = 0;
	return TRUE;
}

static void uppercase(
	char *text)
{
	for (; *text; text++)
	{
		if (*text >= 'a' && *text <= 'z')
			*text = (char)(*text - 32);
	}
}

/* ---------- the setting */

boolean menu_style_chupathingy(
	void)
{
	if (menu_style.chosen < 0)
	{
		char const *value = config_string("display.menus");
		char lower[16];
		size_t index;

		for (index = 0; index + 1 < sizeof(lower) && value[index]; index++)
			lower[index] = (char)(value[index] >= 'A' && value[index] <= 'Z' ? value[index] + 32 : value[index]);
		lower[index] = 0;
		menu_style.chosen = !strcmp(lower, "chupathingy");
		if (menu_style.chosen)
			platform_log("menus: the Chupathingy style (a prototype: the main menu, map and gametype pickers)");
	}
	return menu_style.chosen != 0;
}

/* ---------- the screens */

static char const *widget_tag_name(
	struct widget_instance const *widget)
{
	char const *name = widget ? tag_get_name(widget->definition_tag_index) : NULL;

	return name ? name : "";
}

static boolean name_ends(
	char const *name,
	char const *end)
{
	size_t length = strlen(name), end_length = strlen(end);

	return length >= end_length && !strcmp(name + length - end_length, end);
}

/* the first spinner list of three cards under widget, depth first */
static struct widget_instance *find_card_list(
	struct widget_instance *widget)
{
	struct widget_instance *child;

	for (; widget; widget = widget->next)
	{
		if (widget->type == WIDGET_TYPE_SPINNER_LIST && widget->parameters.list.number_of_items)
			return widget;
		for (child = widget->child; child; child = child->next)
		{
			struct widget_instance *found = find_card_list(child);

			if (found)
				return found;
		}
	}
	return NULL;
}

/* the screen the menus show, by its widget's tag */
static short screen_of(
	struct widget_instance *root,
	struct widget_instance **list)
{
	char const *name = widget_tag_name(root);

	*list = NULL;
	if (!root)
		return _screen_none;
	if (!strcmp(name, "ui\\shell\\main_menu\\main_menu"))
		return _screen_main_menu;
	if (strstr(name, "mp_map_select") || strstr(name, "map_select"))
	{
		*list = root->child ? find_card_list(root->child) : NULL;
		return *list ? _screen_map_select : _screen_none;
	}
	if (strstr(name, "game_select") || strstr(name, "gametype_select") || strstr(name, "settings_select\\multiplayer_setup\\") ||
		name_ends(name, "playlist_select"))
	{
		*list = root->child ? find_card_list(root->child) : NULL;
		return *list ? _screen_gametype_select : _screen_none;
	}
	return _screen_none;
}

static void screen_update(
	struct widget_instance *root)
{
	struct widget_instance *list;
	short screen = screen_of(root, &list);
	char const *name = widget_tag_name(root);

	if (name != menu_style.screen_tag)
	{
		/* (each screen once, while the style is a prototype) */
		platform_log("menus: chupathingy: %s (%s)", name,
			screen == _screen_main_menu ? "main menu" : screen == _screen_map_select ? "map picker" :
			screen == _screen_gametype_select ? "gametype picker" : "Xbox");
		menu_style.screen_tag = name;
	}
	if (list != menu_style.list)
	{
		menu_style.pending_steps = 0;
		menu_style.gametype_items = NULL;
	}
	menu_style.screen = screen;
	menu_style.list = list;
}

/* ---------- drawing helpers */

static float margin_width(
	void)
{
	return (float)((halo_screen_width() - 640) / 2 + 2);
}

static void panel(
	float x,
	float y,
	float width,
	float height)
{
	ui_overlay_rect(x, y, width, height, 6, COLOR_PANEL);
	ui_overlay_outline(x, y, width, height, 6, 1.0f, COLOR_PANEL_EDGE);
}

/* a bitmap's picture (or the box alone), drawn by the game in a cut-out of
the overlay */
static void picture(
	struct bitmap_data *bitmap,
	short art_width,
	short art_height,
	short x,
	short y,
	short width,
	short height)
{
	rectangle2d bounds;

	ui_overlay_cutout(x, y, width, height);
	bounds.x0 = x;
	bounds.y0 = y;
	bounds.x1 = (short)(x + width);
	bounds.y1 = (short)(y + height);
	draw_quad(&bounds, 0xFF0A1A33);
	if (bitmap)
	{
		rectangle2d art;

		art.x0 = 0;
		art.y0 = 0;
		art.x1 = (short)MIN(art_width, bitmap->width);
		art.y1 = (short)MIN(art_height, bitmap->height);
		draw_bitmap_in_rect(bitmap, &bounds, &art, NULL, 0xFFFFFFFF, NULL, FALSE);
	}
	ui_overlay_outline(x - 1, y - 1, width + 2, height + 2, 0, 1.0f, COLOR_ROW_RULE);
}

static struct bitmap_data *tag_picture(
	char const *tag,
	short frame)
{
	long index = tag_loaded('bitm', tag);

	return index != NONE && frame >= 0 ? bitmap_group_get_bitmap_from_sequence(index, 0, frame) : NULL;
}

/* a badge (HALO PC, HALOMD) at x, centred on text of text_size at text_y;
its width */
static float badge(
	char const *text,
	float size,
	float x,
	float text_y,
	float text_size)
{
	float padding = size * 0.5f;
	float height = size + padding;
	float width = ui_overlay_text_width(UI_FONT_BOLD, size, text) + size;
	float y = text_y + text_size * 0.5f - height * 0.5f;

	ui_overlay_rect(x, y, width, height, 2, COLOR_PC_FILL);
	ui_overlay_outline(x, y, width, height, 2, 0.75f, COLOR_PC_EDGE);
	ui_overlay_text(UI_FONT_BOLD, size, x + padding, y + padding * 0.55f, UI_ALIGN_LEFT, COLOR_PC, text);
	return width;
}

/* a label over a value, in the details' column */
static float stat(
	float y,
	char const *label,
	char const *value)
{
	ui_overlay_text(UI_FONT_BOLD, 7.5f, STATS_X, y, UI_ALIGN_LEFT, COLOR_LABEL, label);
	ui_overlay_text(UI_FONT_BOLD, 11.0f, STATS_X, y + 10, UI_ALIGN_LEFT, COLOR_TEXT, value);
	return y + 32;
}

/* a rule's label and value on a line of the details; the next line's y */
static float rule_line(
	float x,
	float y,
	char const *label,
	char const *value)
{
	float width = ui_overlay_text(UI_FONT_REGULAR, 9.0f, x, y, UI_ALIGN_LEFT, COLOR_LABEL, label);

	ui_overlay_text(UI_FONT_BOLD, 9.0f, x + width + 4, y, UI_ALIGN_LEFT, COLOR_TEXT, value);
	return y + 13;
}

/* the list's frame: the panel, its head, and the rows' rules; the first row
drawn (the focused row's page) */
static short list_frame(
	char const *left_head,
	char const *right_head,
	short count,
	short focused)
{
	short row, first = (short)(focused - focused % LIST_ROWS);
	char text[48];

	panel(LIST_X, LIST_Y, LIST_WIDTH, DETAIL_HEIGHT);
	ui_overlay_gradient(LIST_X, LIST_Y, LIST_WIDTH, LIST_HEAD, 6, COLOR_HEAD_TOP, COLOR_HEAD_BOTTOM);
	ui_overlay_text(UI_FONT_BOLD, 8.0f, LIST_X + 12, LIST_Y + 5, UI_ALIGN_LEFT, COLOR_HEAD, left_head);
	if (count > LIST_ROWS)
	{
		snprintf(text, sizeof(text), "%s  \xC2\xB7  %d OF %d", right_head, focused + 1, count);
		ui_overlay_text(UI_FONT_BOLD, 8.0f, LIST_X + LIST_WIDTH - 10, LIST_Y + 5, UI_ALIGN_RIGHT, COLOR_HEAD, text);
	}
	else
		ui_overlay_text(UI_FONT_BOLD, 8.0f, LIST_X + LIST_WIDTH - 10, LIST_Y + 5, UI_ALIGN_RIGHT, COLOR_HEAD, right_head);
	for (row = 1; row < LIST_ROWS; row++)
		ui_overlay_rect(LIST_X + 1, LIST_Y + LIST_HEAD + row * LIST_ROW, LIST_WIDTH - 2, 0.5f, 0, COLOR_ROW_RULE);
	if (focused >= first && focused < first + LIST_ROWS)
	{
		float y = (float)(LIST_Y + LIST_HEAD + (focused - first) * LIST_ROW);

		/* (as the Xbox's focused card: a light outline) */
		ui_overlay_rect(LIST_X + 3, y + 1, LIST_WIDTH - 6, LIST_ROW - 2, 4, COLOR_ROW_FOCUS);
		ui_overlay_outline(LIST_X + 3, y + 1, LIST_WIDTH - 6, LIST_ROW - 2, 4, 1.25f, COLOR_ROW_FOCUS_EDGE);
	}
	menu_style.first_row = first;
	menu_style.list_drawn = TRUE;
	return first;
}

/* the Xbox's band under the pickers, covered */
static void cover(
	void)
{
	/* (the panels hide the cards; between them, the Xbox's band) */
	ui_overlay_rect(LIST_X + LIST_WIDTH, LIST_Y + 8, DETAIL_X - LIST_X - LIST_WIDTH, DETAIL_HEIGHT - 16, 0, 0x0A1B36E6);
}

/* ---------- the main menu */

static char const *const main_menu_descriptions[] =
{
	"Fight through the Halo campaign, alone or with a friend.",
	"Split screen, system link and online games.",
	"Profiles, controls, video, audio and network.",
};

/* the focused item of the main menu's list (its place among the items), or
NONE */
static short focused_index(
	struct widget_instance *root)
{
	struct widget_instance *widget = root, *child;

	while (widget && widget->focused_child)
		widget = widget->focused_child;
	if (!widget || widget == root || !widget->parent)
		return NONE;
	{
		short index = 0;

		for (child = widget->parent->child; child && child != widget; child = child->next)
			index++;
		return child ? index : NONE;
	}
}

static void online_update(
	void)
{
	unsigned long now = system_milliseconds();

	if (menu_style.online_time && now - menu_style.online_time < ONLINE_INTERVAL)
		return;
	menu_style.online_time = now ? now : 1;
	{
		int count = browser_get_games(online_list, BROWSER_MAXIMUM_GAMES), index;

		menu_style.online_games = count;
		menu_style.online_players = 0;
		for (index = 0; index < count; index++)
			menu_style.online_players += online_list[index].players;
	}
}

static void render_main_menu(
	struct widget_instance *root)
{
	short focused = focused_index(root);
	float margin = margin_width(), x;
	char text[96];

	online_update();
	/* what the focused item opens, under the items */
	if (focused >= 0 && focused < (short)NUMBEROF(main_menu_descriptions))
	{
		ui_overlay_text(UI_FONT_REGULAR, 11.0f, 320, 359, UI_ALIGN_CENTER, COLOR_TEXT,
			main_menu_descriptions[focused]);
	}

	/* the foot: a bar as the Xbox's screens have (this version, the game
	list's games, the A prompt) */
	ui_overlay_gradient(-margin, 436, 640 + 2 * margin, 44, 0, COLOR_BAR_TOP, COLOR_BAR_BOTTOM);
	ui_overlay_rect(-margin, 436, 640 + 2 * margin, 0.75f, 0, COLOR_RULE);
	x = 36 + ui_overlay_text(UI_FONT_BOLD, 9.0f, 36, 451.5f, UI_ALIGN_LEFT, COLOR_LABEL, "CHUPATHINGY") + 5;
	ui_overlay_text(UI_FONT_REGULAR, 9.0f, x, 451.5f, UI_ALIGN_LEFT, COLOR_DIM, build_identity_version());
	x = 604;
	snprintf(text, sizeof(text), "%d", menu_style.online_players);
	x -= ui_overlay_text(UI_FONT_BOLD, 9.0f, x, 451.5f, UI_ALIGN_RIGHT, COLOR_TEXT, text) + 4;
	x -= ui_overlay_text(UI_FONT_REGULAR, 9.0f, x, 451.5f, UI_ALIGN_RIGHT, COLOR_DIM, "PLAYERS") + 12;
	snprintf(text, sizeof(text), "%d", menu_style.online_games);
	x -= ui_overlay_text(UI_FONT_BOLD, 9.0f, x, 451.5f, UI_ALIGN_RIGHT, COLOR_TEXT, text) + 4;
	x -= ui_overlay_text(UI_FONT_REGULAR, 9.0f, x, 451.5f, UI_ALIGN_RIGHT, COLOR_DIM, "GAMES") + 12;
	ui_overlay_text(UI_FONT_BOLD, 9.0f, x, 451.5f, UI_ALIGN_RIGHT, COLOR_LABEL, "ONLINE NOW");
	x = 320 - (ui_overlay_button_width(UI_BUTTON_A, 15.0f) + 3 + ui_overlay_text_width(UI_FONT_BOLD, 12.0f, "=SELECT")) / 2;
	x += ui_overlay_button(UI_BUTTON_A, 15.0f, x, 449, 0xFFFFFFFF) + 3;
	ui_overlay_text(UI_FONT_BOLD, 12.0f, x, 450.5f, UI_ALIGN_LEFT, COLOR_PROMPT, "=SELECT");
}

/* ---------- the map picker */

/* a row's map: its name, description (lines), file and family */
struct map_row
{
	char name[64];
	char description[256];
	char file[64];
	short family;
	short picture_frame;
};

static void map_row_get(
	long row,
	struct map_row *map)
{
	wchar_t const *text;
	char buffer[256];

	memset(map, 0, sizeof(*map));
	map->picture_frame = NONE;
#ifdef HALO_CUSTOM_EDITION
	{
		short name_index = ui_map_list_string_index(row, _ui_map_list_string_lobby_name);
		short description_index = ui_map_list_string_index(row, _ui_map_list_string_description);
		char **names = ui_map_list_names();

		text = ui_map_list_text(name_index);
		if (!text)
			text = unicode_string_list_get_string(tag_loaded('ustr', "ui\\shell\\main_menu\\mp_map_list"), name_index);
		text_line(utf8_from(text, buffer, sizeof(buffer)), 0, map->name, sizeof(map->name));
		text = ui_map_list_text(description_index);
		if (!text)
		{
			text = unicode_string_list_get_string(
				tag_loaded('ustr', "ui\\shell\\main_menu\\multiplayer_type_select\\mp_map_select\\map_data"),
				description_index);
		}
		utf8_from(text, map->description, sizeof(map->description));
		map->picture_frame = ui_map_list_picture_index(row);
		if (names && names[row])
			map->family = map_family_parse(names[row], map->file, sizeof(map->file));
	}
#else
	text = unicode_string_list_get_string(tag_loaded('ustr', "ui\\shell\\main_menu\\mp_map_list"), (short)row);
	text_line(utf8_from(text, buffer, sizeof(buffer)), 0, map->name, sizeof(map->name));
	text = unicode_string_list_get_string(
		tag_loaded('ustr', "ui\\shell\\main_menu\\multiplayer_type_select\\mp_map_select\\map_data"), (short)row);
	utf8_from(text, map->description, sizeof(map->description));
	map->picture_frame = (short)row;
#endif
	if (!map->name[0])
		snprintf(map->name, sizeof(map->name), "%s", map->file[0] ? map->file : "Unknown map");
}

/* the description's player count ("4-16 players"), its tagline (the lines
before the first blank one, joined) and whether it says vehicles */
static void map_description_parts(
	char const *description,
	char *players,
	long players_size,
	char *tagline,
	long tagline_size,
	boolean *vehicles)
{
	char line[128];
	long number;
	boolean in_tagline = TRUE;

	players[0] = 0;
	tagline[0] = 0;
	*vehicles = FALSE;
	for (number = 0; text_line(description, number, line, sizeof(line)); number++)
	{
		if (!line[0])
		{
			in_tagline = FALSE;
			continue;
		}
		if (strstr(line, "players") || strstr(line, "Players"))
		{
			char *space = strchr(line, ' ');

			if (space)
				*space = 0;
			snprintf(players, (size_t)players_size, "%s", line);
		}
		else if (strstr(line, "ehicle"))
			*vehicles = TRUE;
		else if (in_tagline)
		{
			size_t used = strlen(tagline);

			/* (a word broken over lines with a hyphen joins again) */
			if (used && tagline[used - 1] == '-')
				snprintf(tagline + used, (size_t)(tagline_size - used), "%s", line);
			else
				snprintf(tagline + used, (size_t)(tagline_size - used), "%s%s", used ? " " : "", line);
		}
	}
}

/* text in lines that fit the detail panel, from y down, at most
maximum_lines of them; the y below the last */
static float paragraph(
	char const *text,
	float size,
	float line_height,
	float y,
	long maximum_lines)
{
	char line[128];
	char const *word = text;
	long used = 0, lines = 0;

	line[0] = 0;
	while (*word && lines < maximum_lines)
	{
		char const *end = strchr(word, ' ');
		long length = end ? (long)(end - word) : (long)strlen(word);
		char candidate[128];

		snprintf(candidate, sizeof(candidate), "%s%s%.*s", line, used ? " " : "", (int)length, word);
		if (used && ui_overlay_text_width(UI_FONT_REGULAR, size, candidate) > DETAIL_WIDTH - 24)
		{
			ui_overlay_text(UI_FONT_REGULAR, size, PICTURE_X, y, UI_ALIGN_LEFT, COLOR_TEXT, line);
			y += line_height;
			lines++;
			snprintf(line, sizeof(line), "%.*s", (int)length, word);
		}
		else
			snprintf(line, sizeof(line), "%s", candidate);
		used = (long)strlen(line);
		word += length;
		while (*word == ' ')
			word++;
	}
	if (used && lines < maximum_lines)
	{
		ui_overlay_text(UI_FONT_REGULAR, size, PICTURE_X, y, UI_ALIGN_LEFT, COLOR_TEXT, line);
		y += line_height;
	}
	return y;
}

/* lines of text (ending with \n) joined into one paragraph, a word broken
over lines with a hyphen whole again */
static void joined_lines(
	char const *text,
	char *joined,
	long size)
{
	char line[128];
	long number;

	joined[0] = 0;
	for (number = 0; text_line(text, number, line, sizeof(line)); number++)
	{
		size_t used = strlen(joined);

		if (!line[0])
			continue;
		if (used && joined[used - 1] == '-')
			snprintf(joined + used, (size_t)(size - used), "%s", line);
		else
			snprintf(joined + used, (size_t)(size - used), "%s%s", used ? " " : "", line);
	}
}

static struct bitmap_data *map_picture(
	short frame)
{
#ifdef HALO_CUSTOM_EDITION
	struct bitmap_data *bitmap = ui_map_list_picture(frame);

	if (bitmap)
		return bitmap;
#endif
	return tag_picture("ui\\shell\\bitmaps\\mp_map_grafix", frame);
}

static void render_map_select(
	void)
{
	struct widget_instance *list = menu_style.list;
	short count = (short)list->parameters.list.number_of_items;
	short focused = list->parameters.list.selected_index;
	short first, row;
	struct map_row map;
	char players[32], tagline[200], text[96];
	boolean vehicles;
	float y;

	cover();
	first = list_frame("MAP", "PLAYERS", count, focused);
	for (row = 0; row < LIST_ROWS && first + row < count; row++)
	{
		float row_y = (float)(LIST_Y + LIST_HEAD + row * LIST_ROW);
		unsigned int color = first + row == focused ? 0xFFFFFFFF : COLOR_TEXT;
		float name_width;

		map_row_get(first + row, &map);
		map_description_parts(map.description, players, sizeof(players), tagline, sizeof(tagline), &vehicles);
		name_width = ui_overlay_text(UI_FONT_BOLD, 10.0f, LIST_X + 12, row_y + 5, UI_ALIGN_LEFT, color, map.name);
		if (map.family != _map_family_xbox)
			badge(map_family_badge(map.family), 6.0f, LIST_X + 18 + name_width, row_y + 5, 10.0f);
		ui_overlay_text(UI_FONT_REGULAR, 9.0f, LIST_X + LIST_WIDTH - 12, row_y + 5.5f, UI_ALIGN_RIGHT,
			first + row == focused ? COLOR_TEXT : COLOR_DIM, players[0] ? players : "\xE2\x80\x93");
	}

	/* the focused map */
	panel(DETAIL_X, DETAIL_Y, DETAIL_WIDTH, DETAIL_HEIGHT);
	map_row_get(focused, &map);
	map_description_parts(map.description, players, sizeof(players), tagline, sizeof(tagline), &vehicles);
	picture(map_picture(map.picture_frame), 140, 116, PICTURE_X, PICTURE_Y, PICTURE_WIDTH, PICTURE_HEIGHT);
	y = PICTURE_Y + 2;
	snprintf(text, sizeof(text), "%s", players[0] ? players : "Any");
	y = stat(y, "PLAYERS", text);
	y = stat(y, "VEHICLES", vehicles ? "Yes" : "No");
	y = stat(y, "FROM", map.family == _map_family_xbox ? "Xbox" : map.family == _map_family_halomd ? "HaloMD" : "Halo PC");
	snprintf(text, sizeof(text), "%d of %d", focused + 1, count);
	stat(y, "MAP", text);

	y = PICTURE_Y + PICTURE_HEIGHT + 12;
	ui_overlay_text(UI_FONT_BOLD, 17.0f, PICTURE_X, y, UI_ALIGN_LEFT, 0xFFFFFFFF, map.name);
	y += 24;
	paragraph(tagline, 11.0f, 15.0f, y, 6);
	ui_overlay_rect(PICTURE_X, DETAIL_Y + DETAIL_HEIGHT - 30, DETAIL_WIDTH - 20, 0.75f, 0, COLOR_ROW_RULE);
	snprintf(text, sizeof(text), "%s%s", map.file[0] ? "File: " : "", map.file);
	ui_overlay_text(UI_FONT_REGULAR, 8.5f, PICTURE_X, DETAIL_Y + DETAIL_HEIGHT - 21, UI_ALIGN_LEFT, COLOR_DIM, text);
	ui_overlay_text(UI_FONT_REGULAR, 8.5f, DETAIL_X + DETAIL_WIDTH - 10, DETAIL_Y + DETAIL_HEIGHT - 21,
		UI_ALIGN_RIGHT, COLOR_DIM, "Next: pick a gametype");
}

/* ---------- the gametype picker */

static char const *const engine_names[] = { "Game", "CTF", "Slayer", "Oddball", "King of the Hill", "Race" };
/* game_type_grafix's frames for the engines */
static short const engine_frames[] = { 5, 0, 2, 3, 1, 4 };
static char const *const weapon_set_names[] =
{
	"Normal", "Pistols", "Rifles", "Plasma", "Snipers", "No sniping", "Rockets", "Shotguns", "Short range",
	"Human", "No grenades", "Covenant", "Classic", "Heavy",
};

static void gametypes_read(
	void)
{
	struct widget_instance *list = menu_style.list;
	short index;

	if (menu_style.gametype_items == list->parameters.list.list_items &&
		menu_style.gametype_count == (short)list->parameters.list.number_of_items)
	{
		return;
	}
	menu_style.gametype_items = list->parameters.list.list_items;
	menu_style.gametype_count = (short)MIN(list->parameters.list.number_of_items, MAXIMUM_GAMETYPES);
	for (index = 0; index < menu_style.gametype_count; index++)
	{
		long profile = ((long *)list->parameters.list.list_items)[index];

		memset(&menu_style.gametypes[index].variant, 0, sizeof(menu_style.gametypes[index].variant));
		menu_style.gametypes[index].valid = profile != NONE &&
			playlist_profile_get(profile, &menu_style.gametypes[index].variant);
	}
}

static short engine_of(
	struct game_variant const *variant)
{
	return variant->game_engine_index > 0 && variant->game_engine_index < (long)NUMBEROF(engine_names) ?
		(short)variant->game_engine_index : 0;
}

static char const *gametype_kind(
	struct game_variant const *variant,
	char *text,
	long size)
{
	short engine = engine_of(variant);

	snprintf(text, (size_t)size, "%s%s", variant->universal_variant.teams && engine != game_engine_ctf ? "Team " : "",
		engine_names[engine]);
	return text;
}

/* the focused card's description (its third child's text), as the Xbox
shows it */
static void focused_card_description(
	struct widget_instance *list,
	char *text,
	long size)
{
	struct widget_instance *card = list->focused_child;
	struct widget_instance *child;
	short index = 0;

	text[0] = 0;
	for (child = card ? card->child : NULL; child; child = child->next, index++)
	{
		if (index == 2 && child->type == WIDGET_TYPE_TEXT_BOX)
			utf8_from(child->parameters.text_box.text, text, size);
	}
}

static void render_gametype_select(
	void)
{
	struct widget_instance *list = menu_style.list;
	short focused = list->parameters.list.selected_index;
	short first, row, engine;
	struct game_variant const *variant;
	char name[64], text[256], line[256];
	float y, x;

	gametypes_read();
	cover();
	first = list_frame("GAMETYPE", "RULES", menu_style.gametype_count, focused);
	for (row = 0; row < LIST_ROWS && first + row < menu_style.gametype_count; row++)
	{
		float row_y = (float)(LIST_Y + LIST_HEAD + row * LIST_ROW);
		unsigned int color = first + row == focused ? 0xFFFFFFFF : COLOR_TEXT;

		if (!menu_style.gametypes[first + row].valid)
			continue;
		variant = &menu_style.gametypes[first + row].variant;
		utf8_from(variant->human_readable_game_description, name, sizeof(name));
		ui_overlay_text(UI_FONT_BOLD, 10.0f, LIST_X + 12, row_y + 5, UI_ALIGN_LEFT, color, name);
		ui_overlay_text(UI_FONT_REGULAR, 9.0f, LIST_X + LIST_WIDTH - 12, row_y + 5.5f, UI_ALIGN_RIGHT,
			first + row == focused ? COLOR_TEXT : COLOR_DIM, gametype_kind(variant, text, sizeof(text)));
	}

	panel(DETAIL_X, DETAIL_Y, DETAIL_WIDTH, DETAIL_HEIGHT);
	if (focused < 0 || focused >= menu_style.gametype_count || !menu_style.gametypes[focused].valid)
		return;
	variant = &menu_style.gametypes[focused].variant;
	engine = engine_of(variant);
	picture(tag_picture("ui\\shell\\bitmaps\\game_type_grafix", engine_frames[engine]), 140, 114,
		PICTURE_X, PICTURE_Y, PICTURE_WIDTH, PICTURE_HEIGHT);
	y = PICTURE_Y + 2;
	y = stat(y, "GAME", gametype_kind(variant, text, sizeof(text)));
	if (variant->universal_variant.score_to_win > 0)
		snprintf(text, sizeof(text), "%ld", (long)variant->universal_variant.score_to_win);
	else
		snprintf(text, sizeof(text), "None");
	y = stat(y, "SCORE TO WIN", text);
	if (variant->universal_variant.lives > 0)
		snprintf(text, sizeof(text), "%ld", (long)variant->universal_variant.lives);
	else
		snprintf(text, sizeof(text), "Unlimited");
	y = stat(y, "LIVES", text);
	snprintf(text, sizeof(text), "%d of %d", focused + 1, menu_style.gametype_count);
	stat(y, "GAMETYPE", text);

	y = PICTURE_Y + PICTURE_HEIGHT + 12;
	utf8_from(variant->human_readable_game_description, name, sizeof(name));
	ui_overlay_text(UI_FONT_BOLD, 17.0f, PICTURE_X, y, UI_ALIGN_LEFT, 0xFFFFFFFF, name);
	y += 24;
	/* (the Xbox's description of it, its card's lines joined and fitted to
	the panel) */
	focused_card_description(list, text, sizeof(text));
	joined_lines(text, line, sizeof(line));
	paragraph(line, 10.0f, 13.0f, y, 5);
	/* the rules in two columns, at the panel's foot */
	y = DETAIL_Y + DETAIL_HEIGHT - 50;
	ui_overlay_rect(PICTURE_X, y - 6, DETAIL_WIDTH - 20, 0.75f, 0, COLOR_ROW_RULE);
	x = PICTURE_X;
	rule_line(x, y, "Weapons:", variant->universal_variant.weapon_set >= 0 &&
		variant->universal_variant.weapon_set < (long)NUMBEROF(weapon_set_names) ?
		weapon_set_names[variant->universal_variant.weapon_set] : "Custom");
	rule_line(x, y + 13, "Shields:", TEST_FLAG(variant->universal_variant.flags, _game_variant_no_shields_bit) ? "Off" : "On");
	snprintf(text, sizeof(text), "%lds", (long)variant->universal_variant.respawn_time / 30);
	rule_line(x, y + 26, "Respawn:", variant->universal_variant.respawn_time ? text : "Instant");
	x = PICTURE_X + 140;
	rule_line(x, y, "Radar:", TEST_FLAG(variant->universal_variant.flags, _game_variant_draw_object_in_motion_sensor_bit) ?
		"Players shown" : "Off");
	rule_line(x, y + 13, "Grenades:", TEST_FLAG(variant->universal_variant.flags, _game_variant_infinite_grenades_bit) ?
		"Infinite" : "Normal");
	rule_line(x, y + 26, "Camo:", TEST_FLAG(variant->universal_variant.flags, _game_variant_always_invisible_bit) ?
		"Always" : "Pickups");
}

/* ---------- public code */

/* each frame the menus draw, after the widgets (ui_widget.c), the first
player's screen */
void menu_style_render(
	struct widget_instance *root)
{
	menu_style.list_drawn = FALSE;
	if (!menu_style_chupathingy() || !ui_overlay_available())
		return;
	screen_update(root);
	/* (the pointer's rows asked for, a press a frame) */
	if (menu_style.pending_steps && menu_style.list)
	{
		event_manager_post_button(0, menu_style.pending_steps > 0 ? _gamepad_binary_button_dpad_right :
			_gamepad_binary_button_dpad_left);
		menu_style.pending_steps += menu_style.pending_steps > 0 ? -1 : 1;
	}
	switch (menu_style.screen)
	{
	case _screen_main_menu:
		render_main_menu(root);
		break;
	case _screen_map_select:
		render_map_select();
		break;
	case _screen_gametype_select:
		render_gametype_select();
		break;
	}
}

/* a widget's event, before it has it (ui_widget.c): on a picker drawn as a
list, up and down step through it as the cards' left and right. The d-pad
direction it was (its repeat timed as that one's), or NONE */
short menu_style_event(
	struct widget_instance *widget,
	struct event_record *event)
{
	if (!menu_style.chosen || !widget || widget != menu_style.list ||
		(menu_style.screen != _screen_map_select && menu_style.screen != _screen_gametype_select))
	{
		return NONE;
	}
	if (event->type == 3 /* a button */)
	{
		short direction = event->data.button.index;

		if (direction == _gamepad_binary_button_dpad_up)
			event->data.button.index = _gamepad_binary_button_dpad_left;
		else if (direction == _gamepad_binary_button_dpad_down)
			event->data.button.index = _gamepad_binary_button_dpad_right;
		else
			return NONE;
		return direction;
	}
	if (event->type == 1 /* the left stick */ && event->data.stick.x == 0 &&
		(event->data.stick.y == SHORT_MAX || event->data.stick.y == SHORT_MIN))
	{
		event->data.stick.x = event->data.stick.y == SHORT_MAX ? SHORT_MIN : SHORT_MAX;
		event->data.stick.y = 0;
	}
	return NONE;
}

/* the menus' pointer, each frame (ui_widget.c): TRUE if it is over a list
this file drew, which takes it (the Xbox's cards under it do not): a click
on a row moves the focus there, a click on the focused row is A, the wheel
steps */
boolean menu_style_pointer(
	struct halo_ui_pointer const *pointer)
{
	short count, focused;

	if (!menu_style.chosen || !menu_style.list_drawn || !menu_style.list ||
		(menu_style.screen != _screen_map_select && menu_style.screen != _screen_gametype_select))
	{
		return FALSE;
	}
	if (pointer->x < COVER_X || pointer->x >= COVER_X + COVER_WIDTH ||
		pointer->y < COVER_Y || pointer->y >= COVER_Y + COVER_HEIGHT)
	{
		return FALSE;
	}
	count = (short)menu_style.list->parameters.list.number_of_items;
	focused = menu_style.list->parameters.list.selected_index;
	if (pointer->wheel_steps)
		menu_style.pending_steps = (short)(menu_style.pending_steps - pointer->wheel_steps);
	if (pointer->left_clicks &&
		pointer->click_x >= LIST_X && pointer->click_x < LIST_X + LIST_WIDTH &&
		pointer->click_y >= LIST_Y + LIST_HEAD && pointer->click_y < LIST_Y + LIST_HEAD + LIST_ROWS * LIST_ROW)
	{
		short row = (short)(menu_style.first_row + (pointer->click_y - LIST_Y - LIST_HEAD) / LIST_ROW);

		if (row == focused)
			event_manager_post_button(0, _gamepad_analog_button_a);
		else if (row < count)
			menu_style.pending_steps = (short)(row - focused);
	}
	return TRUE;
}

#endif
