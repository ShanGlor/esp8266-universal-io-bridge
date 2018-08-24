#include "io_ledpixel.h"
#include "i2c.h"
#include "util.h"
#include "uart.h"

#include <user_interface.h>

#include <stdlib.h>

static int esp8266_pin = -1;
static int esp8266_uart = -1;

typedef struct
{
	unsigned int	enabled:1;
	unsigned int	extended:1;
	unsigned int	grb:1;
	uint32_t		value;
} ledpixel_data_pin_t;

static ledpixel_data_pin_t ledpixel_data_pin[max_pins_per_io];

#if 0
irom static unsigned int simulate_uart(unsigned int in)
{
	unsigned int reversed = 0;
	unsigned int bit;

	for(bit = 0; bit < 6; bit++)
	{
		reversed <<= 1;
		reversed |= in & (1 << bit) ? 0b1 : 0b0;
	}

	reversed <<= 1;
	reversed |= 0x01;

	return(reversed ^ 0xff);
}
#endif

irom static void send_byte(unsigned int byte)
{
	// from an idea by nodemcu coders: https://github.com/nodemcu/nodemcu-firmware/blob/master/app/modules/ws2812.c

	static const unsigned int bit_pattern[4] =
	{				//		mirror		add start/stop	negate
		0b110111,	//	00	111-011		[0]111-011[1]	1000-1000
		0b000111,	//	01	111-000		[0]111-000[1]	1000-1110
		0b110100,	//	10	001-011		[0]001-011[1]	1110-1000
		0b000100,	//	11	001-000		[0]001-000[1]	1110-1110
	};

	unsigned int byte_bit_index;
	unsigned int by_six = 0;

	for(byte_bit_index = 0; byte_bit_index < 4; byte_bit_index++)
	{
		by_six = bit_pattern[(byte & 0b11000000) >> 6];
		byte <<= 2;
		uart_send(esp8266_uart, by_six);
	}
}

irom static void send_all(bool_t force)
{
	unsigned int pin;

	for(pin = 0; pin < max_pins_per_io; pin++)
	{
		if(!force && !ledpixel_data_pin[pin].enabled)
			break;

		if(ledpixel_data_pin[pin].grb)
		{
			send_byte((ledpixel_data_pin[pin].value & 0x0000ff00) >>   8);
			send_byte((ledpixel_data_pin[pin].value & 0x00ff0000) >>  16);
		}
		else
		{
			send_byte((ledpixel_data_pin[pin].value & 0x00ff0000) >>  16);
			send_byte((ledpixel_data_pin[pin].value & 0x0000ff00) >>   8);
		}

		send_byte((ledpixel_data_pin[pin].value & 0x000000ff) >>  0);

		// some ws2812's have four leds (including a white one) and need an extra byte to be sent for it

		if(ledpixel_data_pin[pin].extended)
			send_byte((ledpixel_data_pin[pin].value & 0xff000000) >>  24);
	}

	uart_flush(esp8266_uart);
}

irom void io_ledpixel_setup(unsigned pin, unsigned uart)
{
	esp8266_pin = pin;
	esp8266_uart = uart;
}

irom void io_ledpixel_post_init(const struct io_info_entry_T *info)
{
	send_all(true);
}

irom io_error_t io_ledpixel_init(const struct io_info_entry_T *info)
{
	if((esp8266_pin < 0) || (esp8266_pin > 15) || (esp8266_uart < 0) || (esp8266_uart > 1))
		return(io_error);

	uart_baudrate(esp8266_uart, 3200000);
	uart_data_bits(esp8266_uart, 6);
	uart_stop_bits(esp8266_uart, 1);
	uart_parity(esp8266_uart, parity_none);

	return(io_ok);
}

irom io_error_t io_ledpixel_init_pin_mode(string_t *error_message, const struct io_info_entry_T *info, io_data_pin_entry_t *pin_data, const io_config_pin_entry_t *pin_config, int pin)
{
	ledpixel_data_pin[pin].enabled = pin_config->llmode == io_pin_ll_output_analog;
	ledpixel_data_pin[pin].extended = pin_config->flags.extended;
	ledpixel_data_pin[pin].grb = pin_config->flags.grb;
	ledpixel_data_pin[pin].value = 0;

	return(io_ok);
}

irom io_error_t io_ledpixel_read_pin(string_t *error_message, const struct io_info_entry_T *info, io_data_pin_entry_t *pin_data, const io_config_pin_entry_t *pin_config, int pin, uint32_t *value)
{
	*value = ledpixel_data_pin[pin].value;

	return(io_ok);
}

irom io_error_t io_ledpixel_write_pin(string_t *error_message, const struct io_info_entry_T *info, io_data_pin_entry_t *pin_data, const io_config_pin_entry_t *pin_config, int pin, uint32_t value)
{
	ledpixel_data_pin[pin].value = value;

	send_all(false);

	return(io_ok);
}
