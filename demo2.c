#include <string.h>
#include <curses.h>
#include <panel.h>
#include <locale.h>
#include "unicode.h"
#include <stdlib.h>
#include <ctype.h>

/*
 * This window is main application window. It is used for taking content
 * for shadow drawing.
 */
static WINDOW *mainwin = NULL;

typedef struct _ST_MENU
{
	char	*text;
	int		code;
	char	*shortcut;
	struct _ST_MENU *submenu;
} ST_MENU;

typedef struct
{
	int		c;
	int		code;
} ST_MENU_ACCELERATOR;

typedef struct
{
	bool	force8bit;
	bool	wide_vborders;			/* wide vertical menu borders like Turbo Vision */
	bool	wide_hborders;			/* wide horizontal menu borders like custom menu mc */
	bool	draw_box;				/* when true, then box is created */
	bool	left_alligned_shortcuts;	/* when true, a shortcuts are left alligned */
	bool	extra_inner_space;		/* when true, then there 2 spaces between text and border */
	int		shadow_width;			/* when shadow_width is higher than zero, shadow is visible */
	int		menu_background_cpn;	/* draw area color pair number */
	int		menu_background_attr;	/* draw area attributte */
	int		menu_shadow_cpn;		/* draw area color pair number */
	int		menu_shadow_attr;		/* draw area attributte */
	int		accelerator_cpn;		/* color pair of accelerators */
	int		accelerator_attr;		/* accelerator attributes */
	int		cursor_cpn;				/* cursor color pair */
	int		cursor_attr;			/* cursor attributte */
	int		cursor_accel_cpn;		/* color pair of accelerator on cursor row */
	int		cursor_accel_attr;		/* attributte of of accelerator on cursor row */
	int		disabled_cpn;			/* color of disabled menu fields */
	int		disabled_attr;			/* attributes of disabled menu fields */
	int		shortcut_space;			/* spaces between text and shortcut */
	int		text_space;				/* spaces between text fields (menubar), when it is -1, then dynamic spaces (FAND) */
	int		init_text_space;		/* initial space for menu bar */
	int		menu_bar_menu_offset;	/* offset between menu bar and menu */
	int		inner_space;			/* space between draw area and border, FAND uses 2 spaces */
	int		extern_accel_text_space;	/* space between external accelerator and menu item text */
} ST_MENU_CONFIG;

#define ST_MENU_STYLE_MCB			0
#define ST_MENU_STYLE_MC			1
#define ST_MENU_STYLE_VISION		2
#define ST_MENU_STYLE_DOS			3
#define ST_MENU_STYLE_FAND_1		4
#define ST_MENU_STYLE_FAND_2		5
#define ST_MENU_STYLE_FOXPRO		6
#define ST_MENU_STYLE_PERFECT		7
#define ST_MENU_STYLE_NOCOLOR		8
#define ST_MENU_STYLE_ONECOLOR		9
#define ST_MENU_STYLE_TURBO			10
#define ST_MENU_STYLE_PDMENU		11

typedef struct _ST_MENU_STATE
{
	ST_MENU	   *menu;
	WINDOW	   *draw_area;
	WINDOW	   *window;
	PANEL	   *panel;
	WINDOW	   *shadow_window;
	PANEL	   *shadow_panel;
	int			cursor_row;
	ST_MENU_ACCELERATOR		*accelerators;
	int			naccelerators;
	ST_MENU_CONFIG *config;
	int			help_x_pos;
	int			item_x_pos;
	int			nitems;									/* number of menu items */
	int		   *bar_fields_x_pos;						/* array of x positions of menubar fields */
	char	   *title;
	bool		press_accelerator;
	int			cursor_code;
	bool		is_menubar;
	bool		is_disabled;
	struct _ST_MENU_STATE	*active_submenu;
	struct _ST_MENU_STATE	**submenu_states;
} ST_MENU_STATE;

ST_MENU	   *selected_item = NULL;

static int menutext_displaywidth(ST_MENU_CONFIG *config, char *text, char **accelerator, bool *extern_accel);
static void pulldownmenu_content_size(ST_MENU_CONFIG *config, ST_MENU *menu,
										int *rows, int *columns, int *help_x_pos, int *item_x_pos,
										ST_MENU_ACCELERATOR *accelerators, int *naccelerators,
																			int *first_row, int *first_code);

static void menubar_draw(ST_MENU_STATE *menustate);
static void pulldownmenu_draw(ST_MENU_STATE *menustate);

void st_menu_load_style(ST_MENU_CONFIG *config, int style, int start_from_cpn);

void st_menu_post(ST_MENU_STATE *menustate);
void st_menu_unpost(ST_MENU_STATE *menustate);
void st_menu_driver(ST_MENU_STATE *menustate, int c, MEVENT *mevent);

ST_MENU_STATE *st_menu_new(ST_MENU_CONFIG *config, ST_MENU *menu, int begin_y, int begin_x, char *title);
ST_MENU_STATE *st_menu_new_menubar(ST_MENU_CONFIG *config, ST_MENU *menu);

/*
 * Generic functions
 */
static int
max_int(int a, int b)
{
	return a > b ? a : b;
}

/*
 * Returns display length of some text. ~ char is ignored.
 * ~~ is used as ~.
 */
static int
menutext_displaywidth(ST_MENU_CONFIG *config, char *text, char **accelerator, bool *extern_accel)
{
	int		result = 0;
	bool	_extern_accel = false;
	char   *_accelerator = NULL;
	bool	first_char = true;

	while (*text != '\0')
	{
		if (*text == '~' || (*text == '_' && first_char))
		{
			/*
			 * ~~ or __ disable effect of special chars ~ and _. ~x~ defines
			 * internal accelerator (inside menu item text). _x_ defines
			 * external accelerator (displayed before menu item text) _ has this
			 * effect only when it is first char of menu item text.
			 */
			if (text[1] == *text)
			{
				result += 1;
				text += 2;
			}
			else
			{
				if (*text == '_')
					_extern_accel = true;

				text += 1;
				if (_accelerator == NULL)
					_accelerator = text;

				/* eat second '_' */
				if (_extern_accel)
					text += 1;
			}

			first_char = false;
			continue;
		}

		if (config->force8bit)
		{
			result += 1;
			text += 1;
		}
		else
		{
			result += utf_dsplen(text);
			text += utf8charlen(*text);
		}

		first_char = false;
	}

	if (extern_accel)
		*extern_accel = _extern_accel;
	if (accelerator)
		*accelerator = _accelerator;

	return result;
}

/*
 * Collect display info about pulldown menu
 */
