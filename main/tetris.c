#include <driver/gpio.h>
#include <driver/i2c_master.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <stdio.h>
#include <string.h>
#include "sdkconfig.h"
#include "driver/rtc_io.h"
#include "esp_sleep.h"

#include <u8g2.h>
#include "u8g2_esp32_hal.h"

#define DISPLAY_WIDTH 128
#define DISPLAY_HEIGHT 64

#define LEFT_BUTTON  15
#define DOWN_BUTTON  2
#define UP_BUTTON    27
#define RIGHT_BUTTON 26

#define PIN_SDA 21
#define PIN_SCL 22

#define TETRIS_BLOCK_SIZE 3
#define TETRIS_MAP_WIDTH  10
#define TETRIS_MAP_HEIGHT 10

static u8g2_t u8g2;
static u8g2_esp32_hal_t u8g2_esp32_hal = U8G2_ESP32_HAL_DEFAULT;
static bool tetris_map[20][10];
static int tetris_highscore = 0;

void init_low_power_mode()
{
    uint64_t buttonPinMask = (1ULL << LEFT_BUTTON) | (1ULL << DOWN_BUTTON) |
                             (1ULL << RIGHT_BUTTON) | (1ULL << UP_BUTTON);
    esp_sleep_enable_ext1_wakeup(buttonPinMask, ESP_EXT1_WAKEUP_ANY_HIGH);
}

void init_buttons()
{
    int buttons[] = {LEFT_BUTTON, DOWN_BUTTON, UP_BUTTON, RIGHT_BUTTON};
    for(int i = 0; i < sizeof(buttons)/sizeof(buttons[0]); i++)
    {
        gpio_reset_pin(buttons[i]);
        gpio_set_direction(buttons[i], GPIO_MODE_INPUT);
        gpio_pullup_dis(buttons[i]);
        gpio_pulldown_en(buttons[i]);
    }
}

void init_display()
{
    u8g2_esp32_hal.bus.i2c.sda = PIN_SDA;
    u8g2_esp32_hal.bus.i2c.scl = PIN_SCL;
    u8g2_esp32_hal_init(u8g2_esp32_hal);


    u8g2_Setup_sh1106_i2c_128x64_noname_f(&u8g2, U8G2_R0,
        u8g2_esp32_i2c_byte_cb,
        u8g2_esp32_gpio_and_delay_cb); 
    
    u8x8_SetI2CAddress(&u8g2.u8x8, 0x78);
    u8g2_InitDisplay(&u8g2);  // initialize display, display is in sleep mode after this
    u8g2_SetPowerSave(&u8g2, 0);  // wake up display
    u8g2_ClearBuffer(&u8g2);
    u8g2_SendBuffer(&u8g2);
}

void tetris_start_screen()
{
    u8g2_ClearBuffer(&u8g2); // Clear the internal screen buffer

    u8g2_SetFont(&u8g2, u8g2_font_4x6_mf); // Use small font
    u8g2_DrawStr(&u8g2, 10, 32, "Start screen"); // Draw text at (x=10, y=32)

    u8g2_SendBuffer(&u8g2); // Transfer buffer to the display
}

void tetris_end_screen()
{
    u8g2_ClearBuffer(&u8g2); // Clear the internal screen buffer

    u8g2_SetFont(&u8g2, u8g2_font_4x6_mf); // Use small font
    u8g2_DrawStr(&u8g2, 10, 32, "End screen"); // Draw text at (x=10, y=32)

    u8g2_SendBuffer(&u8g2); // Transfer buffer to the display
}


void tetris_draw_frame()
{
    short int x1 = DISPLAY_WIDTH/2;
    short int x2 = x1 + TETRIS_MAP_WIDTH*TETRIS_BLOCK_SIZE + 2;
    short int y1 = (DISPLAY_HEIGHT - TETRIS_BLOCK_SIZE*TETRIS_MAP_HEIGHT - 2)/2;
    short int y2 = y1 + TETRIS_MAP_HEIGHT*TETRIS_BLOCK_SIZE + 2; 
    u8g2_DrawLine(&u8g2, x1, DISPLAY_HEIGHT - y1, x2, DISPLAY_HEIGHT - y1);
    u8g2_DrawLine(&u8g2, x1, DISPLAY_HEIGHT - y2, x2, DISPLAY_HEIGHT - y2);
    u8g2_DrawLine(&u8g2, x1, DISPLAY_HEIGHT - y2, x1, DISPLAY_HEIGHT - y1);
    u8g2_DrawLine(&u8g2, x2, DISPLAY_HEIGHT - y2, x2, DISPLAY_HEIGHT - y1);
}

void tetris_draw_blocks()
{
    short int x_offset = DISPLAY_WIDTH/2 + 1;
    short int y_offset = (DISPLAY_HEIGHT - TETRIS_BLOCK_SIZE*TETRIS_MAP_HEIGHT - 2)/2 + 1;
    for(int row = 0; row < TETRIS_MAP_HEIGHT; row++)
    {
        for(int col = 0; col < TETRIS_MAP_WIDTH; col++)
        {
            if(tetris_map[row][col])
                u8g2_DrawBox(&u8g2, x_offset + col*TETRIS_BLOCK_SIZE,
                    DISPLAY_HEIGHT - (TETRIS_BLOCK_SIZE - 1) - (y_offset + row*TETRIS_BLOCK_SIZE),
                    TETRIS_BLOCK_SIZE, TETRIS_BLOCK_SIZE);
        }
    }
}

void app_main(void)
{
    init_buttons();
    init_display();
    init_low_power_mode();

    int score;

    while(true)
    {
        //initialize variables
        score = 0;
        memset(tetris_map, 0, sizeof(tetris_map));

        tetris_start_screen();

        //wait for button press to start the game
        esp_light_sleep_start();

        //main game loop
        while(true)
        {

        }

        //free any dynamic memroy

        tetris_end_screen(score);

        //wait for exit the game or play again button press
        esp_light_sleep_start();
        //if(!gpio_get_level(LEFT_BUTTON))
            //break;
    }
}
