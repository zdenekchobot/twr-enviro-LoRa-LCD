/*
TOWER configuration:
    CORE module NR (temperature, acceleration, security chip)
    LCD module - LCD, 2x button, 6x RGB mini LED, gesture & proximity sensor
    LoRa module
    CO2 module
    Barometer tag
    Humidity tag
    VOC-LP tag
    Mini battery module
*/
#include <application.h>
#include <twr_at_lora.h>

#define SEND_DATA_INTERVAL (10 * 60 * 1000)
#define HUMIDITY_UPDATE_INTERVAL (1 * 60 * 1000)
#define CO2_UPDATE_INTERVAL (2 * 60 * 1000)
#define TVOC_UPDATE_INTERVAL (5 * 60 * 1000)
#define PRESSURE_UPDATE_INTERVAL (5 * 60 * 1000)
#define BATTERY_UPDATE_INTERVAL (5 * 60 * 1000)

#define CO2_CALIBRATION_DELAY (2 * 60 * 1000)
#define CO2_CALIBRATION_INTERVAL (1 * 60 * 1000)
#define CO2_UPDATE_SERVICE_INTERVAL (1 * 60 * 1000)

#define MAX_PAGE_INDEX 3

#define PAGE_INDEX_MENU -1

// LED instance
twr_led_t ledr;
twr_led_t ledg;
twr_led_t ledb;

/* Button instance
twr_button_t button_left;
twr_button_t button_right;
*/

// Lora instance
twr_cmwx1zzabz_t lora;

/* Accelerometer instance
twr_lis2dh12_t lis2dh12;
twr_dice_t dice;
twr_dice_face_t dice_face = TWR_DICE_FACE_1;
*/

/* On-board thermometer instance
twr_tmp112_t tmp112;
*/

// Humidity tag instance
twr_tag_humidity_t humi_tag;

// VOC tag instance
twr_tag_voc_lp_t voc_tag;

// Barometer tag instance
twr_tag_barometer_t bar_tag;

// GFX pointer
twr_gfx_t *pgfx;

static struct
{
    float_t temperature;
    float_t humidity;
    float_t pressure;
    float_t co2;
    float_t tvoc;
    float_t battery_voltage;
    float_t battery_pct;

} values;

static const struct
{
    char *name0;
    char *format0;
    float_t *value0;
    char *unit0;
    char *name1;
    char *format1;
    float_t *value1;
    char *unit1;

} pages[] = {
    {"Temperature   ", "%.1f", &values.temperature, " \xb0"
                                                    "C",
     "Humidity      ", "%.0f", &values.humidity, " %"},
    {"CO2           ", "%.0f", &values.co2, " ppm",
     "TVOC          ", "%.0f", &values.tvoc, " ppb"},
    {"Air pressure  ", "%.0f", &values.pressure, " hPa",
     "", "%.0f", 0, ""},
    {"Battery       ", "%.2f", &values.battery_voltage, "V",
     "Battery       ", "%.0f", &values.battery_pct, " %"},
};

static int page_index = 0;
static int menu_item = 0;
bool active_mode = true;
int calibration_counter;

TWR_DATA_STREAM_FLOAT_BUFFER(sm_voltage_buffer, SEND_DATA_INTERVAL / BATTERY_UPDATE_INTERVAL)
TWR_DATA_STREAM_FLOAT_BUFFER(sm_percentage_buffer, SEND_DATA_INTERVAL / BATTERY_UPDATE_INTERVAL)
TWR_DATA_STREAM_FLOAT_BUFFER(sm_temperature_buffer, (SEND_DATA_INTERVAL / HUMIDITY_UPDATE_INTERVAL))
TWR_DATA_STREAM_FLOAT_BUFFER(sm_humidity_buffer, (SEND_DATA_INTERVAL / HUMIDITY_UPDATE_INTERVAL))
TWR_DATA_STREAM_FLOAT_BUFFER(sm_pressure_buffer, (SEND_DATA_INTERVAL / PRESSURE_UPDATE_INTERVAL))
TWR_DATA_STREAM_FLOAT_BUFFER(sm_co2_buffer, (SEND_DATA_INTERVAL / CO2_UPDATE_INTERVAL))
TWR_DATA_STREAM_FLOAT_BUFFER(sm_voc_buffer, (SEND_DATA_INTERVAL / TVOC_UPDATE_INTERVAL))

