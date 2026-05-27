/*
 * Copyright (c) 2021 Arm Limited and Contributors. All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 */

#include <string.h>

#include "pico/stdlib.h"
#include "hardware/gpio.h"

#include "st7789.h"

static struct st7789_config st7789_cfg;
static uint16_t st7789_width;
static uint16_t st7789_height;
static bool st7789_data_mode = false;

static void st7789_cmd(uint8_t cmd, const uint8_t *data, size_t len)
{
    if (st7789_cfg.gpio_cs > -1)
    {
        spi_set_format(st7789_cfg.spi, 8, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);
    }
    else
    {
        spi_set_format(st7789_cfg.spi, 8, SPI_CPOL_1, SPI_CPHA_1, SPI_MSB_FIRST);
    }
    st7789_data_mode = false;

    sleep_us(1);
    if (st7789_cfg.gpio_cs > -1)
    {
        gpio_put(st7789_cfg.gpio_cs, 0);
    }
    gpio_put(st7789_cfg.gpio_dc, 0);
    sleep_us(1);

    spi_write_blocking(st7789_cfg.spi, &cmd, sizeof(cmd));

    if (len)
    {
        sleep_us(1);
        gpio_put(st7789_cfg.gpio_dc, 1);
        sleep_us(1);

        spi_write_blocking(st7789_cfg.spi, data, len);
    }

    sleep_us(1);
    if (st7789_cfg.gpio_cs > -1)
    {
        gpio_put(st7789_cfg.gpio_cs, 1);
    }
    gpio_put(st7789_cfg.gpio_dc, 1);
    sleep_us(1);
}

void st7789_caset(uint16_t xs, uint16_t xe)
{
    uint8_t data[] = {
        xs >> 8,
        xs & 0xff,
        xe >> 8,
        xe & 0xff,
    };

    // CASET (2Ah): Column Address Set
    st7789_cmd(0x2a, data, sizeof(data));
}

void st7789_raset(uint16_t ys, uint16_t ye)
{
    uint8_t data[] = {
        ys >> 8,
        ys & 0xff,
        ye >> 8,
        ye & 0xff,
    };

    // RASET (2Bh): Row Address Set
    st7789_cmd(0x2b, data, sizeof(data));
}

void st7789_init(const struct st7789_config *config, uint16_t width, uint16_t height)
{
    memcpy(&st7789_cfg, config, sizeof(st7789_cfg));
    st7789_width = width;
    st7789_height = height;

    spi_init(st7789_cfg.spi, 125 * 1000 * 1000);
    if (st7789_cfg.gpio_cs > -1)
    {
        spi_set_format(st7789_cfg.spi, 8, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);
    }
    else
    {
        spi_set_format(st7789_cfg.spi, 8, SPI_CPOL_1, SPI_CPHA_1, SPI_MSB_FIRST);
    }

    gpio_set_function(st7789_cfg.gpio_din, GPIO_FUNC_SPI);
    gpio_set_function(st7789_cfg.gpio_clk, GPIO_FUNC_SPI);

    if (st7789_cfg.gpio_cs > -1)
    {
        gpio_init(st7789_cfg.gpio_cs);
    }
    gpio_init(st7789_cfg.gpio_dc);
    gpio_init(st7789_cfg.gpio_rst);
    if (st7789_cfg.gpio_bl > -1)
    {
        gpio_init(st7789_cfg.gpio_bl);
    }

    if (st7789_cfg.gpio_cs > -1)
    {
        gpio_set_dir(st7789_cfg.gpio_cs, GPIO_OUT);
    }
    gpio_set_dir(st7789_cfg.gpio_dc, GPIO_OUT);
    gpio_set_dir(st7789_cfg.gpio_rst, GPIO_OUT);
    if (st7789_cfg.gpio_bl > -1)
    {
        gpio_set_dir(st7789_cfg.gpio_bl, GPIO_OUT);
    }

    if (st7789_cfg.gpio_cs > -1)
    {
        gpio_put(st7789_cfg.gpio_cs, 1);
    }
    gpio_put(st7789_cfg.gpio_dc, 1);
    gpio_put(st7789_cfg.gpio_rst, 1);
    sleep_ms(100);

    // SWRESET (01h): Software Reset
    st7789_cmd(0x01, NULL, 0);
    sleep_ms(150);

    // SLPOUT (11h): Sleep Out
    st7789_cmd(0x11, NULL, 0);
    sleep_ms(50);

    // COLMOD (3Ah): Interface Pixel Format
    // - RGB interface color format     = 65K of RGB interface
    // - Control interface color format = 16bit/pixel
    st7789_cmd(0x3a, (uint8_t[]) { 0x55 }, 1);
    sleep_ms(10);

    // MADCTL (36h): Memory Data Access Control
    // - Page Address Order            = Top to Bottom
    // - Column Address Order          = Left to Right
    // - Page/Column Order             = Normal Mode
    // - Line Address Order            = LCD Refresh Top to Bottom
    // - RGB/BGR Order                 = RGB
    // - Display Data Latch Data Order = LCD Refresh Left to Right
    st7789_cmd(0x36, &st7789_cfg.madctl, 1);

    st7789_caset(st7789_cfg.x_offset, st7789_cfg.x_offset + width - 1);
    st7789_raset(st7789_cfg.y_offset, st7789_cfg.y_offset + height - 1);

    // INVON (21h): Display Inversion On
    st7789_cmd(0x21, NULL, 0);
    sleep_ms(10);

    // NORON (13h): Normal Display Mode On
    st7789_cmd(0x13, NULL, 0);
    sleep_ms(10);

    // DISPON (29h): Display On
    st7789_cmd(0x29, NULL, 0);
    sleep_ms(10);

    if (st7789_cfg.gpio_bl > -1)
    {
        gpio_put(st7789_cfg.gpio_bl, 1);
    }
}