static void
pulldownmenu_content_size(ST_MENU_CONFIG *config, ST_MENU *menu,
								int *rows, int *columns, int *help_x_pos, int *item_x_pos,
								ST_MENU_ACCELERATOR *accelerators, int *naccelerators,
								int *first_row, int *first_code)
{
	char	*accelerator;
	bool	has_extern_accel = false;
	int	max_text_width = 0;
	int max_help_width = 0;
	int		naccel = 0;

	*rows = 0;
	*columns = 0;
	*help_x_pos = 0;
	*first_row = -1;
	*first_code = -1;

	*naccelerators = 0;

	while (menu->text != NULL)
	{
		bool	extern_accel;

		*rows += 1;
		if (*menu->text && strncmp(menu->text, "--", 2) != 0)
		{
			int text_width = 0;
			int help_width = 0;

			if (*first_row == -1)
				*first_row = *rows;
			if (*first_code == -1)
				*first_code = menu->code;

			text_width = menutext_displaywidth(config, menu->text, &accelerator, &extern_accel);

			if (extern_accel)
				has_extern_accel = true;

			if (accelerator != NULL)
			{
				accelerators[naccel].c = 
					config->force8bit ? tolower(*accelerator) : utf8_tofold(accelerator);
				accelerators[naccel++].code = menu->code;
			}

			if (menu->shortcut)
			{
				help_width = config->force8bit ? strlen(menu->shortcut) : utf_string_dsplen(menu->shortcut, SIZE_MAX);
			}

			/*
			 * left alligned shortcuts are used by MC style
			 */
			if (config->left_alligned_shortcuts)
			{
				max_text_width = max_int(max_text_width, 1 + text_width + 2);
				max_help_width = max_int(max_help_width, help_width);
			}
			else
				*columns = max_int(*columns,
											1 + text_width + 1
											  + (config->extra_inner_space ? 2 : 0)
											  + (help_width > 0 ? help_width + 4 : 0));
		}

		menu += 1;
	}

	if (config->left_alligned_shortcuts)
	{
		*columns = max_text_width + (max_help_width > 0 ? max_help_width + 1 : 0);
		*help_x_pos = max_text_width;
	}
	else
		*help_x_pos = -1;

	*naccelerators = naccel;

	/*
	 * When external accelerators are used, move content to right
	 */
	if (has_extern_accel)
	{
		*columns += config->extern_accel_text_space + 1;
		if (*help_x_pos != -1)
			*help_x_pos += config->extern_accel_text_space + 1;
		*item_x_pos = config->extern_accel_text_space + 1;
	}
	else
		*item_x_pos = 1;
}

/*
 * Draw menubar
 */
static void
menubar_draw(ST_MENU_STATE *menustate)
{
	ST_MENU	   *menu = menustate->menu;
	ST_MENU_CONFIG	*config = menustate->config;
	ST_MENU *aux_menu;
	int		i;

	werase(menustate->window);

	aux_menu = menu;
	i = 0;
	while (aux_menu->text != NULL)
	{
		char	*text = aux_menu->text;
		bool	highlight = false;
		bool	is_cursor_row = menustate->cursor_row == i + 1;
		int		current_pos;

		/* bar_fields_x_pos holds x positions of menubar items */
		current_pos = menustate->bar_fields_x_pos[i];

		if (is_cursor_row)
		{
			wmove(menustate->window, 0, current_pos - 1);
			wattron(menustate->window, COLOR_PAIR(config->cursor_cpn) | config->cursor_attr);
			waddstr(menustate->window, " ");
		}
		else
			wmove(menustate->window, 0, current_pos);

		while (*text)
		{
			/* there are not external accelerators */
			if (*text == '~')
			{
				if (text[1] == '~')
				{
					waddstr(menustate->window, "~");
					text += 2;
					continue;
				}

				if (!highlight)
				{
					wattron(menustate->window,
						COLOR_PAIR(is_cursor_row ? config->cursor_accel_cpn : config->accelerator_cpn) |
								   (is_cursor_row ? config->cursor_accel_attr : config->accelerator_attr));
				}
				else
				{
					wattroff(menustate->window,
						COLOR_PAIR(is_cursor_row ? config->cursor_accel_cpn : config->accelerator_cpn) |
								   (is_cursor_row ? config->cursor_accel_attr : config->accelerator_attr));
					if (is_cursor_row)
						wattron(menustate->window, COLOR_PAIR(config->cursor_cpn) | config->cursor_attr);
				}

				highlight = !highlight;
				text += 1;
			}
			else
			{
				int chlen = menustate->config->force8bit ? 1 : utf8charlen(*text);

				waddnstr(menustate->window, text, chlen);
				text += chlen;
			}
		}

		if (is_cursor_row)
		{
			waddstr(menustate->window, " ");
			wattroff(menustate->window, COLOR_PAIR(config->cursor_cpn) | config->cursor_attr);
		}

		aux_menu += 1;
		i += 1;
	}

	wnoutrefresh(menustate->window);
}

/*
 * pulldown menu bar draw
 */
