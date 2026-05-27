#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "pico/stdio.h"
#include "hardware/spi.h"
#include "hardware/uart.h"
#include "bsp/board_api.h"

#include "st7789.h"
 
#include "tusb.h"
#include "device/usbd.h"
#include "tusb_config.h"
 
#define LORA_MODE0_PIN 2
#define LORA_MODE1_PIN 3

#define LCD_BL_PIN -1
#define BUTTON1_PIN 13
#define BUTTON2_PIN 14
 
#define UART_ID     uart0
#define UART_IRQ_ID UART0_IRQ
#define BAUD_RATE   9600
#define UART_TX_PIN 0
#define UART_RX_PIN 1
#define DATA_BITS   8 // 8N1
#define PARITY      UART_PARITY_NONE
#define STOP_BITS   1

#define USB_ENDPOINT_DEBUG 0
#define USB_ENDPOINT_DATA 1

#define LCD_BLACK 0x0000
#define LCD_WHITE 0xffff
#define LCD_GREEN 0x07e0
#define LCD_BLUE  0x001f
 
typedef enum _transceiver_mode {
    RECEIVER,
    TRANSMITTER,
} transceiver_mode;
 
const struct st7789_config lcd_config = {
    .spi = spi1,
    .gpio_din = 11,
    .gpio_clk = 10,
    .gpio_cs = 9,
    .gpio_dc = 8,
    .gpio_rst = 12,
    .gpio_bl = LCD_BL_PIN,
    .madctl = 0x60,
    .x_offset = 40,
    .y_offset = 53,
};
 
// LCD rotation {0x60, 240, 135, 40, 53},
const uint16_t lcd_width = 240;
const uint16_t lcd_height = 135;
 
uint8_t buffer[128];
transceiver_mode mode = RECEIVER;

const uint8_t RX_START_BYTE = 0xFF;
const uint8_t RX_END_BYTE = 0xEE;
const uint32_t RX_PAYLOAD_SIZE = 160;
const uint32_t UART_RING_SIZE = 256;
const uint32_t RX_RATE_WINDOW_SECONDS = 10;
const uint32_t CAPNP_DATA_OFFSET = 16;
const uint32_t TS_DATA_TIME_SECONDS_OFFSET = 0;
const uint32_t TS_DATA_FUEL_CELL_OUTPUT_VOLTAGE_INDEX = 6;
const uint16_t TEXT_MARGIN_X = 6;
const uint16_t RATE_TEXT_Y = 12;
const uint16_t VALUE_TEXT_X = 6;
const uint16_t FC_V_TEXT_Y = 52;
const uint16_t TIME_TEXT_Y = 82;
const uint16_t MODE_ROW_Y = 118;
const uint16_t MODE_TEXT_Y = 121;
const uint16_t RECEIVER_MODE_TEXT_X = 6;
const uint16_t TRANSMITTER_MODE_TEXT_X = 138;
const uint32_t BUTTON_DEBOUNCE_MS = 50;

bool rx_found_start = false;
uint8_t rx_buffer[RX_PAYLOAD_SIZE];
uint32_t current_frame_byte_index = 0;
volatile uint8_t uart_ring[UART_RING_SIZE];
volatile uint32_t uart_ring_read_index = 0;
volatile uint32_t uart_ring_write_index = 0;
volatile uint32_t uart_ring_dropped_bytes = 0;
uint32_t rx_bytes_this_second = 0;
uint32_t rx_bytes_window[RX_RATE_WINDOW_SECONDS] = { 0 };
uint32_t rx_bytes_window_index = 0;
uint32_t rx_bytes_last_10s = 0;
float latest_fuel_cell_voltage = 0.0f;
uint64_t latest_frame_time_seconds = 0;
bool latest_frame_valid = false;