void st7789_ramwr()
{
    sleep_us(1);
    if (st7789_cfg.gpio_cs > -1)
    {
        gpio_put(st7789_cfg.gpio_cs, 0);
    }
    gpio_put(st7789_cfg.gpio_dc, 0);
    sleep_us(1);

    // RAMWR (2Ch): Memory Write
    uint8_t cmd = 0x2c;
    spi_write_blocking(st7789_cfg.spi, &cmd, sizeof(cmd));

    sleep_us(1);
    if (st7789_cfg.gpio_cs > -1)
    {
        gpio_put(st7789_cfg.gpio_cs, 0);
    }
    gpio_put(st7789_cfg.gpio_dc, 1);
    sleep_us(1);
}

void st7789_write(const void *data, size_t len)
{
    if (!st7789_data_mode)
    {
        st7789_ramwr();

        if (st7789_cfg.gpio_cs > -1)
        {
            spi_set_format(st7789_cfg.spi, 16, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);
        }
        else
        {
            spi_set_format(st7789_cfg.spi, 16, SPI_CPOL_1, SPI_CPHA_1, SPI_MSB_FIRST);
        }

        st7789_data_mode = true;
    }

    spi_write16_blocking(st7789_cfg.spi, data, len / 2);
}

void st7789_put(uint16_t pixel)
{
    st7789_write(&pixel, sizeof(pixel));
}

void st7789_fill(uint16_t pixel)
{
    int num_pixels = st7789_width * st7789_height;

    st7789_set_cursor(0, 0);

    for (int i = 0; i < num_pixels; i++)
    {
        st7789_put(pixel);
    }
}

void st7789_fill_rect(uint16_t x, uint16_t y, uint16_t width, uint16_t height, uint16_t pixel)
{
    if (x >= st7789_width || y >= st7789_height)
    {
        return;
    }

    if (x + width > st7789_width)
    {
        width = st7789_width - x;
    }
    if (y + height > st7789_height)
    {
        height = st7789_height - y;
    }

    st7789_caset(st7789_cfg.x_offset + x, st7789_cfg.x_offset + x + width - 1);
    st7789_raset(st7789_cfg.y_offset + y, st7789_cfg.y_offset + y + height - 1);
    for (uint32_t i = 0; i < (uint32_t)width * height; i++)
    {
        st7789_put(pixel);
    }
}

