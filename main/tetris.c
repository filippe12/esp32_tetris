#include <driver/gpio.h>
#include <driver/i2c_master.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
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
#define TETRIS_MAP_HEIGHT 20
#define TETRIS_MAX_SPEED  5
#define TETRIS_NUMBER_OF_BLOCKS 9

static u8g2_t u8g2;
static u8g2_esp32_hal_t u8g2_esp32_hal = U8G2_ESP32_HAL_DEFAULT;
static bool tetris_map[20][10];
static int tetris_highscore = 0;

typedef enum block_rotation
{
    NO_ROTATION , LEFT_90, RIGHT_90, UPSIDE_DOWN
} block_rotation;

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

void tetris_shift_rows_down(short int starting_row, short int amount)
{
    for(int row = starting_row; row < TETRIS_MAP_HEIGHT - amount; row++)
    {
        memcpy(tetris_map[row], tetris_map[row + amount], sizeof(tetris_map[row]));
    }
    for(int row = TETRIS_MAP_HEIGHT - amount; row < TETRIS_MAP_HEIGHT; row++)
    {
        memset(tetris_map[row], 0, sizeof(tetris_map[row]));
    }
}

void tetris_start_screen()
{
    u8g2_ClearBuffer(&u8g2); // Clear the internal screen buffer

    u8g2_SetFont(&u8g2, u8g2_font_4x6_mf); // Use small font
    u8g2_DrawStr(&u8g2, 10, 32, "Start screen"); // Draw text at (x=10, y=32)

    u8g2_SendBuffer(&u8g2); // Transfer buffer to the display
}

void tetris_end_screen(int score)
{
    u8g2_ClearBuffer(&u8g2);

    u8g2_SetFont(&u8g2, u8g2_font_helvB10_tr);
    const char *msg = (score > tetris_highscore) ? "New High Score!" : "Game Over";
    int msg_x = (DISPLAY_WIDTH - u8g2_GetStrWidth(&u8g2, msg)) / 2 - 2;
    u8g2_DrawStr(&u8g2, msg_x, 16, msg);

    char buf[32];
    u8g2_SetFont(&u8g2, u8g2_font_6x10_tr);
    snprintf(buf, sizeof(buf), "Score: %d", score);
    int score_x = (DISPLAY_WIDTH - u8g2_GetStrWidth(&u8g2, buf)) / 2;
    u8g2_DrawStr(&u8g2, score_x, 32, buf);

    if (score <= tetris_highscore) {
        snprintf(buf, sizeof(buf), "Best: %d", tetris_highscore);
        int best_x = (DISPLAY_WIDTH - u8g2_GetStrWidth(&u8g2, buf)) / 2;
        u8g2_DrawStr(&u8g2, best_x, 44, buf);
    }
    
    u8g2_SetFont(&u8g2, u8g2_font_5x8_tr);
    u8g2_DrawStr(&u8g2, 5, 60, "Play Again");
    u8g2_DrawStr(&u8g2, 95, 60, "Exit");

    u8g2_SendBuffer(&u8g2);

    if (score > tetris_highscore)
        tetris_highscore = score;
}