void on_uart_rx() {
    while (uart_is_readable(UART_ID)) {
        uint32_t next_write_index = (uart_ring_write_index + 1) % UART_RING_SIZE;
        uint8_t byte = uart_getc(UART_ID);

        if (next_write_index == uart_ring_read_index)
        {
            uart_ring_dropped_bytes++;
            continue;
        }

        uart_ring[uart_ring_write_index] = byte;
        uart_ring_write_index = next_write_index;
    }
}

bool uart_ring_pop(uint8_t *byte)
{
    if (uart_ring_read_index == uart_ring_write_index)
    {
        return false;
    }

    *byte = uart_ring[uart_ring_read_index];
    uart_ring_read_index = (uart_ring_read_index + 1) % UART_RING_SIZE;
    return true;
}

float read_le_float(const uint8_t *data)
{
    float value;
    memcpy(&value, data, sizeof(value));
    return value;
}

uint64_t read_le_u64(const uint8_t *data)
{
    uint64_t value;
    memcpy(&value, data, sizeof(value));
    return value;
}

float read_ts_data_float(uint32_t field_index)
{
    return read_le_float(&rx_buffer[CAPNP_DATA_OFFSET + field_index * sizeof(float)]);
}

uint64_t read_ts_data_u64(uint32_t field_offset)
{
    return read_le_u64(&rx_buffer[CAPNP_DATA_OFFSET + field_offset]);
}

bool process_telemetry_byte(uint8_t byte)
{
    if (!rx_found_start)
    {
        rx_found_start = byte == RX_START_BYTE;
        current_frame_byte_index = 0;
        return false;
    }

    if (current_frame_byte_index < RX_PAYLOAD_SIZE)
    {
        rx_buffer[current_frame_byte_index++] = byte;
        return false;
    }

    bool frame_complete = false;
    if (byte == RX_END_BYTE)
    {
        latest_frame_time_seconds = read_ts_data_u64(TS_DATA_TIME_SECONDS_OFFSET);
        latest_fuel_cell_voltage = read_ts_data_float(TS_DATA_FUEL_CELL_OUTPUT_VOLTAGE_INDEX);
        latest_frame_valid = true;
        frame_complete = true;
    }

    rx_found_start = false;
    current_frame_byte_index = 0;
    return frame_complete;
}

void format_fixed_1(char *buffer, size_t buffer_size, float value)
{
    int scaled = (int)(value * 10.0f);
    int whole = scaled / 10;
    int frac = scaled % 10;

    if (frac < 0)
    {
        frac = -frac;
    }

    snprintf(buffer, buffer_size, "%d.%d", whole, frac);
}

void format_time_of_day(char *buffer, size_t buffer_size, uint64_t unix_seconds)
{
    uint32_t seconds_in_day = unix_seconds % (24 * 60 * 60);
    uint32_t hours = seconds_in_day / (60 * 60);
    uint32_t minutes = (seconds_in_day / 60) % 60;
    uint32_t seconds = seconds_in_day % 60;

    snprintf(buffer, buffer_size, "%02lu:%02lu:%02lu", (unsigned long)hours, (unsigned long)minutes,
        (unsigned long)seconds);
}

uint32_t update_rx_bytes_window()
{
    rx_bytes_last_10s -= rx_bytes_window[rx_bytes_window_index];
    rx_bytes_window[rx_bytes_window_index] = rx_bytes_this_second;
    rx_bytes_last_10s += rx_bytes_this_second;
    rx_bytes_this_second = 0;
    rx_bytes_window_index = (rx_bytes_window_index + 1) % RX_RATE_WINDOW_SECONDS;

    return rx_bytes_last_10s;
}

void log_reception_status(uint32_t bytes_per_10s)
{
    char time_value[16];

    if (latest_frame_valid)
    {
        format_time_of_day(time_value, sizeof(time_value), latest_frame_time_seconds);
        printf("[printf] Periodic echo - reception - RX_BYTES: %lu B/10S, TIME: %s\r\n",
            (unsigned long)bytes_per_10s, time_value);
    }
    else
    {
        printf("[printf] Periodic echo - reception - RX_BYTES: %lu B/10S\r\n", (unsigned long)bytes_per_10s);
    }
}

