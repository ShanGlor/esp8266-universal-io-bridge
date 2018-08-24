#include "init.h"

#include "util.h"
#include "io.h"
#include "stats.h"
#include "time.h"
#include "dispatch.h"

#include <user_interface.h>

#if IMAGE_OTA == 1
#include <rboot-api.h>
#endif

static void user_init2(void);
void user_init(void);

void user_spi_flash_dio_to_qio_pre_init(void);
iram attr_const void user_spi_flash_dio_to_qio_pre_init(void)
{
}

uint32_t user_rf_cal_sector_set(void);
iram attr_const uint32_t user_rf_cal_sector_set(void)
{
	stat_called.user_rf_cal_sector_set = 1;
	return(RFCAL_ADDRESS / 0x1000);
}

void user_rf_pre_init(void);
iram void user_rf_pre_init(void)
{
	system_phy_set_powerup_option(3); // default = 0, otherwise 3
	system_phy_freq_trace_enable(1); // default = 0, otherwise 1

	stat_called.user_rf_pre_init = 1;
}

irom void user_init(void)
{
	// don't declare stack variables here, they will get overwritten

	register uint32_t *paint;
	volatile uint32_t sp;

	stat_stack_sp_initial = &sp;

	for(paint = (typeof(paint))stack_top; (paint < (typeof(paint))stack_bottom) && (paint < &sp); paint++)
	{
		*paint = stack_paint_magic;
		stat_stack_painted += 4;
	}

	system_set_os_print(0);
	dispatch_init1();
	config_read();
	uart_init();
	uart_set_initial(0);
	uart_set_initial(1);
	os_install_putc1(&logchar);
	system_set_os_print(1);

	if(config_flags_get().flag.wlan_power_save)
		wifi_set_sleep_type(MODEM_SLEEP_T);
	else
		wifi_set_sleep_type(NONE_SLEEP_T);

	system_init_done_cb(user_init2);
}

irom static void user_init2(void)
{
	dispatch_init2();

	if(config_flags_get().flag.cpu_high_speed)
		system_update_cpu_freq(160);
	else
		system_update_cpu_freq(80);

	wlan_init();
	time_init();
	io_init();
}

irom bool_t wlan_init(void)
{
	int wlan_mode_int;
	config_wlan_mode_t wlan_mode;
	string_new(, string_ssid, 64);
	string_new(, string_passwd, 64);
	int channel;
	struct station_config cconf;
	struct softap_config saconf;
	string_init(varname_wlan_mode, "wlan.mode");
	string_init(varname_wlan_client_ssid, "wlan.client.ssid");
	string_init(varname_wlan_client_passwd, "wlan.client.passwd");
	string_init(varname_wlan_ap_ssid, "wlan.ap.ssid");
	string_init(varname_wlan_ap_passwd, "wlan.ap.passwd");
	string_init(varname_wlan_ap_channel, "wlan.ap.channel");

	if(config_get_int(&varname_wlan_mode, -1, -1, &wlan_mode_int))
		wlan_mode = (config_wlan_mode_t)wlan_mode_int;
	else
		wlan_mode = config_wlan_mode_client;

	switch(wlan_mode)
	{
		case(config_wlan_mode_client):
		{
			if(!config_get_string(&varname_wlan_client_ssid, -1, -1, &string_ssid))
			{
				string_clear(&string_ssid);
				string_append(&string_ssid, "esp");
			}

			if(!config_get_string(&varname_wlan_client_passwd, -1, -1, &string_passwd))
			{
				string_clear(&string_passwd);
				string_append(&string_passwd, "espespesp");
			}

			if((wifi_get_opmode() != STATION_MODE) ||
					!wifi_station_get_config(&cconf) ||
					!wifi_station_get_auto_connect() ||
					!string_match_cstr(&string_ssid, cconf.ssid) ||
					!string_match_cstr(&string_passwd, cconf.password))
			{
				memset(&cconf, 0, sizeof(cconf));
				strecpy(cconf.ssid, string_to_cstr(&string_ssid), sizeof(cconf.ssid));
				strecpy(cconf.password, string_to_cstr(&string_passwd), sizeof(cconf.password));
				cconf.bssid_set = 0;

				logfmt("* set wlan mode to client, ssid=\"%s\", passwd=\"%s\" and reconnect\n", cconf.ssid, cconf.password);

				wifi_station_disconnect();
				wifi_set_opmode(STATION_MODE);
				wifi_station_set_config(&cconf);
				wifi_station_connect();
				wifi_station_set_auto_connect(1);
			}
			else
				logfmt("* wlan mode is client, ssid=\"%s\", passwd=\"%s\"\n", cconf.ssid, cconf.password);

			break;
		}

		case(config_wlan_mode_ap):
		{
			memset(&saconf, 0, sizeof(saconf));

			if(config_get_string(&varname_wlan_ap_ssid, -1, -1, &string_ssid))
				strecpy(saconf.ssid, string_to_cstr(&string_ssid), sizeof(saconf.ssid));
			else
				strecpy(saconf.ssid, "esp", sizeof(saconf.ssid));

			if(config_get_string(&varname_wlan_ap_passwd, -1, -1, &string_passwd))
				strecpy(saconf.password, string_to_cstr(&string_passwd), sizeof(saconf.password));
			else
				strecpy(saconf.password, "espespesp", sizeof(saconf.password));

			if(!config_get_int(&varname_wlan_ap_channel, -1, -1, &channel))
				channel = 1;

			saconf.ssid_len = strlen(saconf.ssid);
			saconf.channel = channel;
			saconf.authmode = AUTH_WPA_WPA2_PSK;
			saconf.ssid_hidden = 0;
			saconf.max_connection = 1;
			saconf.beacon_interval = 100;

			logfmt("* set wlan mode to ap, ssid=\"%s\", passwd=\"%s\", channel=%d\r\n",
					saconf.ssid, saconf.password, saconf.channel);

			wifi_station_disconnect();
			wifi_set_opmode_current(SOFTAP_MODE);
			wifi_softap_set_config_current(&saconf);

			break;
		}

		default:
		{
			return(false);
		}
	}

	return(true);
}