static void
pulldownmenu_draw(ST_MENU_STATE *menustate)
{
	bool	draw_box = menustate->config->draw_box;
	ST_MENU	   *menu = menustate->menu;
	ST_MENU_CONFIG	*config = menustate->config;
	WINDOW	   *draw_area = menustate->draw_area;
	int		row = 1;
	int		maxy, maxx;
	int		text_min_x, text_max_x;

	if (menustate->shadow_window != NULL)
	{
		int		smaxy, smaxx;
		int		i;

		getmaxyx(menustate->shadow_window, smaxy, smaxx);

		/* mainwin must be global */
		overwrite(mainwin, menustate->shadow_window);

		for (i = 0; i <= smaxy; i++)
			mvwchgat(menustate->shadow_window, i, 0, smaxx,
							config->menu_shadow_attr,
							config->menu_shadow_cpn,
							NULL);

		wnoutrefresh(menustate->shadow_window);
	}

	werase(menustate->window);

	getmaxyx(draw_area, maxy, maxx);

	/* be compiler quiet */
	(void) maxy;

	if (draw_box)
		box(draw_area, 0, 0);

	text_min_x = (draw_box ? 1 : 0) + (config->extra_inner_space ? 1 : 0);
	text_max_x = maxx - (draw_box ? 1 : 0) - (config->extra_inner_space ? 1 : 0);

	while (menu->text != NULL)
	{
		if (*menu->text == '\0' || strncmp(menu->text, "--", 2) == 0)
		{
			int		i;

			if (draw_box)
			{
				wmove(draw_area, row, 0);
				waddch(draw_area, ACS_LTEE);
			}
			else
				wmove(draw_area, row - 1, 0);

			for(i = 0; i < maxx - 1 - (draw_box ? 1 : -1); i++)
				waddch(draw_area, ACS_HLINE);

			if (draw_box)
				waddch(draw_area, ACS_RTEE);
		}
		else
		{
			char	*text = menu->text;
			bool	highlight = false;
			bool	is_cursor_row = menustate->cursor_row == row;
			bool	first_char = true;
			bool	is_extern_accel;

			if (is_cursor_row)
			{
				mvwchgat(draw_area, row - (draw_box ? 0 : 1), text_min_x, text_max_x - text_min_x,
						config->cursor_attr, config->cursor_cpn, NULL);
				wattron(draw_area, COLOR_PAIR(config->cursor_cpn) | config->cursor_attr);
			}

			is_extern_accel = (*text == '_' && text[1] != '_');

			if (menustate->item_x_pos != 1 && !is_extern_accel)
			{
				wmove(draw_area, row - (draw_box ? 0 : 1), text_min_x + 1 + menustate->item_x_pos);
			}
			else
				wmove(draw_area, row - (draw_box ? 0 : 1), text_min_x + 1);

			while (*text)
			{
				if (*text == '~' || (*text == '_' && (first_char || highlight)))
				{
					if (text[1] == *text)
					{
						waddnstr(draw_area, text, 1);
						text += 2;
						first_char = false;
						continue;
					}

					if (!highlight)
					{
						wattron(draw_area,
							COLOR_PAIR(is_cursor_row ? config->cursor_accel_cpn : config->accelerator_cpn) |
									   (is_cursor_row ? config->cursor_accel_attr : config->accelerator_attr));
					}
					else
					{
						wattroff(draw_area,
							COLOR_PAIR(is_cursor_row ? config->cursor_accel_cpn : config->accelerator_cpn) |
									   (is_cursor_row ? config->cursor_accel_attr : config->accelerator_attr));
						if (is_cursor_row)
							wattron(draw_area, COLOR_PAIR(config->cursor_cpn) | config->cursor_attr);

						if (is_extern_accel)
						{
							int		y, x;

							getyx(draw_area, y, x);
							wmove(draw_area, y, x + config->extern_accel_text_space);
						}
					}

					highlight = !highlight;
					text += 1;
				}
				else
				{
					int chlen = menustate->config->force8bit ? 1 : utf8charlen(*text);

					waddnstr(draw_area, text, chlen);
					text += chlen;
				}

				first_char = false;
			}

			if (menu->shortcut != NULL)
			{
				if (menustate->help_x_pos != -1)
				{
					wmove(draw_area, row - (draw_box ? 0 : 1), menustate->help_x_pos + (draw_box ? 1 : 0));
				}
				else
				{
					int dspl = menustate->config->force8bit ? strlen(menu->shortcut) : utf_string_dsplen(menu->shortcut, SIZE_MAX);

					wmove(draw_area, row - (draw_box ? 0 : 1), text_max_x - dspl - 1);
				}
				waddstr(draw_area, menu->shortcut);
			}

			if (is_cursor_row)
				wattroff(draw_area, COLOR_PAIR(config->cursor_cpn) | config->cursor_attr);
		}

		menu += 1;
		row += 1;
	}

	wnoutrefresh(menustate->window);
}


/*
 * Show menu - pull down or menu bar.
 */
void
st_menu_post(ST_MENU_STATE *menustate)
{
	/* reset globals */
	selected_item = NULL;

	/* show pull down menu shadow */
	if (menustate->shadow_panel)
	{
		show_panel(menustate->shadow_panel);
		top_panel(menustate->shadow_panel);
	}

	show_panel(menustate->panel);
	top_panel(menustate->panel);

	update_panels();

	curs_set(0);
	noecho();

	/* show menu */
	if (menustate->is_menubar)
		menubar_draw(menustate);
	else
		pulldownmenu_draw(menustate);
}

/*
 * Hide menu
 */
void
st_menu_unpost(ST_MENU_STATE *menustate)
{
	/* hide active submenu */
	if (menustate->active_submenu)
	{
		st_menu_unpost(menustate->active_submenu);
		menustate->active_submenu = NULL;
	}

	hide_panel(menustate->panel);
	if (menustate->shadow_panel)
		hide_panel(menustate->shadow_panel);

	update_panels();
}

/*
 * Handle any outer event - pressed key, or mouse event. This driver
 * doesn't handle shortcuts - shortcuts are displayed only.
 */
