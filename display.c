#include "display.h"
#include "display_saa.h"
#include "display_lcd.h"
#include "display_orbital.h"

#include "util.h"
#include "stats.h"
#include "config.h"

#include <user_interface.h>

typedef enum
{
	display_slot_amount = 8,
	display_slot_tag_size = 32,
	display_slot_content_size = 64
} display_slot_enum_t;

assert_size(display_slot_enum_t, 4);

typedef enum
{
	display_saa1064 = 0,
	display_lcd = 1,
	display_orbital = 2,
	display_error,
	display_size = display_error
} display_id_t;

assert_size(display_id_t, 4);

typedef const struct
{
	int				const size;
	const char *	const name;
	const char *	const type;
	bool_t			(* const init_fn)(void);
	bool_t			(* const bright_fn)(int brightness);
	bool_t			(* const set_fn)(const char *tag, const char *text);
	bool_t			(* const show_fn)(void);
} display_info_t;

typedef struct
{
	int	detected;
	int	current_slot;
} display_data_t;

typedef struct
{
	int		timeout;
	char	tag[display_slot_tag_size];
	char	content[display_slot_content_size];
} display_slot_t;

typedef struct
{
	uint16_t utf16;
	uint8_t to;
} display_common_map_t;

const display_common_map_t display_common_map[display_common_map_amount] =
{
	{	0x00b0, 0xdf },	// °
	{	0x03b1, 0xe0 },	// α
	{	0x00e4, 0xe1 },	// ä
	{	0x03b2, 0xe2 },	// β
	{	0x03b5, 0xe3 },	// ε
	{	0x03bc, 0xe4 },	// μ
	{	0x03c3, 0xe5 },	// σ
	{	0x03c1, 0xe6 },	// ρ
	{	0x00f1, 0xee },	// ñ
	{	0x00f6, 0xef },	// ö
	{	0x03b8, 0xf2 },	// θ
	{	0x221e, 0xf3 },	// ∞ FIXME: this cannot work with 2-byte UTF-8
	{	0x03a9, 0xf4 },	// Ω
	{	0x03a3, 0xf6 },	// Σ
	{	0x03c0, 0xf7 },	// π
};

const display_common_udg_t display_common_udg[display_common_udg_amount] =
{
	{
		0x00e9,		// é	0
		{
			0b00000100,
			0b00001000,
			0b00001110,
			0b00010001,
			0b00011111,
			0b00010000,
			0b00001110,
			0b00000000,
		}
	},
	{
		0x00e8,	// è	1
		{
			0b00001000,
			0b00000100,
			0b00001110,
			0b00010001,
			0b00011111,
			0b00010000,
			0b00001110,
			0b00000000,
		}
	},
	{
		0x00ea,	// ê	2
		{
			0b00000100,
			0b00001010,
			0b00001110,
			0b00010001,
			0b00011111,
			0b00010000,
			0b00001110,
			0b00000000,
		}
	},
	{
		0x00eb,	// ë	3
		{
			0b00001010,
			0b00000000,
			0b00001110,
			0b00010001,
			0b00011111,
			0b00010000,
			0b00001110,
			0b00000000,
		}
	},
	{
		0x00fc,	// ü	4
		{
			0b00001010,
			0b00000000,
			0b00010001,
			0b00010001,
			0b00010001,
			0b00010011,
			0b00001101,
			0b00000000,
		}
	},
	{
		0x00e7,	// ç	5
		{
			0b00000000,
			0b00000000,
			0b00001110,
			0b00010000,
			0b00010000,
			0b00010101,
			0b00001110,
			0b00000100,
		}
	},
	{
		0x20ac,	// €	6 // FIXME: this cannot work with 2-byte UTF-8
		{
			0b00001000,
			0b00000100,
			0b00010110,
			0b00011001,
			0b00010001,
			0b00010001,
			0b00010001,
			0b00000000,
		}
	},
	{
		0x00ef,	// ï	7
		{
			0b00001010,
			0b00000000,
			0b00001100,
			0b00000100,
			0b00000100,
			0b00000100,
			0b00001110,
			0b00000000,
		}
	}
};

static roflash display_info_t display_info[display_size] =
{
	{
		4, "saa1064", "4 digit led display",
		display_saa1064_init,
		display_saa1064_bright,
		display_saa1064_set,
		(void *)0,
	},
	{
		80, "hd44780", "4x20 character LCD display",
		display_lcd_init,
		display_lcd_bright,
		display_lcd_set,
		display_lcd_show
	},
	{
		80, "matrix orbital", "4x20 character VFD display",
		display_orbital_init,
		display_orbital_bright,
		display_orbital_set,
		display_orbital_show
	}
};

display_common_row_status_t display_common_row_status;
uint8_t display_common_buffer[display_common_buffer_rows][display_common_buffer_columns];

static display_data_t display_data;
static display_slot_t display_slot[display_slot_amount];

