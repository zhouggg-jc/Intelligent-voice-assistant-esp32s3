#include "max98357.h"
#include "driver/i2s_std.h"
#include <stdio.h>

#define MAX98357_BCLK_IO GPIO_NUM_11
#define MAX98357_WS_IO GPIO_NUM_12
#define MAX98357_DOUT_IO GPIO_NUM_10

#define I2S_NUM I2S_NUM_1
#define I2S_CHANNEL_MODE I2S_SLOT_MODE_MONO
#define SAMPLE_RATE 16000
#define I2S_BITS_PER_SAMPLE I2S_DATA_BIT_WIDTH_16BIT

i2s_chan_handle_t tx_handle;

void max98357_init()
{
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM, I2S_ROLE_MASTER);
    chan_cfg.auto_clear = true;
    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, &tx_handle, NULL));

    i2s_std_config_t std_cfg = {
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_BITS_PER_SAMPLE, I2S_CHANNEL_MODE),
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE),
        .gpio_cfg = {
            .bclk = MAX98357_BCLK_IO,
            .din = I2S_GPIO_UNUSED,
            .dout = MAX98357_DOUT_IO,
            .mclk = I2S_GPIO_UNUSED,
            .ws = MAX98357_WS_IO,
            .invert_flags = {
                .bclk_inv = false,
                .mclk_inv = false,
                .ws_inv = false,
            },
        },
    };

    ESP_ERROR_CHECK(i2s_channel_init_std_mode(tx_handle, &std_cfg));
    ESP_ERROR_CHECK(i2s_channel_enable(tx_handle));
}