void
st_menu_driver(ST_MENU_STATE *menustate, int c, MEVENT *mevent)
{
	ST_MENU_CONFIG	*config = menustate->config;
	int		cursor_row = menustate->cursor_row;		/* number of active menu item */
	bool	is_menubar = menustate->is_menubar;		/* true, when processed object is menu bar */
	int		first_row = -1;							/* number of row of first enabled item */
	int		first_code = -1;						/* code of first enabled item */

	int		last_row = -1;			/* last menu item */
	int		last_code = -1;			/* code of last menu item */
	int		mouse_row = -1;			/* item number selected by mouse */
	int		search_code = -1;		/* code menu selected by accelerator */

	bool	found_row = false;						/* we found new active menu item (row var will be valid) */
	bool	post_menu = false;						/* when it is true, then assiciated pulldown menu will be posted */

	int		row;
	ST_MENU *menu;

	menustate->press_accelerator = false;

	if (c == KEY_MOUSE)
	{
		if (mevent->bstate & BUTTON5_PRESSED)
		{
				c = KEY_DOWN;
		}
		else if (mevent->bstate & BUTTON4_PRESSED)
		{
			c = KEY_UP;
		}
		else if (mevent->bstate & BUTTON1_PRESSED)
		{
			if (is_menubar)
			{
				/*
				 * On menubar level we can process mouse event if row is zero, or
				 * we can check if mouse is positioned inside draw area or active
				 * submenu. If not, then we should to unpost active submenu.
				 */
				if (mevent->y == 0)
				{
					int		i = 0;
					int		chars_before;

					/*
					 * How much chars before active item and following item
					 * specify range of menubar item.
					 */
					chars_before = (config->text_space != -1) ? (config->text_space / 2) : 1;

					menu = menustate->menu;
					while (menu->text)
					{
						int		minx, maxx;

						minx = i > 0 ? (menustate->bar_fields_x_pos[i] - chars_before) : 0;
						maxx = menustate->bar_fields_x_pos[i + 1] - chars_before;

						/* transform possitions to target menu code */
						if (mevent->x >= minx && mevent->x < maxx)
						{
							mouse_row = i + 1;
							break;
						}

						menu += 1;
						i = i + 1;
					}
				}
				else
				{
					/* check if event is inside active submenu */
					if (menustate->active_submenu)
					{
						if (!wenclose(menustate->active_submenu->draw_area, mevent->y, mevent->x))
						{
							st_menu_unpost(menustate->active_submenu);
							menustate->active_submenu = NULL;
						}
					}
				}
			}
			else
			{
				int		row, col;

				row = mevent->y;
				col = mevent->x;

				/* calculate row from transformed mouse event */
				if (wmouse_trafo(menustate->draw_area, &row, &col, false))
						mouse_row = row + 1 - (config->draw_box ? 1:0);
			}
		}
	}

	/*
	 * Try to check if key is accelerator. This check should be on last level.
	 * So don't do it if submenu is active.
	 */
	if (!menustate->active_submenu &&
			c != KEY_HOME && c != KEY_END && c != KEY_UP && c != KEY_DOWN && c != KEY_MOUSE)
	{
		/* ToDo to_fold(int) */
		int		accelerator = menustate->config->force8bit ? tolower(c) : c;
		int		i;

		for (i = 0; i < menustate->naccelerators; i++)
		{
			if (menustate->accelerators[i].c == accelerator)
			{
				search_code = menustate->accelerators[i].code;
				break;
			}
		}
	}

	/*
	 * Iterate over menu items, and try to find next or previous row, code, or mouse
	 * row.
	 */
	menu = menustate->menu; row = 1;
	while (menu->text != 0)
	{
		if (*menu->text != '\0' && strncmp(menu->text, "--", 2) != 0)
		{
			if (first_row == -1)
			{
				first_row = row;
				first_code = menu->code;

				if (c == KEY_HOME && !is_menubar)
				{
					menustate->cursor_row = row;
					menustate->cursor_code = menu->code;
					found_row = true;
					break;
				}
			}

			if (is_menubar)
			{
				if (c == KEY_RIGHT && row > cursor_row) 
				{
					menustate->cursor_row = row;
					menustate->cursor_code = menu->code;
					found_row = true;
					break;
				}
				else if (c == KEY_LEFT && row == cursor_row)
				{
					if (last_row != -1)
					{
						menustate->cursor_row = last_row;
						menustate->cursor_code = last_code;
						found_row = true;
						break;
					}
				}
			}
			else
			{
				/* Is not menubar */
				if (c == KEY_DOWN && row > cursor_row)
				{
					menustate->cursor_row = row;
					menustate->cursor_code = menu->code;
					found_row = true;
					break;
				}
				else if (c == KEY_UP && row == cursor_row)
				{
					if (last_row != -1)
					{
						menustate->cursor_row = last_row;
						menustate->cursor_code = last_code;
						found_row = true;
						break;
					}
					else
						c = KEY_END;
				}
			}

			if (mouse_row != -1 && row == mouse_row)
			{
				menustate->cursor_row = row;
				menustate->cursor_code = menu->code;
				found_row = true;
				post_menu = true;
				break;
			}

			if (search_code != -1 && search_code == menu->code)
			{
				menustate->press_accelerator = true;
				menustate->cursor_row = row;
				menustate->cursor_code = search_code;
				post_menu =true;
				found_row = true;
				break;
			}

			last_row = row;
			last_code = menu->code;
		}
		menu += 1;
		row += 1;
	}

	/*
	 * Iterate over menu items in cycle
	 */
	if (!found_row)
	{
		if (is_menubar)
		{
			if (c == KEY_RIGHT)
			{
				menustate->cursor_row = first_row;
				menustate->cursor_code = first_code;
			}
			else if (c == KEY_LEFT)
			{
				menustate->cursor_row = last_row;
				menustate->cursor_code = last_code;
			}
		}
		else
		{
			if (c == KEY_END)
			{
				menustate->cursor_row = last_row;
				menustate->cursor_code = last_code;
			}
			else if (c == KEY_DOWN)
			{
				menustate->cursor_row = first_row;
				menustate->cursor_code = first_code;
			}
		}
	}

	if (is_menubar)
	{
		/* when menubar is changed, unpost active pulldown submenu */
		if (menustate->active_submenu && cursor_row != menustate->cursor_row)
		{
			st_menu_unpost(menustate->active_submenu);
			menustate->active_submenu = NULL;

			/* remember, submenu was visible */
			post_menu = true;
		}
		if (menustate->active_submenu != NULL)
		{
			/* When submenu is still active, post event to active submenu */
			st_menu_driver(menustate->active_submenu, c, mevent);
		}
		else if (menustate->press_accelerator
					|| c == KEY_DOWN || c == 10 || post_menu)
		{
			menustate->active_submenu = menustate->submenu_states[menustate->cursor_row - 1];
			st_menu_post(menustate->active_submenu);
		}
	}

	/* show menu */
	if (menustate->is_menubar)
		menubar_draw(menustate);
	else
		pulldownmenu_draw(menustate);
}

/*
 * Create state variable for pulldown menu. It based on template - a array of ST_MENU fields.
 * The initial position can be specified. The config (specify desplay properties) should be
 * passed. The config can be own or preloaded from preddefined styles by function st_menu_load_style.
 * a title is not supported yet.
 */
ST_MENU_STATE *
st_menu_new(ST_MENU_CONFIG *config, ST_MENU *menu, int begin_y, int begin_x, char *title)
{
	ST_MENU_STATE *menustate;
	int		rows, cols;
	ST_MENU *aux_menu;
	int		menu_fields;
	int		i;

	menustate = malloc(sizeof(ST_MENU_STATE));

	menustate->menu = menu;
	menustate->config = config;
	menustate->title = title;
	menustate->naccelerators = 0;
	menustate->is_menubar = false;

	/* how much items are in template */
	aux_menu = menu;
	while (aux_menu->text != NULL)
	{
		menu_fields += 1;
		aux_menu += 1;
	}

	/* preallocate good enough memory */
	menustate->accelerators = malloc(sizeof(ST_MENU_ACCELERATOR) * menu_fields);
	menustate->submenu_states = malloc(sizeof(ST_MENU_STATE) * menu_fields);

	menustate->nitems = menu_fields;

	/*
	 * Initialize submenu states (nested submenus)
	 */
	aux_menu = menu;
	i = 0;
	while (aux_menu->text)
	{
		if (aux_menu->submenu)
		{
			menustate->submenu_states[i] = 
					st_menu_new(config, aux_menu->submenu,
										begin_y + i + 1 
										+ (config->draw_box ? 1 : 0)
										+ (config->wide_vborders ? 1 : 0),
										begin_x
										+ (config->wide_hborders ? 1 : 0)
										+ (config->draw_box ? 1 : 0) + 1,
										NULL);
		}
		else
			menustate->submenu_states[i] = NULL;

		aux_menu += 1;
		i += 1;
	}

	/* get pull down menu dimensions */
	pulldownmenu_content_size(config, menu, &rows, &cols,
							&menustate->help_x_pos, &menustate->item_x_pos,
							menustate->accelerators, &menustate->naccelerators,
							&menustate->cursor_row, &menustate->cursor_code);

	if (config->draw_box)
	{
		rows += 2;
		cols += 2;
	}

	if (config->wide_vborders)
		cols += 2;
	if (config->wide_hborders)
		rows += 2;

	/* Prepare property for menu shadow */
	if (config->shadow_width > 0)
	{
		menustate->shadow_window = newwin(rows, cols, begin_y + 1, begin_x + config->shadow_width);
		menustate->shadow_panel = new_panel(menustate->shadow_window);

		hide_panel(menustate->shadow_panel);
		wbkgd(menustate->shadow_window, COLOR_PAIR(config->menu_shadow_cpn) | config->menu_shadow_attr);

		wnoutrefresh(menustate->shadow_window);
	}
	else
	{
		menustate->shadow_window = NULL;
		menustate->shadow_panel = NULL;
	}

	menustate->window = newwin(rows, cols, begin_y, begin_x);

	wbkgd(menustate->window, COLOR_PAIR(config->menu_background_cpn) | config->menu_background_attr);
	wnoutrefresh(menustate->window);

	/* draw area can be same like window or smaller */
	if (config->wide_vborders || config->wide_hborders)
	{
		menustate->draw_area = derwin(menustate->window,
			rows - (config->wide_hborders ? 2 : 0),
			cols - (config->wide_vborders ? 2 : 0),
			config->wide_hborders ? 1 : 0,
			config->wide_vborders ? 1 : 0);

		wbkgd(menustate->draw_area, COLOR_PAIR(config->menu_background_cpn) | config->menu_background_attr);

		wnoutrefresh(menustate->draw_area);
	}
	else
		menustate->draw_area = menustate->window;

	menustate->panel = new_panel(menustate->window);
	hide_panel(menustate->panel);

	return menustate;
}