twr_data_stream_t sm_voltage;
twr_data_stream_t sm_percentage;
twr_data_stream_t sm_temperature;
twr_data_stream_t sm_humidity;
twr_data_stream_t sm_co2;
twr_data_stream_t sm_voc;
twr_data_stream_t sm_pressure;

twr_scheduler_task_id_t calibration_task_id;

enum
{
    HEADER_BOOT = 0x00,
    HEADER_UPDATE = 0x01,
    HEADER_BUTTON_CLICK = 0x02,
    HEADER_BUTTON_HOLD = 0x03,

} header = HEADER_BOOT;

void calibration_task(void *param);

void calibration_start()
{
    calibration_counter = 32;

    twr_led_set_mode(&ledb, TWR_LED_MODE_BLINK_SLOW);
    calibration_task_id = twr_scheduler_register(calibration_task, NULL, twr_tick_get() + CO2_CALIBRATION_DELAY);
    twr_log_debug("Start CO2 calibration");
}

void calibration_stop()
{
    twr_led_set_mode(&ledb, TWR_LED_MODE_OFF);
    twr_scheduler_unregister(calibration_task_id);
    calibration_task_id = 0;

    twr_module_co2_set_update_interval(CO2_UPDATE_INTERVAL);
    twr_log_debug("Stop CO2 calibration");
}

void calibration_task(void *param)
{
    (void)param;

    twr_led_set_mode(&ledb, TWR_LED_MODE_BLINK_FAST);

    twr_log_debug("CO2 calibration %i", calibration_counter);

    twr_module_co2_set_update_interval(CO2_UPDATE_SERVICE_INTERVAL);
    twr_module_co2_calibration(TWR_LP8_CALIBRATION_BACKGROUND_FILTERED);

    calibration_counter--;

    if (calibration_counter == 0)
    {
        calibration_stop();
    }

    twr_scheduler_plan_current_relative(CO2_CALIBRATION_INTERVAL);
}

static void lcd_page_render()
{
    int w;
    char str[32];

    twr_system_pll_enable();

    twr_gfx_clear(pgfx);

    if ((page_index <= MAX_PAGE_INDEX) && (page_index != PAGE_INDEX_MENU))
    {
        twr_gfx_set_font(pgfx, &twr_font_ubuntu_15);
        twr_gfx_draw_string(pgfx, 10, 5, pages[page_index].name0, true);

        twr_gfx_set_font(pgfx, &twr_font_ubuntu_28);
        snprintf(str, sizeof(str), pages[page_index].format0, *pages[page_index].value0);
        w = twr_gfx_draw_string(pgfx, 25, 25, str, true);
        twr_gfx_set_font(pgfx, &twr_font_ubuntu_15);
        w = twr_gfx_draw_string(pgfx, w, 35, pages[page_index].unit0, true);

        twr_gfx_set_font(pgfx, &twr_font_ubuntu_15);
        twr_gfx_draw_string(pgfx, 10, 55, pages[page_index].name1, true);

        twr_gfx_set_font(pgfx, &twr_font_ubuntu_28);
        snprintf(str, sizeof(str), pages[page_index].format1, *pages[page_index].value1);
        w = twr_gfx_draw_string(pgfx, 25, 75, str, true);
        twr_gfx_set_font(pgfx, &twr_font_ubuntu_15);
        twr_gfx_draw_string(pgfx, w, 85, pages[page_index].unit1, true);
    }

    snprintf(str, sizeof(str), "%d/%d", page_index + 1, MAX_PAGE_INDEX + 1);
    twr_gfx_set_font(pgfx, &twr_font_ubuntu_13);
    twr_gfx_draw_string(pgfx, 55, 115, str, true);

    twr_system_pll_disable();
}