void tetris_draw_frame()
{
    short int x1 = DISPLAY_WIDTH/2;
    short int x2 = x1 + TETRIS_MAP_WIDTH*TETRIS_BLOCK_SIZE + 1;
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

void tetris_draw_active_block(short int map_x, short int map_y, short int id, block_rotation rotation)
{
    short int x_offset = DISPLAY_WIDTH/2 + 1;
    short int y_offset = (DISPLAY_HEIGHT - TETRIS_BLOCK_SIZE*TETRIS_MAP_HEIGHT - 2)/2 + 1;
    switch(id)
    {
        case 0: //single block
            u8g2_DrawBox(&u8g2, x_offset + map_x*TETRIS_BLOCK_SIZE,
                    DISPLAY_HEIGHT - (TETRIS_BLOCK_SIZE - 1) - (y_offset + map_y*TETRIS_BLOCK_SIZE),
                    TETRIS_BLOCK_SIZE, TETRIS_BLOCK_SIZE);       
            break;

        case 1: //2x2 block
            u8g2_DrawBox(&u8g2, x_offset + map_x*TETRIS_BLOCK_SIZE,
                    DISPLAY_HEIGHT - (TETRIS_BLOCK_SIZE - 1) - (y_offset + map_y*TETRIS_BLOCK_SIZE),
                    2*TETRIS_BLOCK_SIZE, 2*TETRIS_BLOCK_SIZE);       
            break;

        case 2: //small L block
            u8g2_DrawBox(&u8g2, x_offset + map_x*TETRIS_BLOCK_SIZE,
                    DISPLAY_HEIGHT - (TETRIS_BLOCK_SIZE - 1) - (y_offset + map_y*TETRIS_BLOCK_SIZE),
                    2*TETRIS_BLOCK_SIZE, 2*TETRIS_BLOCK_SIZE);       
            u8g2_SetDrawColor(&u8g2, 0);
            switch (rotation)
            {
                case NO_ROTATION:
                    u8g2_DrawBox(&u8g2, x_offset + (map_x + 1)*TETRIS_BLOCK_SIZE,
                        DISPLAY_HEIGHT - (TETRIS_BLOCK_SIZE - 1) - (y_offset + map_y*TETRIS_BLOCK_SIZE),
                        TETRIS_BLOCK_SIZE, TETRIS_BLOCK_SIZE);       
                    break;
                case RIGHT_90:
                    u8g2_DrawBox(&u8g2, x_offset + (map_x + 1)*TETRIS_BLOCK_SIZE,
                        DISPLAY_HEIGHT - (TETRIS_BLOCK_SIZE - 1) - (y_offset + (map_y - 1)*TETRIS_BLOCK_SIZE),
                        TETRIS_BLOCK_SIZE, TETRIS_BLOCK_SIZE);       
                    break;
                case UPSIDE_DOWN:
                    u8g2_DrawBox(&u8g2, x_offset + map_x*TETRIS_BLOCK_SIZE,
                        DISPLAY_HEIGHT - (TETRIS_BLOCK_SIZE - 1) - (y_offset + (map_y - 1)*TETRIS_BLOCK_SIZE),
                        TETRIS_BLOCK_SIZE, TETRIS_BLOCK_SIZE);       
                    break;
                case LEFT_90:
                    u8g2_DrawBox(&u8g2, x_offset + map_x*TETRIS_BLOCK_SIZE,
                        DISPLAY_HEIGHT - (TETRIS_BLOCK_SIZE - 1) - (y_offset + map_y*TETRIS_BLOCK_SIZE),
                        TETRIS_BLOCK_SIZE, TETRIS_BLOCK_SIZE);       
                    break;
            }
            u8g2_SetDrawColor(&u8g2, 1);
            break;

        case 3: //t block
            u8g2_DrawBox(&u8g2, x_offset + map_x*TETRIS_BLOCK_SIZE,
                    DISPLAY_HEIGHT - (TETRIS_BLOCK_SIZE - 1) - (y_offset + map_y*TETRIS_BLOCK_SIZE),
                    TETRIS_BLOCK_SIZE, 2*TETRIS_BLOCK_SIZE);
            switch(rotation)
            {
                case NO_ROTATION:
                    u8g2_DrawBox(&u8g2, x_offset + (map_x - 1)*TETRIS_BLOCK_SIZE,
                        DISPLAY_HEIGHT - (TETRIS_BLOCK_SIZE - 1) - (y_offset + map_y*TETRIS_BLOCK_SIZE),
                        3*TETRIS_BLOCK_SIZE, TETRIS_BLOCK_SIZE);
                    break;
                case RIGHT_90:
                    u8g2_DrawBox(&u8g2, x_offset + map_x*TETRIS_BLOCK_SIZE,
                        DISPLAY_HEIGHT - (TETRIS_BLOCK_SIZE - 1) - (y_offset + (map_y - 2)*TETRIS_BLOCK_SIZE),
                        TETRIS_BLOCK_SIZE, TETRIS_BLOCK_SIZE);
                    u8g2_DrawBox(&u8g2, x_offset + (map_x - 1)*TETRIS_BLOCK_SIZE,
                        DISPLAY_HEIGHT - (TETRIS_BLOCK_SIZE - 1) - (y_offset + (map_y - 1)*TETRIS_BLOCK_SIZE),
                        TETRIS_BLOCK_SIZE, TETRIS_BLOCK_SIZE);
                    break;
                case UPSIDE_DOWN:
                    u8g2_DrawBox(&u8g2, x_offset + (map_x - 1)*TETRIS_BLOCK_SIZE,
                        DISPLAY_HEIGHT - (TETRIS_BLOCK_SIZE - 1) - (y_offset + (map_y - 1)*TETRIS_BLOCK_SIZE),
                        3*TETRIS_BLOCK_SIZE, TETRIS_BLOCK_SIZE);
                    break;
                case LEFT_90:
                    u8g2_DrawBox(&u8g2, x_offset + map_x*TETRIS_BLOCK_SIZE,
                        DISPLAY_HEIGHT - (TETRIS_BLOCK_SIZE - 1) - (y_offset + (map_y - 2)*TETRIS_BLOCK_SIZE),
                        TETRIS_BLOCK_SIZE, TETRIS_BLOCK_SIZE);
                    u8g2_DrawBox(&u8g2, x_offset + (map_x + 1)*TETRIS_BLOCK_SIZE,
                        DISPLAY_HEIGHT - (TETRIS_BLOCK_SIZE - 1) - (y_offset + (map_y - 1)*TETRIS_BLOCK_SIZE),
                        TETRIS_BLOCK_SIZE, TETRIS_BLOCK_SIZE);
                    break;
            } break;

        case 4: //z block
            switch(rotation)
            {
                case NO_ROTATION:
                case UPSIDE_DOWN:
                    u8g2_DrawBox(&u8g2, x_offset + (map_x - 1)*TETRIS_BLOCK_SIZE,
                        DISPLAY_HEIGHT - (TETRIS_BLOCK_SIZE - 1) - (y_offset + map_y*TETRIS_BLOCK_SIZE),
                        2*TETRIS_BLOCK_SIZE, TETRIS_BLOCK_SIZE);
                    u8g2_DrawBox(&u8g2, x_offset + map_x*TETRIS_BLOCK_SIZE,
                        DISPLAY_HEIGHT - (TETRIS_BLOCK_SIZE - 1) - (y_offset + (map_y - 1)*TETRIS_BLOCK_SIZE),
                        2*TETRIS_BLOCK_SIZE, TETRIS_BLOCK_SIZE);
                    break;
                case RIGHT_90:
                case LEFT_90:
                    u8g2_DrawBox(&u8g2, x_offset + map_x*TETRIS_BLOCK_SIZE,
                        DISPLAY_HEIGHT - (TETRIS_BLOCK_SIZE - 1) - (y_offset + map_y*TETRIS_BLOCK_SIZE),
                        TETRIS_BLOCK_SIZE, 2*TETRIS_BLOCK_SIZE);
                    u8g2_DrawBox(&u8g2, x_offset + (map_x - 1)*TETRIS_BLOCK_SIZE,
                        DISPLAY_HEIGHT - (TETRIS_BLOCK_SIZE - 1) - (y_offset + (map_y - 1)*TETRIS_BLOCK_SIZE),
                        TETRIS_BLOCK_SIZE, 2*TETRIS_BLOCK_SIZE);
                    break;
            } break;

        case 5: //reverse z block
            switch(rotation)
            {
                case NO_ROTATION:
                case UPSIDE_DOWN:
                    u8g2_DrawBox(&u8g2, x_offset + map_x*TETRIS_BLOCK_SIZE,
                        DISPLAY_HEIGHT - (TETRIS_BLOCK_SIZE - 1) - (y_offset + map_y*TETRIS_BLOCK_SIZE),
                        2*TETRIS_BLOCK_SIZE, TETRIS_BLOCK_SIZE);
                    u8g2_DrawBox(&u8g2, x_offset + (map_x - 1)*TETRIS_BLOCK_SIZE,
                        DISPLAY_HEIGHT - (TETRIS_BLOCK_SIZE - 1) - (y_offset + (map_y - 1)*TETRIS_BLOCK_SIZE),
                        2*TETRIS_BLOCK_SIZE, TETRIS_BLOCK_SIZE);
                    break;
                case RIGHT_90:
                case LEFT_90:
                    u8g2_DrawBox(&u8g2, x_offset + map_x*TETRIS_BLOCK_SIZE,
                        DISPLAY_HEIGHT - (TETRIS_BLOCK_SIZE - 1) - (y_offset + map_y*TETRIS_BLOCK_SIZE),
                        TETRIS_BLOCK_SIZE, 2*TETRIS_BLOCK_SIZE);
                    u8g2_DrawBox(&u8g2, x_offset + (map_x + 1)*TETRIS_BLOCK_SIZE,
                        DISPLAY_HEIGHT - (TETRIS_BLOCK_SIZE - 1) - (y_offset + (map_y - 1)*TETRIS_BLOCK_SIZE),
                        TETRIS_BLOCK_SIZE, 2*TETRIS_BLOCK_SIZE);
                    break;
            } break;

        case 6: //L block
            switch(rotation)
            {
                case NO_ROTATION:
                    u8g2_DrawBox(&u8g2, x_offset + (map_x + 1)*TETRIS_BLOCK_SIZE,
                        DISPLAY_HEIGHT - (TETRIS_BLOCK_SIZE - 1) - (y_offset + map_y*TETRIS_BLOCK_SIZE),
                        TETRIS_BLOCK_SIZE, 2*TETRIS_BLOCK_SIZE);
                    u8g2_DrawBox(&u8g2, x_offset + (map_x - 1)*TETRIS_BLOCK_SIZE,
                        DISPLAY_HEIGHT - (TETRIS_BLOCK_SIZE - 1) - (y_offset + (map_y - 1)*TETRIS_BLOCK_SIZE),
                        2*TETRIS_BLOCK_SIZE, TETRIS_BLOCK_SIZE);
                    break;
                case RIGHT_90:
                    u8g2_DrawBox(&u8g2, x_offset + map_x*TETRIS_BLOCK_SIZE,
                        DISPLAY_HEIGHT - (TETRIS_BLOCK_SIZE - 1) - (y_offset + (map_y - 2)*TETRIS_BLOCK_SIZE),
                        2*TETRIS_BLOCK_SIZE, TETRIS_BLOCK_SIZE);
                    u8g2_DrawBox(&u8g2, x_offset + map_x*TETRIS_BLOCK_SIZE,
                        DISPLAY_HEIGHT - (TETRIS_BLOCK_SIZE - 1) - (y_offset + map_y*TETRIS_BLOCK_SIZE),
                        TETRIS_BLOCK_SIZE, 2*TETRIS_BLOCK_SIZE);
                    break;
                case UPSIDE_DOWN:
                    u8g2_DrawBox(&u8g2, x_offset + (map_x - 1)*TETRIS_BLOCK_SIZE,
                        DISPLAY_HEIGHT - (TETRIS_BLOCK_SIZE - 1) - (y_offset + map_y*TETRIS_BLOCK_SIZE),
                        TETRIS_BLOCK_SIZE, 2*TETRIS_BLOCK_SIZE);
                    u8g2_DrawBox(&u8g2, x_offset + map_x*TETRIS_BLOCK_SIZE,
                        DISPLAY_HEIGHT - (TETRIS_BLOCK_SIZE - 1) - (y_offset + map_y*TETRIS_BLOCK_SIZE),
                        2*TETRIS_BLOCK_SIZE, TETRIS_BLOCK_SIZE);
                    break;
                case LEFT_90:
                    u8g2_DrawBox(&u8g2, x_offset + map_x*TETRIS_BLOCK_SIZE,
                        DISPLAY_HEIGHT - (TETRIS_BLOCK_SIZE - 1) - (y_offset + map_y*TETRIS_BLOCK_SIZE),
                        2*TETRIS_BLOCK_SIZE, TETRIS_BLOCK_SIZE);
                    u8g2_DrawBox(&u8g2, x_offset + (map_x + 1)*TETRIS_BLOCK_SIZE,
                        DISPLAY_HEIGHT - (TETRIS_BLOCK_SIZE - 1) - (y_offset + (map_y - 1)*TETRIS_BLOCK_SIZE),
                        TETRIS_BLOCK_SIZE, 2*TETRIS_BLOCK_SIZE);
                    break;
            } break;

        case 7: //reverse L block
            switch(rotation)
            {
                case NO_ROTATION:
                    u8g2_DrawBox(&u8g2, x_offset + (map_x - 1)*TETRIS_BLOCK_SIZE,
                        DISPLAY_HEIGHT - (TETRIS_BLOCK_SIZE - 1) - (y_offset + map_y*TETRIS_BLOCK_SIZE),
                        TETRIS_BLOCK_SIZE, 2*TETRIS_BLOCK_SIZE);
                    u8g2_DrawBox(&u8g2, x_offset + map_x*TETRIS_BLOCK_SIZE,
                        DISPLAY_HEIGHT - (TETRIS_BLOCK_SIZE - 1) - (y_offset + (map_y - 1)*TETRIS_BLOCK_SIZE),
                        2*TETRIS_BLOCK_SIZE, TETRIS_BLOCK_SIZE);
                    break;
                case RIGHT_90:
                    u8g2_DrawBox(&u8g2, x_offset + map_x*TETRIS_BLOCK_SIZE,
                        DISPLAY_HEIGHT - (TETRIS_BLOCK_SIZE - 1) - (y_offset + map_y*TETRIS_BLOCK_SIZE),
                        2*TETRIS_BLOCK_SIZE, TETRIS_BLOCK_SIZE);
                    u8g2_DrawBox(&u8g2, x_offset + map_x*TETRIS_BLOCK_SIZE,
                        DISPLAY_HEIGHT - (TETRIS_BLOCK_SIZE - 1) - (y_offset + (map_y - 1)*TETRIS_BLOCK_SIZE),
                        TETRIS_BLOCK_SIZE, 2*TETRIS_BLOCK_SIZE);
                    break;
                case UPSIDE_DOWN:
                    u8g2_DrawBox(&u8g2, x_offset + (map_x + 1)*TETRIS_BLOCK_SIZE,
                        DISPLAY_HEIGHT - (TETRIS_BLOCK_SIZE - 1) - (y_offset + map_y*TETRIS_BLOCK_SIZE),
                        TETRIS_BLOCK_SIZE, 2*TETRIS_BLOCK_SIZE);
                    u8g2_DrawBox(&u8g2, x_offset + (map_x - 1)*TETRIS_BLOCK_SIZE,
                        DISPLAY_HEIGHT - (TETRIS_BLOCK_SIZE - 1) - (y_offset + map_y*TETRIS_BLOCK_SIZE),
                        2*TETRIS_BLOCK_SIZE, TETRIS_BLOCK_SIZE);
                    break;
                case LEFT_90:
                    u8g2_DrawBox(&u8g2, x_offset + (map_x + 1)*TETRIS_BLOCK_SIZE,
                        DISPLAY_HEIGHT - (TETRIS_BLOCK_SIZE - 1) - (y_offset + map_y*TETRIS_BLOCK_SIZE),
                        TETRIS_BLOCK_SIZE, 2*TETRIS_BLOCK_SIZE);
                    u8g2_DrawBox(&u8g2, x_offset + map_x*TETRIS_BLOCK_SIZE,
                        DISPLAY_HEIGHT - (TETRIS_BLOCK_SIZE - 1) - (y_offset + (map_y - 2)*TETRIS_BLOCK_SIZE),
                        2*TETRIS_BLOCK_SIZE, TETRIS_BLOCK_SIZE);
                    break;
            } break;

        case 8: //4x1 long block
            switch(rotation)
            {
                case NO_ROTATION:
                case UPSIDE_DOWN:
                    u8g2_DrawBox(&u8g2, x_offset + (map_x - 1)*TETRIS_BLOCK_SIZE,
                        DISPLAY_HEIGHT - (TETRIS_BLOCK_SIZE - 1) - (y_offset + map_y*TETRIS_BLOCK_SIZE),
                        4*TETRIS_BLOCK_SIZE, TETRIS_BLOCK_SIZE);
                    break;
                case RIGHT_90:
                case LEFT_90:
                    u8g2_DrawBox(&u8g2, x_offset + map_x*TETRIS_BLOCK_SIZE,
                        DISPLAY_HEIGHT - (TETRIS_BLOCK_SIZE - 1) - (y_offset + map_y*TETRIS_BLOCK_SIZE),
                        TETRIS_BLOCK_SIZE, 4*TETRIS_BLOCK_SIZE);
                    break;
            } break;
    }
}

void tetris_draw_background(int score)
{
    if(score > tetris_highscore)
        tetris_highscore = score;
}

void tetris_draw_row_deletion(short int row, short int count, int score)
{
    if(row == -1)
        return;

    for(int i = 0; i < TETRIS_MAP_WIDTH/2; i++)
    {
        for(int j = 0; j < count; j++)
        {
            tetris_map[row + j][TETRIS_MAP_WIDTH/2 + i] = false;
            tetris_map[row + j][TETRIS_MAP_WIDTH/2 - 1 - i] = false;
        }
        u8g2_ClearBuffer(&u8g2);
        tetris_draw_background(score);
        tetris_draw_frame();
        tetris_draw_blocks();
        u8g2_SendBuffer(&u8g2);
    }

    tetris_shift_rows_down(row, count);
    u8g2_ClearBuffer(&u8g2);
    tetris_draw_background(score);
    tetris_draw_frame();
    tetris_draw_blocks();
    u8g2_SendBuffer(&u8g2);
}

bool tetris_block_fits(short int map_x, short int map_y, short int id, block_rotation rotation)
{
    switch(id)
    {
        case 0: //signle block
            if(map_x >= TETRIS_MAP_WIDTH || map_x < 0 || map_y < 0)
                return false;
            if(tetris_map[map_y][map_x])
                return false;
            break;

        case 1: //2x2 block
            if((map_x + 1) >= TETRIS_MAP_WIDTH || map_x < 0 || (map_y - 1) < 0)
                return false;
            if(tetris_map[map_y][map_x] || tetris_map[map_y - 1][map_x + 1] ||
                tetris_map[map_y - 1][map_x] || tetris_map[map_y][map_x + 1])
                return false;
            break;

        case 2: //small L block
            if((map_x + 1) >= TETRIS_MAP_WIDTH || map_x < 0 || (map_y - 1) < 0)
                return false;
            switch(rotation)
            {
                case NO_ROTATION:
                    if(tetris_map[map_y][map_x] || tetris_map[map_y - 1][map_x + 1] || tetris_map[map_y - 1][map_x])
                        return false;
                    break;
                case RIGHT_90:
                    if(tetris_map[map_y][map_x] || tetris_map[map_y - 1][map_x] || tetris_map[map_y][map_x + 1])
                        return false;
                    break;
                case UPSIDE_DOWN:
                    if(tetris_map[map_y][map_x] || tetris_map[map_y - 1][map_x + 1] || tetris_map[map_y][map_x + 1])
                        return false;
                    break;
                case LEFT_90:
                    if(tetris_map[map_y - 1][map_x + 1] || tetris_map[map_y - 1][map_x] || tetris_map[map_y][map_x + 1])
                        return false;
                    break;
            }
            break;

        case 3: //t block
            switch(rotation)
            {
                case NO_ROTATION:
                    if((map_x + 1) >= TETRIS_MAP_WIDTH || (map_x - 1) < 0 || (map_y - 1) < 0)
                        return false;
                    if(tetris_map[map_y][map_x] || tetris_map[map_y - 1][map_x] ||
                        tetris_map[map_y][map_x + 1] || tetris_map[map_y][map_x - 1])
                        return false;
                    break;
                case RIGHT_90:
                    if(map_x >= TETRIS_MAP_WIDTH || (map_x - 1) < 0 || (map_y - 2) < 0)
                        return false;
                    if(tetris_map[map_y][map_x] || tetris_map[map_y - 1][map_x] ||
                        tetris_map[map_y - 2][map_x] || tetris_map[map_y - 1][map_x - 1])
                        return false;
                    break;
                case UPSIDE_DOWN:
                    if((map_x + 1) >= TETRIS_MAP_WIDTH || (map_x - 1) < 0 || (map_y - 1) < 0)
                        return false;
                    if(tetris_map[map_y][map_x] || tetris_map[map_y - 1][map_x] ||
                        tetris_map[map_y - 1][map_x + 1] || tetris_map[map_y - 1][map_x - 1])
                        return false;
                    break;
                case LEFT_90:
                    if((map_x + 1) >= TETRIS_MAP_WIDTH || map_x < 0 || (map_y - 2) < 0)
                        return false;
                    if(tetris_map[map_y][map_x] || tetris_map[map_y - 1][map_x] ||
                        tetris_map[map_y - 2][map_x] || tetris_map[map_y - 1][map_x + 1])
                        return false;
                    break;
            }
            break;

        case 4: //z block
            switch (rotation)
            {
                case NO_ROTATION:
                case UPSIDE_DOWN:
                    if((map_x + 1) >= TETRIS_MAP_WIDTH || (map_x - 1) < 0 || (map_y - 1) < 0)
                        return false;
                    if(tetris_map[map_y][map_x - 1] || tetris_map[map_y][map_x] ||
                        tetris_map[map_y - 1][map_x] || tetris_map[map_y - 1][map_x + 1])
                        return false;
                    break;
                case RIGHT_90:
                case LEFT_90:
                    if(map_x >= TETRIS_MAP_WIDTH || (map_x - 1) < 0 || (map_y - 2) < 0)
                        return false;
                    if(tetris_map[map_y][map_x] || tetris_map[map_y - 1][map_x] ||
                        tetris_map[map_y - 1][map_x - 1] || tetris_map[map_y - 2][map_x - 1])
                        return false;
                    break;
            } break;

        case 5: //reverse z block
            switch (rotation)
            {
                case NO_ROTATION:
                case UPSIDE_DOWN:
                    if((map_x + 1) >= TETRIS_MAP_WIDTH || (map_x - 1) < 0 || (map_y - 1) < 0)
                        return false;
                    if(tetris_map[map_y][map_x + 1] || tetris_map[map_y][map_x] ||
                        tetris_map[map_y - 1][map_x] || tetris_map[map_y - 1][map_x - 1])
                        return false;
                    break;
                case RIGHT_90:
                case LEFT_90:
                    if((map_x + 1) >= TETRIS_MAP_WIDTH || map_x < 0 || (map_y - 2) < 0)
                        return false;
                    if(tetris_map[map_y][map_x] || tetris_map[map_y - 1][map_x] ||
                        tetris_map[map_y - 1][map_x + 1] || tetris_map[map_y - 2][map_x + 1])
                        return false;
                    break;
            } break;

        case 6: //L block
            switch (rotation)
            {
                case NO_ROTATION:
                    if((map_x + 1) >= TETRIS_MAP_WIDTH || (map_x - 1) < 0 || (map_y - 1) < 0)
                        return false;
                    if(tetris_map[map_y - 1][map_x - 1] || tetris_map[map_y - 1][map_x + 1] ||
                        tetris_map[map_y - 1][map_x] || tetris_map[map_y][map_x + 1])
                        return false;
                    break;
                case RIGHT_90:
                    if((map_x + 1) >= TETRIS_MAP_WIDTH || map_x < 0 || (map_y - 2) < 0)
                        return false;
                    if(tetris_map[map_y][map_x] || tetris_map[map_y - 2][map_x + 1] ||
                        tetris_map[map_y - 1][map_x] || tetris_map[map_y - 2][map_x])
                        return false;
                    break;
                case UPSIDE_DOWN:
                    if((map_x + 1) >= TETRIS_MAP_WIDTH || (map_x - 1) < 0 || (map_y - 1) < 0)
                        return false;
                    if(tetris_map[map_y][map_x - 1] || tetris_map[map_y][map_x] ||
                        tetris_map[map_y][map_x + 1] || tetris_map[map_y - 1][map_x - 1])
                        return false;
                    break;
                case LEFT_90:
                    if((map_x + 1) >= TETRIS_MAP_WIDTH || map_x < 0 || (map_y - 2) < 0)
                        return false;
                    if(tetris_map[map_y][map_x + 1] || tetris_map[map_y - 1][map_x + 1] ||
                        tetris_map[map_y - 2][map_x + 1] || tetris_map[map_y][map_x])
                        return false;
                    break;
            } break;

        case 7: //reverse L block
            switch (rotation)
            {
                case NO_ROTATION:
                    if((map_x + 1) >= TETRIS_MAP_WIDTH || (map_x - 1) < 0 || (map_y - 1) < 0)
                        return false;
                    if(tetris_map[map_y][map_x - 1] || tetris_map[map_y - 1][map_x - 1] ||
                        tetris_map[map_y - 1][map_x] || tetris_map[map_y - 1][map_x + 1])
                        return false;
                    break;
                case RIGHT_90:
                    if((map_x + 1) >= TETRIS_MAP_WIDTH || map_x < 0 || (map_y - 2) < 0)
                        return false;
                    if(tetris_map[map_y][map_x] || tetris_map[map_y][map_x + 1] ||
                        tetris_map[map_y - 1][map_x] || tetris_map[map_y - 2][map_x])
                        return false;
                    break;
                case UPSIDE_DOWN:
                    if((map_x + 1) >= TETRIS_MAP_WIDTH || (map_x - 1) < 0 || (map_y - 1) < 0)
                        return false;
                    if(tetris_map[map_y][map_x - 1] || tetris_map[map_y][map_x] ||
                        tetris_map[map_y][map_x + 1] || tetris_map[map_y - 1][map_x + 1])
                        return false;
                    break;
                case LEFT_90:
                    if((map_x + 1) >= TETRIS_MAP_WIDTH || map_x < 0 || (map_y - 2) < 0)
                        return false;
                    if(tetris_map[map_y][map_x + 1] || tetris_map[map_y - 1][map_x + 1] ||
                        tetris_map[map_y - 2][map_x] || tetris_map[map_y - 2][map_x + 1])
                        return false;
                    break;
            } break;

        case 8: //4x1 long block
            switch (rotation)
            {
                case NO_ROTATION:
                case UPSIDE_DOWN:
                    if((map_x + 2) >= TETRIS_MAP_WIDTH || (map_x - 1) < 0 || map_y < 0)
                        return false;
                    if(tetris_map[map_y][map_x - 1] || tetris_map[map_y][map_x] ||
                        tetris_map[map_y][map_x + 1] || tetris_map[map_y][map_x + 2])
                        return false;
                    break;
                case RIGHT_90:
                case LEFT_90:
                    if(map_x >= TETRIS_MAP_WIDTH || map_x < 0 || (map_y - 3) < 0)
                        return false;
                    if(tetris_map[map_y][map_x] || tetris_map[map_y - 1][map_x] ||
                        tetris_map[map_y - 2][map_x] || tetris_map[map_y - 3][map_x])
                        return false;
                    break;
            } break;
    }
    return true;
}

void tetris_deactivate_block(short int map_x, short int map_y, short int id, block_rotation rotation)
{
    switch(id)
    {
        case 0: //single block
            tetris_map[map_y][map_x] = true;
            break;

        case 1: //2x2 block
            tetris_map[map_y][map_x] = true;
            tetris_map[map_y][map_x + 1] = true;
            tetris_map[map_y - 1][map_x] = true;
            tetris_map[map_y - 1][map_x + 1] = true;
            break;

        case 2: //small L block
            switch(rotation)
            {
                case NO_ROTATION:
                    tetris_map[map_y][map_x] = true;
                    tetris_map[map_y - 1][map_x] = true;
                    tetris_map[map_y - 1][map_x + 1] = true;
                    break;
                case RIGHT_90:
                    tetris_map[map_y][map_x] = true;
                    tetris_map[map_y][map_x + 1] = true;
                    tetris_map[map_y - 1][map_x] = true;
                    break;
                case UPSIDE_DOWN:
                    tetris_map[map_y][map_x] = true;
                    tetris_map[map_y][map_x + 1] = true;
                    tetris_map[map_y - 1][map_x + 1] = true;
                    break;
                case LEFT_90:
                    tetris_map[map_y][map_x + 1] = true;
                    tetris_map[map_y - 1][map_x] = true;
                    tetris_map[map_y - 1][map_x + 1] = true;
                    break;
            } break;

        case 3: //t block
            switch(rotation)
            {
                case NO_ROTATION:
                    tetris_map[map_y][map_x] = true;
                    tetris_map[map_y - 1][map_x] = true;
                    tetris_map[map_y][map_x + 1] = true;
                    tetris_map[map_y][map_x - 1] = true;
                    break;
                case RIGHT_90:
                    tetris_map[map_y][map_x] = true;
                    tetris_map[map_y - 1][map_x] = true;
                    tetris_map[map_y - 2][map_x] = true;
                    tetris_map[map_y - 1][map_x - 1] = true;
                    break;
                case UPSIDE_DOWN:
                    tetris_map[map_y][map_x] = true;
                    tetris_map[map_y - 1][map_x] = true;
                    tetris_map[map_y - 1][map_x + 1] = true;
                    tetris_map[map_y - 1][map_x - 1] = true;
                    break;
                case LEFT_90:
                    tetris_map[map_y][map_x] = true;
                    tetris_map[map_y - 1][map_x] = true;
                    tetris_map[map_y - 2][map_x] = true;
                    tetris_map[map_y - 1][map_x + 1] = true;
                    break;
            } break;
        
        case 4: //z block
            tetris_map[map_y][map_x] = true;
            tetris_map[map_y - 1][map_x] = true;
            switch(rotation)
            {
                case NO_ROTATION:
                case UPSIDE_DOWN:
                    tetris_map[map_y][map_x - 1] = true;
                    tetris_map[map_y - 1][map_x + 1] = true;
                    break;
                case RIGHT_90:
                case LEFT_90:
                    tetris_map[map_y - 1][map_x - 1] = true;
                    tetris_map[map_y - 2][map_x - 1] = true;
                    break;
            } break;

        case 5: //reverse z block
            tetris_map[map_y][map_x] = true;
            tetris_map[map_y - 1][map_x] = true;
            switch(rotation)
            {
                case NO_ROTATION:
                case UPSIDE_DOWN:
                    tetris_map[map_y][map_x + 1] = true;
                    tetris_map[map_y - 1][map_x - 1] = true;
                    break;
                case RIGHT_90:
                case LEFT_90:
                    tetris_map[map_y - 1][map_x + 1] = true;
                    tetris_map[map_y - 2][map_x + 1] = true;
                    break;
            } break;
        
        case 6: //L block
            switch(rotation)
            {
                case NO_ROTATION:
                    tetris_map[map_y][map_x + 1] = true;
                    tetris_map[map_y - 1][map_x - 1] = true;
                    tetris_map[map_y - 1][map_x] = true;
                    tetris_map[map_y - 1][map_x + 1] = true;
                    break;
                case RIGHT_90:
                    tetris_map[map_y][map_x] = true;
                    tetris_map[map_y - 2][map_x + 1] = true;
                    tetris_map[map_y - 1][map_x] = true;
                    tetris_map[map_y - 2][map_x] = true;
                    break;
                case UPSIDE_DOWN:
                    tetris_map[map_y][map_x - 1] = true;
                    tetris_map[map_y][map_x] = true;
                    tetris_map[map_y][map_x + 1] = true;
                    tetris_map[map_y - 1][map_x - 1] = true;
                    break;
                case LEFT_90:
                    tetris_map[map_y][map_x] = true;
                    tetris_map[map_y][map_x + 1] = true;
                    tetris_map[map_y - 1][map_x + 1] = true;
                    tetris_map[map_y - 2][map_x + 1] = true;
                    break;
            } break;

        case 7: //reverse L block
            switch(rotation)
            {
                case NO_ROTATION:
                    tetris_map[map_y][map_x - 1] = true;
                    tetris_map[map_y - 1][map_x - 1] = true;
                    tetris_map[map_y - 1][map_x] = true;
                    tetris_map[map_y - 1][map_x + 1] = true;
                    break;
                case RIGHT_90:
                    tetris_map[map_y][map_x] = true;
                    tetris_map[map_y][map_x + 1] = true;
                    tetris_map[map_y - 1][map_x] = true;
                    tetris_map[map_y - 2][map_x] = true;
                    break;
                case UPSIDE_DOWN:
                    tetris_map[map_y][map_x - 1] = true;
                    tetris_map[map_y][map_x] = true;
                    tetris_map[map_y][map_x + 1] = true;
                    tetris_map[map_y - 1][map_x + 1] = true;
                    break;
                case LEFT_90:
                    tetris_map[map_y][map_x + 1] = true;
                    tetris_map[map_y - 1][map_x + 1] = true;
                    tetris_map[map_y - 2][map_x] = true;
                    tetris_map[map_y - 2][map_x + 1] = true;
                    break;
            } break;

        case 8: //4x1 long block
            switch(rotation)
            {
                case NO_ROTATION:
                case UPSIDE_DOWN:
                    tetris_map[map_y][map_x - 1] = true;
                    tetris_map[map_y][map_x] = true;
                    tetris_map[map_y][map_x + 1] = true;
                    tetris_map[map_y][map_x + 2] = true;
                    break;
                case RIGHT_90:
                case LEFT_90:
                    tetris_map[map_y][map_x] = true;
                    tetris_map[map_y - 1][map_x] = true;
                    tetris_map[map_y - 2][map_x] = true;
                    tetris_map[map_y - 3][map_x] = true;
                    break;
            } break;
    }
}

int tetris_check_row_completion(int score)
{
    short int consecutive_rows = 1;
    short int starting_row = -1;
    bool completed_row;
    for(int row = 0; row < TETRIS_MAP_HEIGHT; row++)
    {
        completed_row = 1;
        for(int col = 0; col < TETRIS_MAP_WIDTH; col++)
        {
            if(!tetris_map[row][col])
            {
                completed_row = 0;
                break;
            }
        }

        if(starting_row != -1)
        {
            if(completed_row)
                consecutive_rows += 1;
            else
                break;
        }
        else if(completed_row)
            starting_row = row;
    }

    tetris_draw_row_deletion(starting_row, consecutive_rows, score);
    
    if(starting_row == -1)
        return score;

    switch(consecutive_rows)
    {
        case 1:
            score += 100; break;
        case 2:
            score += 300; break;
        case 3:
            score += 600; break;    
        case 4:
            score += 1000; break;
    }
    return score;
}

void app_main(void)
{
    init_buttons();
    init_display();
    init_low_power_mode();
    srand(time(0));

    int score;
    short int block_id, block_x, block_y;
    short int next_id, next_x, next_y;
    short int speed, ticks_till_fall;
    block_rotation rotation, next_rotation;

    while(true)
    {
        //initialize variables
        score = 0, speed = 1;
        ticks_till_fall = TETRIS_MAX_SPEED + 1 - speed;
        block_x = -1, block_y = -1, next_x = -1, next_y = -1;
        block_id = rand() % TETRIS_NUMBER_OF_BLOCKS;
        next_id = rand() % TETRIS_NUMBER_OF_BLOCKS;
        memset(tetris_map, 0, sizeof(tetris_map));
        rotation = NO_ROTATION, next_rotation = NO_ROTATION;

        tetris_start_screen();

        //wait for button press to start the game
        esp_light_sleep_start();

        //main game loop
        while(true)
        {
            u8g2_ClearBuffer(&u8g2);

            //process user inupt
            if(gpio_get_level(DOWN_BUTTON))
                next_y = block_y - 1;
            if(gpio_get_level(LEFT_BUTTON))
                next_x = block_x - 1;
            if(gpio_get_level(RIGHT_BUTTON))
                next_x = block_x + 1;
            if(gpio_get_level(UP_BUTTON))
                switch(rotation)
                {
                    case NO_ROTATION:
                        next_rotation = RIGHT_90; break;
                    case RIGHT_90:
                        next_rotation = UPSIDE_DOWN; break;
                    case UPSIDE_DOWN:
                        next_rotation = LEFT_90; break;
                    case LEFT_90:
                        next_rotation = NO_ROTATION; break;
                }

            if(block_id == -1)
            {
                block_id = next_id;
                next_id = rand() % TETRIS_NUMBER_OF_BLOCKS;
                block_x = TETRIS_MAP_WIDTH / 2;
                block_y = TETRIS_MAP_HEIGHT - 1;
                rotation = NO_ROTATION;
                next_x = block_x, next_y = block_y, next_rotation = rotation;
                if(!tetris_block_fits(block_x, block_y, block_id, rotation))
                {
                    break;
                }
            }

            ticks_till_fall--;
            if(ticks_till_fall == 0)
            {
                ticks_till_fall = TETRIS_MAX_SPEED + 1 - speed;
                if(next_y == block_y)
                    next_y--;
            }

            if(next_x != block_x)
                if(tetris_block_fits(next_x, block_y, block_id, rotation))
                    block_x = next_x;
            if(next_rotation != rotation)
                if(tetris_block_fits(block_x, block_y, block_id, next_rotation))
                    rotation = next_rotation;
            if(next_y < block_y)
            {
                if(tetris_block_fits(block_x, next_y, block_id, rotation))
                    block_y = next_y;
                else
                {
                    tetris_deactivate_block(block_x, block_y, block_id, rotation);
                    block_y = -1, block_x = -1, block_id = -1;
                    next_x = -1, next_y = -1;
                }
            }
            //rollback all unsuccessful states
            next_x = block_x, next_y = block_y, next_rotation = rotation;

            //render eveything
            tetris_draw_active_block(block_x, block_y, block_id, rotation);
            tetris_draw_background(score);
            tetris_draw_frame();
            tetris_draw_blocks();
            u8g2_SendBuffer(&u8g2);

            //check for completed rows
            if(block_id == -1)
                score = tetris_check_row_completion(score);
        }

        tetris_end_screen(score);

        //wait for exit the game or play again button press
        esp_light_sleep_start();
        //if(!gpio_get_level(LEFT_BUTTON))
            //break;
    }
}
