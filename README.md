ESP32 Tetris

A retro Tetris game running on an ESP32 — just a fun personal project!

I’m building a classic Tetris game on the ESP32. It uses the u8g2 library to handle the OLED 128x64 display via I2C. The goal of this project was to learn embedded programming and have some fun making a game console featuring three games.

What you need:

    - ESP32 board

    - OLED display (compatible with u8g2, like an SSD1306)

    - Four buttons

    - ESP-IDF setup (version 3.16 or higher)

    - u8g2 library added as a component
    (You’ll need to modify the CMakeLists.txt path to point to your u8g2 library location. Mine is in oled_test/components outside of this repo, but you can put it wherever you like.)


How to run it

Grab the code:

    git clone https://github.com/filippe12/esp32_tetris.git
    cd esp32_snake

Make sure you have ESP-IDF set up.

Build and flash:

    idf.py build
    idf.py flash


A few notes:

This is just a fun side project to mess around with the ESP32 and OLED displays. Feel free to poke around, suggest improvements, or just enjoy the code.


License:

MIT — do what you want with it!