irom bool_t display_common_set(const char *tag, const char *text)
{
	unsigned int current, mapped, utf16;
	int y, x, ix;

	for(y = 0; y < display_common_buffer_rows; y++)
		for(x = 0; x < display_common_buffer_columns; x++)
			display_common_buffer[y][x] = ' ';

	x = 0;
	y = 0;
	utf16 = 0x00;

	for(;;)
	{
		if(tag && ((current = (uint8_t)*tag++) == '\0'))
		{
			tag = (char *)0;
			x = 0;
			y = 1;
			utf16 = 0x00;
		}

		if(!tag && ((current = (uint8_t)*text++) == '\0'))
			break;

		mapped = ~0UL;

		if(utf16)
		{
			if((current & 0xc0) == 0x80) // valid second byte of a two-byte sequence
			{
				utf16 |= current & 0x3f;

				for(ix = 0; ix < display_common_map_amount; ix++)
				{
					if(display_common_map[ix].utf16 == utf16)
					{
						mapped = display_common_map[ix].to;
						break;
					}
				}

				for(ix = 0; ix < display_common_udg_amount; ix++)
				{
					if((display_common_udg[ix].utf16 == utf16))
					{
						mapped = ix;
						break;
					}
				}
			}
		}

		utf16 = 0x0000;

		if(mapped != ~0UL)
			current = mapped;
		else
		{
			if((current & 0xe0) == 0xc0) // UTF-8, start of two byte sequence
			{
				utf16 = (current & 0x1f) << 6;
				continue;
			}

			if(current == '\r')
			{
				x = 0;

				continue;
			}

			if(current == '\n')
			{
				x = 0;
				tag = (char *)0;

				if(y < 4)
					y++;

				continue;
			}

			if((current < ' ') || (current >= 0x80))
				current = ' ';
		}

		if((y < display_common_buffer_rows) && (x < display_common_buffer_columns))
			display_common_buffer[y][x++] = (uint8_t)(current & 0xff);
	}

	for(ix = 0; ix < display_common_buffer_rows; ix++)
		display_common_row_status.row[ix].dirty = 1;

	return(true);
}

irom static void display_update(bool_t advance)
{
	const char *display_text;
	int slot;
	display_info_t *display_info_entry;
	string_new(static, tag_text, 32);
	string_new(static, info_text, 64);

	if(display_data.detected < 0)
		return;

	display_info_entry = &display_info[display_data.detected];

	for(slot = display_data.current_slot + (advance ? 1 : 0); slot < display_slot_amount; slot++)
		if(display_slot[slot].content[0])
			break;

	if(slot >= display_slot_amount)
		for(slot = 0; slot < display_slot_amount; slot++)
			if(display_slot[slot].content[0])
				break;

	if(slot >= display_slot_amount)
		slot = 0;

	display_data.current_slot = slot;
	display_text = display_slot[slot].content;

	if(!ets_strcmp(display_text, "%%%%"))
	{
		string_clear(&info_text);
		string_format(&info_text, "%02u.%02u %s %s",
				rt_hours, rt_mins, display_info_entry->name, display_info_entry->type);
		display_text = string_to_ptr(&info_text);
	}

	if(ets_strcmp(display_slot[slot].tag, "-"))
	{
		string_clear(&tag_text);
		string_format(&tag_text, "%02u:%02u ", rt_hours, rt_mins);
		string_cat_ptr(&tag_text, display_slot[slot].tag);
		string_format(&tag_text, " [%u]", slot);
		display_info_entry->set_fn(string_to_ptr(&tag_text), display_text);
	}
	else
		display_info_entry->set_fn((char *)0, display_text);
}

irom static void display_expire(void) // call one time per second
{
	int active_slots, slot;

	if(display_data.detected < 0)
		return;

	active_slots = 0;

	for(slot = 0; slot < display_slot_amount; slot++)
	{
		if(display_slot[slot].timeout == 1)
		{
			display_slot[slot].timeout = 0;
			display_slot[slot].tag[0] = '\0';
			display_slot[slot].content[0] = '\0';
		}

		if(display_slot[slot].content[0])
			active_slots++;
	}

	if(active_slots == 0)
	{
		display_slot[0].timeout = 1;
		strlcpy(display_slot[0].tag, "boot", display_slot_tag_size - 1);
		strlcpy(display_slot[0].content, config.display.default_msg, display_slot_content_size - 1);
	}
}

iram bool display_periodic(void) // gets called 10 times per second
{
	static int last_update = 0;
	static int expire_counter = 0;
	int now;
	display_info_t *display_info_entry;

	if(display_data.detected < 0)
		return(false);

	now = system_get_time() / 1000000;

	display_info_entry = &display_info[display_data.detected];

	if(++expire_counter > 10) // expire once a second
	{
		expire_counter = 0;
		display_expire();
	}

	if((last_update > now) || ((last_update + config.display.flip_timeout) < now))
	{
		last_update = now;
		display_update(true);
	}

	if(display_info_entry->show_fn)
		return(display_info_entry->show_fn());

	return(false);
}

