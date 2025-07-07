#include <stdio.h>
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
    .gpio_bl = 13,
};
 
// LCD rotation {0x60, 240, 135, 40, 53},
const uint16_t lcd_width = 240;
const uint16_t lcd_height = 135;
 
uint8_t buffer[128];
transceiver_mode mode = RECEIVER;

const uint8_t RX_START_BYTE = 0xFF;
const uint8_t RX_END_BYTE = 0xEE;
const uint32_t RX_BUFFER_SIZE = 64;
const uint32_t RX_FRAME_SIZE = 128;

bool rx_found_start = false;
uint8_t rx_buffer[RX_BUFFER_SIZE];
uint32_t current_frame_byte_index = 0;

void on_uart_rx() {
    while (uart_is_readable(UART_ID)) {
        tud_cdc_n_write_char(USB_ENDPOINT_DATA, uart_getc(UART_ID));
    }
}

//void read_next_uart_byte() {
    // if (uart_is_readable_within_us(UART_ID, 10)) {

    //     while (uart_is_readable_within_us(UART_ID, 100))
    //     {
    //         current_frame_byte_index++;
    //         rx_buffer[current_frame_byte_index] = uart_getc(UART_ID);

    //         if ()
    //         tud_cdc_n_write_char(USB_ENDPOINT_DATA, received_char);


    //         printf("[printf] Data received - BYTE reception \r\n");
    //         st7789_put(0x0FFF);
    //     }
    //     tud_cdc_n_write_flush(USB_ENDPOINT_DATA);

    // }

//     if (!uart_is_readable_within_us(UART_ID, 10)) {
//         return;
//     }

//     uint8_t rx_byte = uart_getc(UART_ID);

//     // Write incoming byte to array
//     rx_buffer[current_frame_byte_index] = rx_byte;

//     if (!rx_found_start) {
//         if (rx_buffer[current_frame_byte_index] == RX_START_BYTE) {
//             // 1 variant - found start byte
//             rx_found_start = 1;
//             current_frame_byte_index = 1;
//             rx_buffer[0] = RX_START_BYTE;
//         } else {
//             // 2 variant - not found start byte yet
//             current_frame_byte_index++;
//         }

//         HAL_UART_Receive_IT(&UART_PORT_RS485_SW, &rx_byte, 1);
//         return;
//     }

//     current_frame_byte_index++;

//     // Make sure there is no overflow in array size
//     if (current_frame_byte_index > RX_BUFFER_SIZE) {
//         current_frame_byte_index = 0;
//     }

//     if (current_frame_byte_index == RX_FRAME_SIZE) {
//         if (rx_buffer[RX_FRAME_SIZE - 1] == RX_END_BYTE) {
//             // 3 variant - found end byte
//             intRxCplt_SW = 1;
//         }
//         // 4 variant - not found start byte

//         current_frame_byte_index = 0;
//         rx_found_start = 0;

//         HAL_UART_Receive_IT(&UART_PORT_RS485_SW, &rx_byte, 1);
//         return;
//     }

//     // 5 variant - byte between start and end
//     HAL_UART_Receive_IT(&UART_PORT_RS485_SW, &rx_byte, 1);
// }
 
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
 
    // let pico sdk use the first cdc interface for std io
    //stdio_init_all();
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
    st7789_fill(0xffff); // make screen white
    st7789_set_cursor(100, 100);
 
    uint64_t delay = 0;
    while (1)
    {
        // tinyusb device task
        tud_task();
 
        if (gpio_get(BUTTON1_PIN) == false)
        {
            delay = 0;
            mode = RECEIVER;
            st7789_fill(0xffff);
            st7789_set_cursor(100, 100);
            printf("[printf] New mode: receiver \r\n");
        }
 
        if (gpio_get(BUTTON2_PIN) == false)
        {
            delay = 0;
            mode = TRANSMITTER;
            st7789_fill(0x0000);
            st7789_set_cursor(100, 100);
            printf("[printf] New mode: transmitter \r\n");
        }
 
        delay++;
 
        if (mode == RECEIVER)
        {
            if (delay > 1000000)
            {
                delay = 0;
                printf("[printf] Periodic echo - reception \r\n");
            }

            //read_next_uart_byte();

        }
 
        if (mode == TRANSMITTER)
        {
            if (delay > 1000000)
            {
                delay = 0;
 
                sprintf((char *)buffer, "New message \r\n\0");

                tud_cdc_n_write_str(USB_ENDPOINT_DATA, "Transmission: ");
                tud_cdc_n_write_str(USB_ENDPOINT_DATA, (const char*) buffer);
                tud_cdc_n_write_flush(USB_ENDPOINT_DATA);
                uart_puts(UART_ID, (const char*) buffer);
                st7789_put(0xFFF0);

                // for (int i = 0; i < strlen((const char *)buffer); i++)
                // {
                //     uart_putc(UART_ID, buffer[i]);
                //     st7789_put(0xFFF0);
                //     tud_cdc_n_write_char(USB_ENDPOINT_DATA, buffer[i]); //
                //     tud_cdc_write_flush();
                // }

 
                // for (int i = 0; i < 128; i++)
                // {
                //     uart_putc(UART_ID, buffer[i]);
                // }
 
                printf("[printf] Periodic echo - transmission \r\n");
            }
        }
    }
}