void lcd_draw()
{
    if (!twr_gfx_display_is_ready(pgfx))
    {
        return;
    }

    if (active_mode)
    {
        lcd_page_render();
    }
    else
    {
        twr_scheduler_plan_current_relative(500);
    }

    twr_gfx_update(pgfx);
}

/* void button_event_handler(twr_button_t *self, twr_button_event_t event, void *event_param)
{
    if (event == TWR_BUTTON_EVENT_CLICK && event_param == 0)
    {
        header = HEADER_BUTTON_CLICK;

        twr_scheduler_plan_now(0);
    }
    else if (event == TWR_BUTTON_EVENT_HOLD)
    {
        header = HEADER_BUTTON_HOLD;

        twr_scheduler_plan_now(0);
    }
    else if (event == TWR_BUTTON_EVENT_CLICK && (int)event_param == 1)
    {
        twr_led_set_mode(&ledr, TWR_LED_MODE_TOGGLE);
    }
}
*/

void lcd_event_handler(twr_module_lcd_event_t event, void *event_param)
{
    (void)event_param;

    if (event == TWR_MODULE_LCD_EVENT_LEFT_CLICK)
    {
        if ((page_index != PAGE_INDEX_MENU))
        {
            // Key previous page
            page_index--;
            if (page_index < 0)
            {
                page_index = MAX_PAGE_INDEX;
                menu_item = 0;
            }
        }
        else
        {
            // Key menu down
            menu_item++;
            if (menu_item > 4)
            {
                menu_item = 0;
            }
        }

        static uint16_t left_event_count = 0;
        left_event_count++;
        lcd_draw();
    }
    else if (event == TWR_MODULE_LCD_EVENT_BOTH_HOLD)
    {
        static int both_hold_event_count = 0;
        both_hold_event_count++;
        twr_led_pulse(&ledr, 200);
        /* active_mode = !active_mode;
        if (active_mode)
        {
            lcd_draw();
        }
        else
        {
            twr_gfx_clear(pgfx);
            twr_gfx_update(pgfx);
        }
        */
    }
    else if (event == TWR_MODULE_LCD_EVENT_RIGHT_CLICK)
    {
        if ((page_index != PAGE_INDEX_MENU) || (menu_item == 0))
        {
            // Key next page
            page_index++;
            if (page_index > MAX_PAGE_INDEX)
            {
                page_index = 0;
            }
            if (page_index == PAGE_INDEX_MENU)
            {
                menu_item = 0;
            }
        }

        static uint16_t right_event_count = 0;
        right_event_count++;
        lcd_draw();
    }
    else if (event == TWR_MODULE_LCD_EVENT_LEFT_HOLD)
    {
        static int left_hold_event_count = 0;
        left_hold_event_count++;
        twr_led_set_mode(&ledg, TWR_LED_MODE_ON);
        twr_scheduler_plan_now(0);
    }
    else if (event == TWR_MODULE_LCD_EVENT_RIGHT_HOLD)
    {
        static int right_hold_event_count = 0;
        right_hold_event_count++;
        if (!calibration_task_id)
        {
            calibration_start();
        }
        else
        {
            calibration_stop();
        }
    }
}

/* void tmp112_event_handler(twr_tmp112_t *self, twr_tmp112_event_t event, void *event_param)
{
    float value = NAN;
    if (event == TWR_TMP112_EVENT_UPDATE)
    {
        twr_tmp112_get_temperature_celsius(self, &value);
        twr_log_debug("CORE: Temperature: %.1f ??C", value);
    }
}
*/