/*
 * Create state variable for menubar based on template (array) of ST_MENU
 */
ST_MENU_STATE *
st_menu_new_menubar(ST_MENU_CONFIG *config, ST_MENU *menu)
{
	ST_MENU_STATE *menustate;
	int		maxy, maxx;
	ST_MENU *aux_menu;
	int		menu_fields;
	int		aux_width = 0;
	int		text_space;
	int		current_pos;
	int		i = 0;
	int		naccel = 0;

	getmaxyx(stdscr, maxy, maxx);

	/* by compiler quiet */
	(void) maxy;

	menustate = malloc(sizeof(ST_MENU_STATE));

	menustate->window = newwin(1, maxx, 0, 0);
	menustate->panel = new_panel(menustate->window);

	/* there are not shadows */
	menustate->shadow_window = NULL;
	menustate->shadow_panel = NULL;

	menustate->config = config;
	menustate->menu = menu;
	menustate->cursor_row = 1;
	menustate->active_submenu = NULL;

	menustate->is_menubar = true;

	wbkgd(menustate->window, COLOR_PAIR(config->menu_background_cpn) | config->menu_background_attr);

	aux_menu = menu;
	menu_fields = 0;
	while (aux_menu->text)
	{
		menu_fields += 1;

		if (config->text_space == -1)
			aux_width += menutext_displaywidth(config, aux_menu->text, NULL, NULL);

		aux_menu += 1;
	}

	/*
	 * last bar position is hypotetical - we should not to calculate length of last field
	 * every time.
	 */
	menustate->bar_fields_x_pos = malloc(sizeof(int) * (menu_fields + 1));
	menustate->submenu_states = malloc(sizeof(ST_MENU_STATE) * menu_fields);
	menustate->accelerators = malloc(sizeof(ST_MENU_ACCELERATOR) * menu_fields);

	menustate->nitems = menu_fields;

	/*
	 * When text_space is not defined, then try to vallign menu items
	 */
	if (config->text_space == -1)
	{
		text_space = (maxx + 1 - aux_width) / (menu_fields + 1);
		if (text_space < 4)
			text_space = 4;
		else if (text_space > 15)
			text_space = 15;
		current_pos = text_space;
	}
	else
	{
		text_space = config->text_space;
		current_pos = config->init_text_space;
	}

	/* Initialize submenu */
	aux_menu = menu; i = 0;
	while (aux_menu->text)
	{
		char	*accelerator;

		menustate->bar_fields_x_pos[i] = current_pos;
		current_pos += menutext_displaywidth(config, aux_menu->text, &accelerator, NULL);
		current_pos += text_space;
		if (aux_menu->submenu)
		{
			menustate->submenu_states[i] = 
					st_menu_new(config, aux_menu->submenu,
										1, menustate->bar_fields_x_pos[i] + 
										config->menu_bar_menu_offset
										- (config->draw_box ? 1 : 0)
										- (config->wide_vborders ? 1 : 0)
										- (config->extra_inner_space ? 1 : 0) - 1, NULL);
		}
		else
			menustate->submenu_states[i] = NULL;

		if (accelerator)
		{
			menustate->accelerators[naccel].c = 
				config->force8bit ? tolower(*accelerator) : utf8_tofold(accelerator);
			menustate->accelerators[naccel++].code = aux_menu->code;
		}

		menustate->naccelerators = naccel;

		aux_menu += 1;
		i += 1;
	}

	/*
	 * store hypotetical x bar position
	 */
	menustate->bar_fields_x_pos[i] = current_pos;

	return menustate;
}

/*
 * Prepared styles - config settings. start_from_cpn is first color pair 
 * number that can be used by st_menu library. For ST_MENU_STYLE_ONECOLOR
 * style it is number of already existing color pair.
 */