static const uint8_t *glyph_rows(char c)
{
    static const uint8_t space[] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
    static const uint8_t minus[] = { 0x00, 0x00, 0x00, 0x1f, 0x00, 0x00, 0x00 };
    static const uint8_t dot[] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x0c, 0x0c };
    static const uint8_t slash[] = { 0x01, 0x01, 0x02, 0x04, 0x08, 0x10, 0x10 };
    static const uint8_t colon[] = { 0x00, 0x0c, 0x0c, 0x00, 0x0c, 0x0c, 0x00 };
    static const uint8_t alnum[][7] = {
        { 0x0e, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0e },
        { 0x04, 0x0c, 0x04, 0x04, 0x04, 0x04, 0x0e },
        { 0x0e, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1f },
        { 0x1f, 0x02, 0x04, 0x02, 0x01, 0x11, 0x0e },
        { 0x02, 0x06, 0x0a, 0x12, 0x1f, 0x02, 0x02 },
        { 0x1f, 0x10, 0x1e, 0x01, 0x01, 0x11, 0x0e },
        { 0x06, 0x08, 0x10, 0x1e, 0x11, 0x11, 0x0e },
        { 0x1f, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08 },
        { 0x0e, 0x11, 0x11, 0x0e, 0x11, 0x11, 0x0e },
        { 0x0e, 0x11, 0x11, 0x0f, 0x01, 0x02, 0x0c },
        { 0x0e, 0x11, 0x11, 0x1f, 0x11, 0x11, 0x11 },
        { 0x1e, 0x11, 0x11, 0x1e, 0x11, 0x11, 0x1e },
        { 0x0e, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0e },
        { 0x1e, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1e },
        { 0x1f, 0x10, 0x10, 0x1e, 0x10, 0x10, 0x1f },
        { 0x1f, 0x10, 0x10, 0x1e, 0x10, 0x10, 0x10 },
        { 0x0e, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0f },
        { 0x11, 0x11, 0x11, 0x1f, 0x11, 0x11, 0x11 },
        { 0x0e, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0e },
        { 0x07, 0x02, 0x02, 0x02, 0x12, 0x12, 0x0c },
        { 0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11 },
        { 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1f },
        { 0x11, 0x1b, 0x15, 0x15, 0x11, 0x11, 0x11 },
        { 0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11 },
        { 0x0e, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0e },
        { 0x1e, 0x11, 0x11, 0x1e, 0x10, 0x10, 0x10 },
        { 0x0e, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0d },
        { 0x1e, 0x11, 0x11, 0x1e, 0x14, 0x12, 0x11 },
        { 0x0f, 0x10, 0x10, 0x0e, 0x01, 0x01, 0x1e },
        { 0x1f, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04 },
        { 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0e },
        { 0x11, 0x11, 0x11, 0x11, 0x11, 0x0a, 0x04 },
        { 0x11, 0x11, 0x11, 0x15, 0x15, 0x15, 0x0a },
        { 0x11, 0x11, 0x0a, 0x04, 0x0a, 0x11, 0x11 },
        { 0x11, 0x11, 0x0a, 0x04, 0x04, 0x04, 0x04 },
        { 0x1f, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1f },
    };

    if (c >= 'a' && c <= 'z')
    {
        c -= 'a' - 'A';
    }
    if (c >= '0' && c <= '9')
    {
        return alnum[c - '0'];
    }
    if (c >= 'A' && c <= 'Z')
    {
        return alnum[10 + c - 'A'];
    }
    if (c == '-')
    {
        return minus;
    }
    if (c == '.')
    {
        return dot;
    }
    if (c == '/')
    {
        return slash;
    }
    if (c == ':')
    {
        return colon;
    }
    return space;
}

static void st7789_draw_char(uint16_t x, uint16_t y, char c, uint16_t fg, uint16_t bg, uint8_t scale)
{
    const uint8_t *rows = glyph_rows(c);

    for (uint8_t row = 0; row < 7; row++)
    {
        for (uint8_t col = 0; col < 5; col++)
        {
            uint16_t color = (rows[row] & (1u << (4 - col))) ? fg : bg;
            st7789_fill_rect(x + col * scale, y + row * scale, scale, scale, color);
        }
    }
}

void st7789_draw_text(uint16_t x, uint16_t y, const char *text, uint16_t fg, uint16_t bg, uint8_t scale)
{
    while (*text)
    {
        st7789_draw_char(x, y, *text, fg, bg, scale);
        x += 6 * scale;
        text++;
    }
}

void st7789_set_cursor(uint16_t x, uint16_t y)
{
    st7789_caset(st7789_cfg.x_offset + x, st7789_cfg.x_offset + st7789_width - 1);
    st7789_raset(st7789_cfg.y_offset + y, st7789_cfg.y_offset + st7789_height - 1);
}

void st7789_vertical_scroll(uint16_t row)
{
    uint8_t data[] = { (row >> 8) & 0xff, row & 0x00ff };

    // VSCSAD (37h): Vertical Scroll Start Address of RAM
    st7789_cmd(0x37, data, sizeof(data));
}