void tag_humidity_event_handler(twr_tag_humidity_t *self, twr_tag_humidity_event_t event, void *event_param)
{
    float temperature = NAN;
    float humidity = NAN;
    if (event == TWR_TAG_HUMIDITY_EVENT_UPDATE)
    {
        if (twr_tag_humidity_get_humidity_percentage(self, &humidity))
        {
            twr_data_stream_feed(&sm_humidity, &humidity);
            values.humidity = humidity;
            lcd_draw();
            twr_log_debug("HUMIDITY TAG: Humidity: %.0f %%", humidity);
        }
        else
        {
            twr_log_debug("HUMIDITY TAG: Invalid humidity value");
        }

        if (twr_tag_humidity_get_temperature_celsius(self, &temperature))
        {
            twr_data_stream_feed(&sm_temperature, &temperature);
            values.temperature = temperature;
            lcd_draw();
            twr_log_debug("HUMIDITY TAG: Temperature: %.1f ??C", temperature);
        }
        else
        {
            twr_log_debug("HUMIDITY TAG: Temperature value invalid");
        }
    }
}

void tag_voc_event_handler(twr_tag_voc_lp_t *self, twr_tag_voc_lp_event_t event, void *event_param)
{

    if (event == TWR_TAG_VOC_LP_EVENT_UPDATE)
    {
        uint16_t value;

        if (twr_tag_voc_lp_get_tvoc_ppb(self, &value))
        {
            float retyped_value = (float)value;
            twr_data_stream_feed(&sm_voc, &retyped_value);
            values.tvoc = value;
            lcd_draw();
            twr_log_debug("VOC TAG: TVOC: %i ppb", value);
        }
        else
        {
            twr_log_debug("VOC TAG: Invalid TVOC value");
        }
    }
}

void tag_barometer_event_handler(twr_tag_barometer_t *self, twr_tag_barometer_event_t event, void *event_param)
{
    if (event == TWR_TAG_BAROMETER_EVENT_UPDATE)
    {
        float pressure = NAN;
        if (twr_tag_barometer_get_pressure_pascal(self, &pressure))
        {
            pressure /= 100; // Pa to hPa
            twr_data_stream_feed(&sm_pressure, &pressure);
            values.pressure = pressure;
            lcd_draw();
            twr_log_debug("BAROMETER TAG: Air pressure: %.0f hPa", pressure);
        }
        else
        {
            twr_log_debug("BAROMETER TAG: Invalid air pressure value");
        }
    }
}

void battery_event_handler(twr_module_battery_event_t event, void *event_param)
{
    if (event == TWR_MODULE_BATTERY_EVENT_UPDATE)
    {
        float value = NAN;
        int percentage;
        if (twr_module_battery_get_voltage(&value))
        {
            twr_data_stream_feed(&sm_voltage, &value);
            twr_log_debug("BATTERY MODULE: Voltage %.2f V", value);
            values.battery_voltage = value;
            lcd_draw();
        }
        else
        {
            twr_log_debug("BATTERY MODULE: Invalid voltage value");
        }
        if (twr_module_battery_get_charge_level(&percentage))
        {
            float retyped_percentage = (float)percentage;
            twr_data_stream_feed(&sm_percentage, &retyped_percentage);
            twr_log_debug("BATTERY MODULE: Charge level %i %%", percentage);
            values.battery_pct = percentage;
            lcd_draw();
        }
        else
        {
            twr_log_debug("BATTERY MODULE: Invalid charge level value");
        }
    }
}

void co2_module_event_handler(twr_module_co2_event_t event, void *event_param)
{
    if (event == TWR_MODULE_CO2_EVENT_UPDATE)
    {
        float value = NAN;
        if (twr_module_co2_get_concentration_ppm(&value))
        {
            twr_data_stream_feed(&sm_co2, &value);
            values.co2 = value;
            lcd_draw();
            twr_log_debug("CO2 MODULE: CO2: %.1f ppm", value);
        }
        else
        {
            twr_log_debug("CO2 MODULE: CO2 measure failed");
        }
    }
}

/* void lis2dh12_event_handler(twr_lis2dh12_t *self, twr_lis2dh12_event_t event, void *event_param)
{
    if (event == TWR_LIS2DH12_EVENT_UPDATE)
    {
        twr_lis2dh12_result_g_t g;

        if (twr_lis2dh12_get_result_g(self, &g))
        {
            twr_dice_feed_vectors(&dice, g.x_axis, g.y_axis, g.z_axis);

            int orientation = (int)twr_dice_get_face(&dice);

            twr_data_stream_feed(&sm_orientation, &orientation);
        }
    }
}
*/