void
st_menu_load_style(ST_MENU_CONFIG *config, int style, int start_from_cpn)
{
	switch (style)
	{
		case ST_MENU_STYLE_MCB:
			use_default_colors();
			config->menu_background_cpn = start_from_cpn;
			config->menu_background_attr = 0;
			init_pair(start_from_cpn++, COLOR_BLACK, COLOR_WHITE);

			config->menu_shadow_cpn = start_from_cpn;
			config->menu_shadow_attr = 0;
			init_pair(start_from_cpn++, COLOR_WHITE, COLOR_BLACK);

			config->accelerator_cpn = start_from_cpn;
			config->accelerator_attr = A_BOLD;
			init_pair(start_from_cpn++, COLOR_WHITE, COLOR_BLACK);

			config->cursor_cpn = start_from_cpn;
			config->cursor_attr = 0;
			init_pair(start_from_cpn++, -1, -1);

			config->cursor_accel_cpn = start_from_cpn;
			config->cursor_accel_attr = A_BOLD;
			init_pair(start_from_cpn++, COLOR_WHITE, COLOR_BLACK);

			config->left_alligned_shortcuts = true;
			config->wide_vborders = false;
			config->wide_hborders = false;

			config->shortcut_space = 5;
			config->text_space = 5;
			config->init_text_space = 2;
			config->menu_bar_menu_offset = 0;
			config->shadow_width = 0;

			break;

		case ST_MENU_STYLE_MC:
			config->menu_background_cpn = start_from_cpn;
			config->menu_background_attr = A_BOLD;
			init_pair(start_from_cpn++, COLOR_WHITE, COLOR_CYAN);

			config->menu_shadow_cpn = start_from_cpn;
			config->menu_shadow_attr = 0;
			init_pair(start_from_cpn++, COLOR_WHITE, COLOR_BLACK);

			config->accelerator_cpn = start_from_cpn;
			config->accelerator_attr = A_BOLD;
			init_pair(start_from_cpn++, COLOR_YELLOW, COLOR_CYAN);

			config->cursor_cpn = start_from_cpn;
			config->cursor_attr = A_BOLD;
			init_pair(start_from_cpn++, COLOR_WHITE, COLOR_BLACK);

			config->cursor_accel_cpn = start_from_cpn;
			config->cursor_accel_attr = A_BOLD;
			init_pair(start_from_cpn++, COLOR_YELLOW, COLOR_BLACK);

			config->left_alligned_shortcuts = true;
			config->wide_vborders = false;
			config->wide_hborders = false;
			config->extra_inner_space = false;

			config->shortcut_space = 5;
			config->text_space = 5;
			config->init_text_space = 2;
			config->menu_bar_menu_offset = 0;
			config->shadow_width = 0;

			break;

		case ST_MENU_STYLE_VISION:
			config->menu_background_cpn = start_from_cpn;
			config->menu_background_attr = 0;
			init_pair(start_from_cpn++, COLOR_BLACK, COLOR_WHITE);

			config->menu_shadow_cpn = start_from_cpn;
			config->menu_shadow_attr = A_REVERSE;
			init_pair(start_from_cpn++, COLOR_BLACK, COLOR_WHITE);

			config->accelerator_cpn = start_from_cpn;
			config->accelerator_attr = 0;
			init_pair(start_from_cpn++, COLOR_RED, COLOR_WHITE);

			config->cursor_cpn = start_from_cpn;
			config->cursor_attr = 0;
			init_pair(start_from_cpn++, COLOR_BLACK, COLOR_GREEN);

			config->cursor_accel_cpn = start_from_cpn;
			config->cursor_accel_attr = 0;
			init_pair(start_from_cpn++, COLOR_RED, COLOR_GREEN);

			config->left_alligned_shortcuts = false;
			config->wide_vborders = true;
			config->wide_hborders = false;
			config->extra_inner_space = false;

			config->shortcut_space = 4;
			config->text_space = 2;
			config->init_text_space = 2;
			config->menu_bar_menu_offset = 1;
			config->shadow_width = 2;

			break;

		case ST_MENU_STYLE_DOS:
			config->menu_background_cpn = start_from_cpn;
			config->menu_background_attr = 0;
			init_pair(start_from_cpn++, COLOR_BLACK, COLOR_WHITE);

			config->menu_shadow_cpn = start_from_cpn;
			config->menu_shadow_attr = 0;
			init_pair(start_from_cpn++, COLOR_WHITE, COLOR_BLACK);

			config->accelerator_cpn = start_from_cpn;
			config->accelerator_attr = A_BOLD;
			init_pair(start_from_cpn++, COLOR_WHITE, COLOR_WHITE);

			config->cursor_cpn = start_from_cpn;
			config->cursor_attr = 0;
			init_pair(start_from_cpn++, COLOR_WHITE, COLOR_BLACK);

			config->cursor_accel_cpn = start_from_cpn;
			config->cursor_accel_attr = A_BOLD;
			init_pair(start_from_cpn++, COLOR_WHITE, COLOR_BLACK);

			config->left_alligned_shortcuts = false;
			config->wide_vborders = false;
			config->wide_hborders = false;
			config->extra_inner_space = false;

			config->shortcut_space = 4;
			config->text_space = 2;
			config->init_text_space = 1;
			config->menu_bar_menu_offset = 1;
			config->shadow_width = 2;

			break;

		case ST_MENU_STYLE_FAND_1:
			config->menu_background_cpn = start_from_cpn;
			config->menu_background_attr = 0;
			init_pair(start_from_cpn++, COLOR_BLACK, COLOR_CYAN);

			config->menu_shadow_cpn = start_from_cpn;
			config->menu_shadow_attr = 0;
			init_pair(start_from_cpn++, COLOR_WHITE, COLOR_BLACK);

			config->accelerator_cpn = start_from_cpn;
			config->accelerator_attr = 0;
			init_pair(start_from_cpn++, COLOR_RED, COLOR_CYAN);

			config->cursor_cpn = start_from_cpn;
			config->cursor_attr = A_BOLD;
			init_pair(start_from_cpn++, COLOR_YELLOW, COLOR_BLUE);

			config->cursor_accel_cpn = start_from_cpn;
			config->cursor_accel_attr = A_BOLD;
			init_pair(start_from_cpn++, COLOR_YELLOW, COLOR_BLUE);

			config->left_alligned_shortcuts = false;
			config->wide_vborders = false;
			config->wide_hborders = false;
			config->extra_inner_space = true;

			config->shortcut_space = 4;
			config->text_space = -1;
			config->init_text_space = 1;
			config->menu_bar_menu_offset = 2;
			config->shadow_width = 2;

			break;

		case ST_MENU_STYLE_FAND_2:
			config->menu_background_cpn = start_from_cpn;
			config->menu_background_attr = 0;
			init_pair(start_from_cpn++, COLOR_BLACK, COLOR_CYAN);

			config->menu_shadow_cpn = start_from_cpn;
			config->menu_shadow_attr = 0;
			init_pair(start_from_cpn++, COLOR_WHITE, COLOR_BLACK);

			config->accelerator_cpn = start_from_cpn;
			config->accelerator_attr = A_BOLD;
			init_pair(start_from_cpn++, COLOR_CYAN, COLOR_CYAN);

			config->cursor_cpn = start_from_cpn;
			config->cursor_attr = A_BOLD;
			init_pair(start_from_cpn++, COLOR_YELLOW, COLOR_BLUE);

			config->cursor_accel_cpn = start_from_cpn;
			config->cursor_accel_attr = A_BOLD;
			init_pair(start_from_cpn++, COLOR_YELLOW, COLOR_BLUE);

			config->left_alligned_shortcuts = false;
			config->wide_vborders = false;
			config->wide_hborders = false;
			config->extra_inner_space = true;

			config->shortcut_space = 4;
			config->text_space = -1;
			config->init_text_space = 1;
			config->menu_bar_menu_offset = 2;
			config->shadow_width = 2;

			break;

		case ST_MENU_STYLE_FOXPRO:
			config->menu_background_cpn = start_from_cpn;
			config->menu_background_attr = 0;
			init_pair(start_from_cpn++, COLOR_BLACK, COLOR_WHITE);

			config->menu_shadow_cpn = start_from_cpn;
			config->menu_shadow_attr = 0;
			init_pair(start_from_cpn++, COLOR_WHITE, COLOR_BLACK);

			config->accelerator_cpn = start_from_cpn;
			config->accelerator_attr = A_BOLD;
			init_pair(start_from_cpn++, COLOR_WHITE, COLOR_WHITE);

			config->cursor_cpn = start_from_cpn;
			config->cursor_attr = 0;
			init_pair(start_from_cpn++, COLOR_BLACK, COLOR_CYAN);

			config->cursor_accel_cpn = start_from_cpn;
			config->cursor_accel_attr = A_BOLD;
			init_pair(start_from_cpn++, COLOR_WHITE, COLOR_CYAN);

			config->left_alligned_shortcuts = false;
			config->wide_vborders = false;
			config->wide_hborders = false;
			config->extra_inner_space = false;

			config->shortcut_space = 4;
			config->text_space = 2;
			config->init_text_space = 1;
			config->menu_bar_menu_offset = 1;
			config->shadow_width = 2;

			break;

		case ST_MENU_STYLE_PERFECT:
			config->menu_background_cpn = start_from_cpn;
			config->menu_background_attr = 0;
			init_pair(start_from_cpn++, COLOR_BLACK, COLOR_WHITE);

			config->menu_shadow_cpn = start_from_cpn;
			config->menu_shadow_attr = 0;
			init_pair(start_from_cpn++, COLOR_WHITE, COLOR_BLACK);

			config->accelerator_cpn = start_from_cpn;
			config->accelerator_attr = 0;
			init_pair(start_from_cpn++, COLOR_RED, COLOR_WHITE);

			config->cursor_cpn = start_from_cpn;
			config->cursor_attr = A_BOLD;
			init_pair(start_from_cpn++, COLOR_WHITE, COLOR_RED);

			config->cursor_accel_cpn = start_from_cpn;
			config->cursor_accel_attr = 0;
			init_pair(start_from_cpn++, COLOR_WHITE, COLOR_RED);

			config->left_alligned_shortcuts = false;
			config->wide_vborders = false;
			config->wide_hborders = false;
			config->extra_inner_space = false;

			config->shortcut_space = 4;
			config->text_space = 2;
			config->init_text_space = 1;
			config->menu_bar_menu_offset = 1;
			config->shadow_width = 2;

			break;

		case ST_MENU_STYLE_NOCOLOR:
			use_default_colors();
			config->menu_background_cpn = 0;
			config->menu_background_attr = 0;

			config->menu_shadow_cpn = start_from_cpn;
			config->menu_shadow_attr = A_REVERSE;

			config->accelerator_cpn = 0;
			config->accelerator_attr = A_UNDERLINE;

			config->cursor_cpn = 0;
			config->cursor_attr = A_REVERSE;

			config->cursor_accel_cpn = 0;
			config->cursor_accel_attr = A_UNDERLINE | A_REVERSE;

			config->left_alligned_shortcuts = false;
			config->wide_vborders = false;
			config->wide_hborders = false;
			config->extra_inner_space = false;

			config->shortcut_space = 4;
			config->text_space = 2;
			config->init_text_space = 1;
			config->menu_bar_menu_offset = 1;
			config->shadow_width = 0;

			break;

		case ST_MENU_STYLE_ONECOLOR:
			use_default_colors();
			config->menu_background_cpn = start_from_cpn;
			config->menu_background_attr = 0;

			config->menu_shadow_cpn = start_from_cpn;
			config->menu_shadow_attr = A_REVERSE;

			config->accelerator_cpn = start_from_cpn;
			config->accelerator_attr = A_UNDERLINE;

			config->cursor_cpn = start_from_cpn;
			config->cursor_attr = A_REVERSE;

			config->cursor_accel_cpn = start_from_cpn;
			config->cursor_accel_attr = A_UNDERLINE | A_REVERSE;

			config->left_alligned_shortcuts = false;
			config->wide_vborders = false;
			config->wide_hborders = false;

			config->shortcut_space = 4;
			config->text_space = 2;
			config->init_text_space = 1;
			config->menu_bar_menu_offset = 1;
			config->extra_inner_space = false;

			config->shortcut_space = 4;
			config->text_space = 2;
			config->init_text_space = 1;
			config->menu_bar_menu_offset = 1;
			config->shadow_width = 0;

			break;

		case ST_MENU_STYLE_TURBO:
			config->menu_background_cpn = start_from_cpn;
			config->menu_background_attr = 0;
			init_pair(start_from_cpn++, COLOR_BLACK, COLOR_WHITE);

			config->menu_shadow_cpn = start_from_cpn;
			config->menu_shadow_attr = 0;
			init_pair(start_from_cpn++, COLOR_WHITE, COLOR_BLACK);

			config->accelerator_cpn = start_from_cpn;
			config->accelerator_attr = 0;
			init_pair(start_from_cpn++, COLOR_RED, COLOR_WHITE);

			config->cursor_cpn = start_from_cpn;
			config->cursor_attr = A_BOLD;
			init_pair(start_from_cpn++, COLOR_WHITE, COLOR_BLACK);

			config->cursor_accel_cpn = start_from_cpn;
			config->cursor_accel_attr = A_BOLD;
			init_pair(start_from_cpn++, COLOR_WHITE, COLOR_BLACK);

			config->left_alligned_shortcuts = false;
			config->wide_vborders = false;
			config->wide_hborders = false;
			config->extra_inner_space = false;

			config->shortcut_space = 4;
			config->text_space = 2;
			config->init_text_space = 1;
			config->menu_bar_menu_offset = 1;
			config->shadow_width = 2;

			break;

		case ST_MENU_STYLE_PDMENU:
			config->menu_background_cpn = start_from_cpn;
			config->menu_background_attr = 0;
			init_pair(start_from_cpn++, COLOR_BLACK, COLOR_CYAN);

			config->menu_shadow_cpn = start_from_cpn;
			config->menu_shadow_attr = 0;
			init_pair(start_from_cpn++, COLOR_WHITE, COLOR_BLACK);

			config->accelerator_cpn = start_from_cpn;
			config->accelerator_attr = A_BOLD;
			init_pair(start_from_cpn++, COLOR_WHITE, COLOR_CYAN);

			config->cursor_cpn = start_from_cpn;
			config->cursor_attr = 0;
			init_pair(start_from_cpn++, COLOR_CYAN, COLOR_BLACK);

			config->cursor_accel_cpn = start_from_cpn;
			config->cursor_accel_attr = A_BOLD;
			init_pair(start_from_cpn++, COLOR_WHITE, COLOR_BLACK);

			config->left_alligned_shortcuts = false;
			config->wide_vborders = false;
			config->wide_hborders = false;
			config->extra_inner_space = false;

			config->shortcut_space = 4;
			config->text_space = 2;
			config->init_text_space = 1;
			config->menu_bar_menu_offset = 1;
			config->shadow_width = 2;

			break;
	}

	config->draw_box = true;
	config->extern_accel_text_space = 2;
}



