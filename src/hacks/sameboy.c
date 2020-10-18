#include <Arduino.h>
#include "Core/gb.h"

//I dont really know why this is needed. But accessing GB_gameboy_t struct from the cpp file does not work.
//The struct changes between cpp and c files due to the GB_SECTION def in gb.h.
void same_boy_setup_memory(GB_gameboy_t *gb, uint8_t *rom, uint32_t rom_size, uint8_t *ram, uint8_t *vram)
{
    gb->rom = rom;
    gb->rom_size = rom_size;

    //Replace the malloc'd RAM and VRAM with static memory. On Teensy4 this is much faster RAM.
    if (gb->ram)
    {
        memcpy(ram, gb->ram, gb->ram_size);
        free(gb->ram);
    }

    if (gb->vram)
    {
        memcpy(vram, gb->vram, gb->vram_size);
        free(gb->vram);
    }

    gb->ram = ram;
    gb->vram = vram;

}