/* void battery_measure_task(void *param)
{
    if (!twr_module_battery_measure())
    {
        twr_scheduler_plan_current_now();
    }
}
*/

void lora_callback(twr_cmwx1zzabz_t *self, twr_cmwx1zzabz_event_t event, void *event_param)
{
    if (event == TWR_CMWX1ZZABZ_EVENT_ERROR)
    {
        twr_led_set_mode(&ledr, TWR_LED_MODE_BLINK_FAST);
    }
    else if (event == TWR_CMWX1ZZABZ_EVENT_SEND_MESSAGE_START)
    {
        // twr_led_set_mode(&ledg, TWR_LED_MODE_ON);
    }
    else if (event == TWR_CMWX1ZZABZ_EVENT_SEND_MESSAGE_DONE)
    {
        twr_led_set_mode(&ledr, TWR_LED_MODE_OFF);
        twr_led_set_mode(&ledg, TWR_LED_MODE_OFF);
    }
    else if (event == TWR_CMWX1ZZABZ_EVENT_READY)
    {
        twr_led_set_mode(&ledr, TWR_LED_MODE_OFF);
    }
    else if (event == TWR_CMWX1ZZABZ_EVENT_JOIN_SUCCESS)
    {
        twr_atci_printfln("$JOIN_OK");
    }
    else if (event == TWR_CMWX1ZZABZ_EVENT_JOIN_ERROR)
    {
        twr_atci_printfln("$JOIN_ERROR");
    }
    else if (event == TWR_CMWX1ZZABZ_EVENT_MESSAGE_RECEIVED)
    {
        twr_log_debug("Prijata zprava");
        uint32_t message_lenght;
        message_lenght = twr_cmwx1zzabz_get_received_message_length(&lora);
        if (message_lenght > 0)
        {
            uint8_t buffer[message_lenght];
            twr_cmwx1zzabz_get_received_message_data(&lora, buffer, message_lenght);
            twr_atci_printfln("LoRa received msg: %s", buffer);
        }
    }
}

bool at_send(void)
{
    twr_scheduler_plan_now(0);

    return true;
}

bool at_status(void)
{
    float value_avg = NAN;

    static const struct
    {
        twr_data_stream_t *stream;
        const char *name;
        int precision;
    } values[] = {
        {&sm_voltage, "Voltage", 2},
        {&sm_percentage, "Charge level", 0},
        {&sm_temperature, "Temperature", 1},
        {&sm_humidity, "Humidity", 0},
        {&sm_pressure, "Air pressure", 0},
        {&sm_co2, "CO2", 0},
        {&sm_voc, "VOC", 0},
    };

    for (size_t i = 0; i < sizeof(values) / sizeof(values[0]); i++)
    {
        value_avg = NAN;

        if (twr_data_stream_get_average(values[i].stream, &value_avg))
        {
            twr_atci_printfln("%s: %.*f", values[i].name, values[i].precision, value_avg);
        }
        else
        {
            twr_atci_printfln("%s: -", values[i].name);
        }
    }

    /* int orientation;

    if (twr_data_stream_get_median(&sm_orientation, &orientation))
    {
        twr_atci_printf("$STATUS: \"Orientation\",%d\n", orientation);
    }
    else
    {
        twr_atci_printf("$STATUS: \"Orientation\",\n", orientation);
    }
    */

    return true;
}