void draw_mode_row()
{
    st7789_fill_rect(0, MODE_ROW_Y, lcd_width, 17, LCD_BLACK);
    if (mode == RECEIVER)
    {
        st7789_draw_text(RECEIVER_MODE_TEXT_X, MODE_TEXT_Y, "RECEIVER MODE", LCD_GREEN, LCD_BLACK, 1);
    }
    else
    {
        st7789_draw_text(TRANSMITTER_MODE_TEXT_X, MODE_TEXT_Y, "TRANSMITTER MODE", LCD_GREEN, LCD_BLACK, 1);
    }
}

void clear_telemetry_values()
{
    st7789_fill_rect(0, 46, lcd_width, 70, LCD_BLACK);
    latest_frame_valid = false;
}

void redraw_status(uint32_t bytes_per_10s)
{
    char line[32];
    char value[16];

    st7789_fill_rect(0, 0, lcd_width, 34, LCD_BLACK);
    st7789_fill_rect(0, 34, lcd_width, 4, LCD_GREEN);
    snprintf(line, sizeof(line), "RX_BYTES: %lu B/10S", (unsigned long)bytes_per_10s);
    st7789_draw_text(TEXT_MARGIN_X, RATE_TEXT_Y, line, LCD_WHITE, LCD_BLACK, 1);

    st7789_fill_rect(0, 46, lcd_width, 70, LCD_BLACK);
    if (mode == RECEIVER && latest_frame_valid)
    {
        format_fixed_1(value, sizeof(value), latest_fuel_cell_voltage);
        snprintf(line, sizeof(line), "FC_V: %s V", value);
        st7789_draw_text(VALUE_TEXT_X, FC_V_TEXT_Y, line, LCD_WHITE, LCD_BLACK, 2);

        format_time_of_day(value, sizeof(value), latest_frame_time_seconds);
        snprintf(line, sizeof(line), "TIME: %s", value);
        st7789_draw_text(VALUE_TEXT_X, TIME_TEXT_Y, line, LCD_WHITE, LCD_BLACK, 2);
    }
    else if (mode == RECEIVER)
    {
        st7789_draw_text(VALUE_TEXT_X, FC_V_TEXT_Y, "FC_V: --- V", LCD_BLUE, LCD_BLACK, 2);
        st7789_draw_text(VALUE_TEXT_X, TIME_TEXT_Y, "TIME: --:--:--", LCD_BLUE, LCD_BLACK, 2);
    }

    draw_mode_row();
}
 
