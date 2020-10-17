#include <Arduino.h>
#include "Core/gb.h"

//I dont really know why this is needed. But accessing GB_gameboy_t struct from the cpp file does not work.
//The struct changes between cpp and c files doe to the GB_SECTION def.
void same_boy_setup_memory(GB_gameboy_t *gb, uint8_t *rom, uint32_t rom_size)
{
    gb->rom = rom;
    gb->rom_size = rom_size;
}