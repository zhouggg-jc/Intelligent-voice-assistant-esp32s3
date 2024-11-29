#include "inmp441.h"
#include "driver/i2s_std.h"
#include <stdio.h>

#define INMP441_BCLK_IO GPIO_NUM_1
#define INMP441_WS_IO GPIO_NUM_2
#define INMP441_DIN_IO GPIO_NUM_42

#define I2S_NUM I2S_NUM_0
#define I2S_CHANNEL_MODE I2S_SLOT_MODE_MONO
#define SAMPLE_RATE 16000
#define I2S_BITS_PER_SAMPLE I2S_DATA_BIT_WIDTH_32BIT
#define BYTE_RATE (SAMPLE_RATE * (I2S_BITS_PER_SAMPLE / 8) * I2S_CHANNEL_MODE)

#define BUF_SIZE 1024

i2s_chan_handle_t rx_handle;

void inmp441_init()
{
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM, I2S_ROLE_MASTER);
    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, NULL, &rx_handle));

    i2s_std_config_t std_cfg = {
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_BITS_PER_SAMPLE, I2S_CHANNEL_MODE),
        .slot_cfg.slot_mask = I2S_STD_SLOT_LEFT,
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE),
        .gpio_cfg = {
            .bclk = INMP441_BCLK_IO,
            .din = INMP441_DIN_IO,
            .dout = I2S_GPIO_UNUSED,
            .mclk = I2S_GPIO_UNUSED,
            .ws = INMP441_WS_IO,
            .invert_flags = {
                .bclk_inv = false,
                .mclk_inv = false,
                .ws_inv = false,
            },
        },
    };

    ESP_ERROR_CHECK(i2s_channel_init_std_mode(rx_handle, &std_cfg));
    ESP_ERROR_CHECK(i2s_channel_enable(rx_handle));
}