int main()
{
    // ---------------------- USB -------------------------
    // Initialize TinyUSB stack
    board_init();
    tusb_init();
 
    // TinyUSB board init callback after init
    if (board_init_after_tusb)
    {
        board_init_after_tusb();
    }
 
    stdio_usb_init();

    // ---------------------- LoRa Mode -------------------------
    gpio_init(LORA_MODE0_PIN);
    gpio_init(LORA_MODE1_PIN);
    gpio_set_dir(LORA_MODE0_PIN, GPIO_OUT);
    gpio_set_dir(LORA_MODE1_PIN, GPIO_OUT);
    gpio_put(LORA_MODE0_PIN, 0);
    gpio_put(LORA_MODE1_PIN, 0);
 
    // ---------------------- LoRa UART -------------------------
    gpio_set_function(UART_TX_PIN, UART_FUNCSEL_NUM(UART_ID, UART_TX_PIN));
    gpio_set_function(UART_RX_PIN, UART_FUNCSEL_NUM(UART_ID, UART_RX_PIN));

    uart_init(UART_ID, BAUD_RATE);
    uart_set_hw_flow(UART_ID, false, false);
    uart_set_format(UART_ID, DATA_BITS, STOP_BITS, PARITY);
    uart_set_fifo_enabled(UART_ID, false);
    uart_set_irq_enables(UART_ID, true, false);

    irq_set_exclusive_handler(UART0_IRQ, on_uart_rx);
    irq_set_enabled(UART0_IRQ, true);
    uart_set_irq_enables(UART_ID, true, false);
 
    // ---------------------- Buttons -------------------------
    gpio_init(BUTTON1_PIN);
    gpio_set_dir(BUTTON1_PIN, GPIO_IN);
    gpio_set_pulls(BUTTON1_PIN, true, false);
 
    gpio_init(BUTTON2_PIN);
    gpio_set_dir(BUTTON2_PIN, GPIO_IN);
    gpio_set_pulls(BUTTON2_PIN, true, false);
 
    // ---------------------- Screen -------------------------
    st7789_init(&lcd_config, lcd_width, lcd_height);
    st7789_fill(LCD_BLACK);
    redraw_status(0);
    bool button1_was_released = true;
    bool button2_was_released = true;
    absolute_time_t last_button_event_time = nil_time;
    bool pending_ui_refresh = false;
 
    absolute_time_t next_status_update = make_timeout_time_ms(1000);
    absolute_time_t next_transmit_update = make_timeout_time_ms(1000);
    while (1)
    {
        // tinyusb device task
        tud_task();

        uint8_t rx_byte;
        while (uart_ring_pop(&rx_byte))
        {
            tud_cdc_n_write_char(USB_ENDPOINT_DATA, rx_byte);
            rx_bytes_this_second++;
            if (process_telemetry_byte(rx_byte))
            {
                pending_ui_refresh = true;
            }
        }
        tud_cdc_n_write_flush(USB_ENDPOINT_DATA);
 
        bool button1_released = gpio_get(BUTTON1_PIN);
        bool button2_released = gpio_get(BUTTON2_PIN);

        // React only to press edge (released -> pressed), not while held.
        bool debounce_elapsed = is_nil_time(last_button_event_time) ||
            absolute_time_diff_us(last_button_event_time, get_absolute_time()) >= (int64_t)BUTTON_DEBOUNCE_MS * 1000;

        if (button1_was_released && !button1_released && debounce_elapsed)
        {
            if (mode != RECEIVER)
            {
                mode = RECEIVER;
                pending_ui_refresh = true;
                next_status_update = make_timeout_time_ms(1000);
                printf("[printf] New mode: receiver \r\n");
            }
            last_button_event_time = get_absolute_time();
        }
 
        if (button2_was_released && !button2_released && debounce_elapsed)
        {
            if (mode != TRANSMITTER)
            {
                mode = TRANSMITTER;
                clear_telemetry_values();
                pending_ui_refresh = true;
                next_transmit_update = make_timeout_time_ms(1000);
                printf("[printf] New mode: transmitter \r\n");
            }
            last_button_event_time = get_absolute_time();
        }

        button1_was_released = button1_released;
        button2_was_released = button2_released;
 
        if (absolute_time_diff_us(get_absolute_time(), next_status_update) <= 0)
        {
            uint32_t bytes_per_10s = update_rx_bytes_window();
            pending_ui_refresh = true;
            if (mode == RECEIVER)
            {
                log_reception_status(bytes_per_10s);
            }
            next_status_update = make_timeout_time_ms(1000);
        }

        if (pending_ui_refresh)
        {
            redraw_status(rx_bytes_last_10s);
            pending_ui_refresh = false;
        }

        if (mode == TRANSMITTER)
        {
            if (absolute_time_diff_us(get_absolute_time(), next_transmit_update) <= 0)
            {
                next_transmit_update = make_timeout_time_ms(1000);

                snprintf((char *)buffer, sizeof(buffer), "New message \r\n");

                tud_cdc_n_write_str(USB_ENDPOINT_DATA, "Transmission: ");
                tud_cdc_n_write_str(USB_ENDPOINT_DATA, (const char*) buffer);
                tud_cdc_n_write_flush(USB_ENDPOINT_DATA);
                uart_puts(UART_ID, (const char*) buffer);
                st7789_put(0xFFF0);
                printf("[printf] Periodic echo - transmission \r\n");
            }
        }
    }
}