/*
 * Application demo
 */
 
int
main()
{
	int		maxx, maxy;
	PANEL *mainpanel;
	ST_MENU_CONFIG config;
	ST_MENU_STATE *menustate;
	int		c;
	MEVENT	mevent;
	int		i;

	const char *demo = 
			"Lorem ipsum dolor sit amet, consectetur adipiscing elit, sed do eiusmod tempor incididunt ut labore "
			"et dolore magna aliqua. Ut enim ad minim veniam, quis nostrud exercitation ullamco laboris nisi ut "
			"aliquip ex ea commodo consequat. Duis aute irure dolor in reprehenderit in voluptate velit esse cillum "
			"dolore eu fugiat nulla pariatur. Excepteur sint occaecat cupidatat non proident, sunt in culpa qui officia"
			"deserunt mollit anim id est laborum.";

	ST_MENU _left[] = {
		{"File listin~g~", 1, NULL},
		{"~Q~uick view", 2, "C-x q"},
		{"~I~nfo", 3, "C-x i"},
		{"~T~ree", 4, NULL},
		{"--", -1, NULL},
		{"~L~isting mode...", 5, NULL},
		{"~S~ort order...", 6, NULL},
		{"~F~ilter...", 7, NULL},
		{"~E~ncoding", 8, "M-e"},
		{"--", -1, NULL},
		{"FT~P~ link...", 9, NULL},
		{"S~h~ell link...", 10, NULL},
		{"S~F~TP link...", 11, NULL},
		{"SM~B~ link...", 12, NULL},
		{"Paneli~z~e", 13, NULL},
		{"--", -1, NULL},
		{"~R~escan", 14, "C-r"},
		{NULL, -1, NULL}
	};

	ST_MENU _file[] = {
		{"~V~iew", 15, "F3"},
		{"Vie~w~ file...",16, NULL},
		{"~F~iltered view", 17, "M-!"},
		{"~E~dit", 18, "F4"},
		{"~C~opy", 19, "F5"},
		{"C~h~mod", 20, "C-x c"},
		{"~L~ink", 21, "C-x l"},
		{"~S~ymlink", 22, "C-x s"},
		{"Relative symlin~k~", 23, "C-x v"},
		{"Edit s~y~mlink", 24, "C-x C-s"},
		{"Ch~o~wn", 25, "C-x o"},
		{"~A~dvanced chown", 26, NULL},
		{"~R~ename/Move", 27, "F6"},
		{"~M~kdir", 28, "F7"},
		{"~D~elete", 29, "F8"},
		{"~Q~uick cd", 30, "M-c"},
		{"--", -1, NULL},
		{"Select ~g~roup", 31, "+"},
		{"U~n~select group", 32, "-"},
		{"~I~nvert selection", 33, "*"},
		{"--", -1, NULL},
		{"E~x~it", 34, "F10"},
		{NULL, -1, NULL}
	};

	ST_MENU _command[] = {
		{"~U~ser menu", 35, "F2"},
		{"~D~irectory tree", 36, NULL},
		{"~F~ind file", 37, "M-?"},
		{"S~w~ap panels", 38, "C-u"},
		{"Switch ~p~anels on/off", 39, "C-o"},
		{"~C~ompare directories", 40, "C-x d"},
		{"C~o~mpare files", 41, "C-x C-d"},
		{"E~x~ternal panelize", 42, "C-x !"},
		{"~S~how directory sizes", 43, "C-Space"},
		{"--", -1, NULL},
		{"Command ~h~istory", 44, "M-h"},
		{"Di~r~ectory hotlist", 45, "C-\\"},
		{"~A~ctive VFS list", 46, "C-x a"},
		{"~B~ackground jobs", 47, "C-x j"},
		{"Screen lis~t~", 48, "M-`"},
		{"--", -1, NULL},
		{"Edit ~e~xtension file", 49, NULL},
		{"Edit ~m~enu file", 50, NULL},
		{"Edit highli~g~hting group file", 51, NULL},
		{"_@_Do something on current file", 52, NULL},
		{NULL, -1, NULL}
	};

	ST_MENU menubar[] = {
		{"~L~eft", 60, NULL, _left},
		{"~F~ile", 61, NULL, _file},
		{"~C~ommand", 62, NULL, _command},
		{NULL, -1, NULL}
	};

	config.force8bit = false;

	setlocale(LC_ALL, "");

	initscr();
	start_color();
	clear();
	cbreak();
	noecho();
	keypad(stdscr, true);
	mouseinterval(0);

	refresh();

	init_pair(1, COLOR_WHITE, COLOR_BLUE);

	/* load style, possible alternatives: ST_MENU_STYLE_MC, ST_MENU_STYLE_DOS */
	st_menu_load_style(&config, ST_MENU_STYLE_VISION, 2);

#if NCURSES_MOUSE_VERSION > 1

	mousemask(BUTTON1_PRESSED | BUTTON4_PRESSED | BUTTON5_PRESSED, NULL);

#else

	mousemask(BUTTON1_PRESSED, NULL);

#endif

	/* prepare main window */
	getmaxyx(stdscr, maxy, maxx);

	mainwin = newwin(maxy, maxx, 0, 0);
	wbkgd(mainwin, COLOR_PAIR(1));

	for (i = 0; i <= maxy; i++)
	{
		wmove(mainwin, i, 0);

		waddnstr(mainwin, demo + i , maxx);
	}

	wrefresh(mainwin);

	mainpanel = new_panel(mainwin);

	/* be compiler quiet */
	(void) mainpanel;

	/* prepare state variable for menubar */
	menustate = st_menu_new_menubar(&config, menubar);

	/* post meubar (display it) */
	st_menu_post(menustate);

	doupdate();

	c = getch(); if (c == KEY_MOUSE) getmouse(&mevent);

	while (c != 'q')
	{

		/* when submenu is not active, then enter activate submenu,
		 * else end
		 */
		if (c == 10 && menustate->active_submenu != NULL)
		{
			break;
		}

		/* send event to menubar (top object) */
		st_menu_driver(menustate, c, &mevent);
		doupdate();

		/* get new event */
		c = getch(); if (c == KEY_MOUSE) getmouse(&mevent);
	}

	endwin();

	return 0;
}