irom void display_init(void)
{
	display_info_t *display_info_entry;
	int current, slot;

	display_data.detected = -1;

	for(current = 0; current < display_size; current++)
	{
		display_info_entry = &display_info[current];

		if(display_info_entry->init_fn && (display_info_entry->init_fn()))
		{
			display_data.detected = current;
			break;
		}
	}

	display_data.current_slot = 0;

	for(slot = 0; slot < display_slot_amount; slot++)
	{
		display_slot[slot].timeout = 0;
		display_slot[slot].tag[0] = '\0';
		display_slot[slot].content[0] = '\0';
	}
}

irom static void display_dump(string_t *dst)
{
	display_info_t *display_info_entry;
	int slot;

	if(display_data.detected < 0)
	{
		string_cat(dst, "> no displays detected\n");
		return;
	}

	display_info_entry = &display_info[display_data.detected];

	string_format(dst, "> display type #%u (%s: %s)\n", display_data.detected,
			display_info_entry->name, display_info_entry->type);

	for(slot = 0; slot < display_slot_amount; slot++)
		string_format(dst, ">> %c slot %u: timeout %u, tag: \"%s\", text: \"%s\"\n",
				slot == display_data.current_slot ? '+' : ' ',
				slot, display_slot[slot].timeout, display_slot[slot].tag, display_slot[slot].content);
}

irom app_action_t application_function_display_dump(const string_t *src, string_t *dst)
{
	display_dump(dst);

	return(app_action_normal);
}

irom app_action_t application_function_display_default_message(const string_t *src, string_t *dst)
{
	const char *text;
	int ws;

	text = string_to_const_ptr(src);

	for(ws = 1; ws > 0; text++)
	{
		if(*text == '\0')
			break;

		if(*text == ' ')
			ws--;
	}

	strlcpy(config.display.default_msg, text, sizeof(config.display.default_msg) - 1);
	string_format(dst, "set default display message to \"%s\"\n", config.display.default_msg);

	return(app_action_normal);
}

irom app_action_t application_function_display_flip_timeout(const string_t *src, string_t *dst)
{
	int timeout;

	if(parse_int(1, src, &timeout, 0) == parse_ok)
	{
		if((timeout < 1) || (timeout > 60))
		{
			string_format(dst, "display-flip-timeout: invalid timeout: %u\n", timeout);
			return(app_action_error);
		}

		config.display.flip_timeout = (uint16_t)timeout;
	}

	string_format(dst, "display-flip-timeout: %u s\n", config.display.flip_timeout);

	return(app_action_normal);
}

irom app_action_t application_function_display_brightness(const string_t *src, string_t *dst)
{
	int value;
	display_info_t *display_info_entry;

	if(display_data.detected < 0)
	{
		string_cat(dst, "display_brightess: no display detected\n");
		return(app_action_error);
	}

	display_info_entry = &display_info[display_data.detected];

	if(parse_int(1, src, &value, 0) != parse_ok)
	{
		string_cat(dst, "display-brightness: usage: <brightness>=0,1,2,3,4\n");
		return(app_action_error);
	}

	if(!display_info_entry->bright_fn || !display_info_entry->bright_fn(value))
	{
		string_format(dst, "display-brightness: invalid brightness value: %d\n", value);
		return(app_action_error);
	}

	string_format(dst, "display brightness: %u\n", value);

	return(app_action_normal);
}

irom app_action_t application_function_display_set(const string_t *src, string_t *dst)
{
	int slot, timeout, current;
	const char *text;

	if(display_data.detected < 0)
	{
		string_cat(dst, "display_set: no display detected\n");
		return(app_action_error);
	}

	if((parse_int(1, src, &slot, 0) != parse_ok) ||
		(parse_int(2, src, &timeout, 0) != parse_ok) ||
		(parse_string(3, src, dst) != parse_ok))
	{
		string_clear(dst);
		string_cat(dst, "display-set: usage: slot timeout tag text\n");
		return(app_action_error);
	}

	text = src->buffer;

	for(current = 4; current > 0; text++)
	{
		if(*text == '\0')
			break;

		if(*text == ' ')
			current--;
	}

	if(current > 0)
	{
		string_clear(dst);
		string_cat(dst, "display-set: usage: slot timeout tag TEXT\n");
		return(app_action_error);
	}

	if(slot > display_slot_amount)
	{
		string_clear(dst);
		string_format(dst, "display-set: slot #%d out of limits\n", slot);
		return(app_action_error);
	}

	strlcpy(display_slot[slot].tag, string_to_ptr(dst), display_slot_tag_size - 1);
	strlcpy(display_slot[slot].content, text, display_slot_content_size - 1);
	display_slot[slot].timeout = timeout;

	display_update(false);
	string_clear(dst);

	string_format(dst, "display-set: set slot %d with tag %s to \"%s\"\n",
				slot, display_slot[slot].tag,
				display_slot[slot].content);

	return(app_action_normal);
}