void application_init(void)
{
    // Initialize logging
    twr_log_init(TWR_LOG_LEVEL_DUMP, TWR_LOG_TIMESTAMP_ABS);

    twr_data_stream_init(&sm_voltage, 1, &sm_voltage_buffer);
    twr_data_stream_init(&sm_percentage, 1, &sm_percentage_buffer);
    twr_data_stream_init(&sm_temperature, 1, &sm_temperature_buffer);
    twr_data_stream_init(&sm_humidity, 1, &sm_humidity_buffer);
    twr_data_stream_init(&sm_pressure, 1, &sm_pressure_buffer);
    twr_data_stream_init(&sm_co2, 1, &sm_co2_buffer);
    twr_data_stream_init(&sm_voc, 1, &sm_voc_buffer);

    // Initialize LED
    const twr_led_driver_t *driver = twr_module_lcd_get_led_driver();
    twr_led_init_virtual(&ledg, TWR_MODULE_LCD_LED_GREEN, driver, 1);
    twr_led_init_virtual(&ledr, TWR_MODULE_LCD_LED_RED, driver, 1);
    twr_led_init_virtual(&ledb, TWR_MODULE_LCD_LED_BLUE, driver, 1);
    // twr_led_set_mode(&led, TWR_LED_MODE_ON);

    /* Initialize button
    const twr_button_driver_t *lcdButtonDriver = twr_module_lcd_get_button_driver();
    twr_button_init_virtual(&button_left, 0, lcdButtonDriver, 0);
    twr_button_init_virtual(&button_right, 1, lcdButtonDriver, 0);
    twr_button_set_event_handler(&button_left, button_event_handler, (int *)0);
    twr_button_set_event_handler(&button_right, button_event_handler, (int *)1);
    */

    // Initialize LCD module
    twr_module_lcd_init();
    twr_module_lcd_set_event_handler(lcd_event_handler, NULL);
    twr_module_lcd_set_button_hold_time(1000);
    pgfx = twr_module_lcd_get_gfx();

    /* Initialize TMP112 - thermometer on Core board
    twr_tmp112_init(&tmp112, TWR_I2C_I2C0, 0x49);
    twr_tmp112_set_update_interval(&tmp112, MEASURE_INTERVAL);
    twr_tmp112_set_event_handler(&tmp112, tmp112_event_handler, NULL);
    */

    // Initialize humidity tag
    twr_tag_humidity_init(&humi_tag, TWR_TAG_HUMIDITY_REVISION_R3, TWR_I2C_I2C0, 0x40);
    twr_tag_humidity_set_event_handler(&humi_tag, tag_humidity_event_handler, NULL);
    twr_tag_humidity_set_update_interval(&humi_tag, HUMIDITY_UPDATE_INTERVAL);

    // Initialize VOC tag
    twr_tag_voc_lp_init(&voc_tag, TWR_I2C_I2C0);
    twr_tag_voc_lp_set_event_handler(&voc_tag, tag_voc_event_handler, NULL);
    twr_tag_voc_lp_set_update_interval(&voc_tag, TVOC_UPDATE_INTERVAL);

    // Intitialize BAROMETER TAG
    twr_tag_barometer_init(&bar_tag, TWR_I2C_I2C0);
    twr_tag_barometer_set_event_handler(&bar_tag, tag_barometer_event_handler, NULL);
    twr_tag_barometer_set_update_interval(&bar_tag, PRESSURE_UPDATE_INTERVAL);

    // Initialize CO2 module
    twr_module_co2_init();
    twr_module_co2_set_event_handler(co2_module_event_handler, NULL);
    twr_module_co2_set_update_interval(CO2_UPDATE_INTERVAL);

    // Initialize battery
    twr_module_battery_init();
    twr_module_battery_set_event_handler(battery_event_handler, NULL);
    twr_module_battery_set_threshold_levels((4 * 1.7), (4 * 1.6));
    twr_module_battery_set_update_interval(BATTERY_UPDATE_INTERVAL);
    // battery_measure_task_id = twr_scheduler_register(battery_measure_task, NULL, 2020);

    /* Initialize accelerometer
    twr_dice_init(&dice, TWR_DICE_FACE_UNKNOWN);
    twr_lis2dh12_init(&lis2dh12, TWR_I2C_I2C0, 0x19);
    twr_lis2dh12_set_resolution(&lis2dh12, TWR_LIS2DH12_RESOLUTION_8BIT);
    twr_lis2dh12_set_event_handler(&lis2dh12, lis2dh12_event_handler, NULL);
    twr_lis2dh12_set_update_interval(&lis2dh12, MEASURE_INTERVAL);
    */

    // Initialize lora module
    twr_cmwx1zzabz_init(&lora, TWR_UART_UART1);
    twr_cmwx1zzabz_set_event_handler(&lora, lora_callback, NULL);
    twr_cmwx1zzabz_set_class(&lora, TWR_CMWX1ZZABZ_CONFIG_CLASS_A);

    // Initialize AT command interface
    twr_at_lora_init(&lora);
    static const twr_atci_command_t commands[] = {
        TWR_AT_LORA_COMMANDS,
        {"$SEND", at_send, NULL, NULL, NULL, "Immediately send packet"},
        {"$STATUS", at_status, NULL, NULL, NULL, "Show status"},
        TWR_ATCI_COMMAND_CLAC,
        TWR_ATCI_COMMAND_HELP};
    twr_atci_init(commands, TWR_ATCI_COMMANDS_LENGTH(commands));

    twr_module_battery_measure();
    twr_tag_humidity_measure(&humi_tag);
    twr_tag_barometer_measure(&bar_tag);

    twr_scheduler_plan_current_relative(10 * 1000);
}

void application_task(void)
{
    if (!twr_cmwx1zzabz_is_ready(&lora))
    {
        twr_scheduler_plan_current_relative(100);

        return;
    }

    static uint8_t buffer[12];
    memset(buffer, 0xff, sizeof(buffer));
    buffer[0] = header;

    float voltage_avg = 0;
    twr_data_stream_get_average(&sm_voltage, &voltage_avg);
    if (!isnan(voltage_avg))
    {
        buffer[1] = ceil(voltage_avg * 30.f);
    }

    float percentage_avg = 0;
    twr_data_stream_get_average(&sm_percentage, &percentage_avg);
    if (!isnan(percentage_avg))
    {

        buffer[2] = percentage_avg;
    }

    float temperature_avg = NAN;
    twr_data_stream_get_average(&sm_temperature, &temperature_avg);
    if (!isnan(temperature_avg))
    {
        int16_t temperature_i16 = (int16_t)(temperature_avg * 10.f);

        buffer[3] = temperature_i16 >> 8;
        buffer[4] = temperature_i16;
    }

    float humidity_avg = NAN;
    twr_data_stream_get_average(&sm_humidity, &humidity_avg);
    if (!isnan(humidity_avg))
    {
        buffer[5] = humidity_avg * 2;
    }

    float co2_avg = NAN;
    twr_data_stream_get_average(&sm_co2, &co2_avg);
    if (!isnan(co2_avg))
    {
        if (co2_avg > 65534)
        {
            co2_avg = 65534;
        }
        uint16_t value = (uint16_t)co2_avg;
        buffer[6] = value >> 8;
        buffer[7] = value;
    }

    float voc_avg = NAN;
    twr_data_stream_get_average(&sm_voc, &voc_avg);
    if (!isnan(voc_avg))
    {
        uint16_t value = (uint16_t)voc_avg;
        buffer[8] = value >> 8;
        buffer[9] = value;
    }

    float pressure_avg = NAN;
    twr_data_stream_get_average(&sm_pressure, &pressure_avg);
    if (!isnan(pressure_avg))
    {
        uint16_t value = (uint16_t)pressure_avg;
        buffer[10] = value >> 8;
        buffer[11] = value;
    }

    twr_cmwx1zzabz_send_message(&lora, buffer, sizeof(buffer));
    static char tmp[sizeof(buffer) * 2 + 1];
    for (size_t i = 0; i < sizeof(buffer); i++)
    {
        sprintf(tmp + i * 2, "%02x", buffer[i]);
    }
    twr_atci_printfln("$SEND: %s", tmp);

    header = HEADER_UPDATE;

    lcd_draw();

    twr_scheduler_plan_current_relative(SEND_DATA_INTERVAL);
}
