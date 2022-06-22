#include <Arduino.h>
#include <Audio.h>
#include <SD.h>
#include "ILI9341_t3n.h"
#include "USBHost_t36.h"
#include "frame.h"

extern "C"
{
//SameBoy seems to have trouble compiling with a mix of cplusplus/c.
//Hacks to fix. Dont access GB_gameboy_t struct from cpp files!
#undef __cplusplus
#include "Core/gb.h"
void same_boy_setup_memory(GB_gameboy_t *gb, uint8_t *rom, uint32_t rom_size, uint8_t *ram, uint8_t *vram);
#define __cplusplus
}


/* Gameboy Emulator */
GB_gameboy_t gameboy;
uint32_t active_pixel_buffer[160 * 144];
EXTMEM uint8_t gb_rom[1024 * 1024 * 2]; //Max size 8MB
uint8_t gb_ram[0x1000 * 8];
uint8_t gb_vram[0x2000 * 2];
//FIXME. Dont really want hardcoded
#ifndef ROMNAME
#define ROMNAME "myrom.gbc"
#endif
#ifndef SAVNAME
#define SAVNAME "myrom.sav"
#endif

/* Video (TFT Display) */
#define ENABLE_TFT_DISPLAY 1
#define TFT_ROTATION 1 //0-3
#define TFT_DC 38
#define TFT_CS 40
#define TFT_MOSI 26
#define TFT_SCK 27
#define TFT_MISO 39
#define TFT_RST 41
ILI9341_t3n tft = ILI9341_t3n(TFT_CS, TFT_DC, TFT_RST, TFT_MOSI, TFT_SCK, TFT_MISO);
DMAMEM uint16_t tft_buffer[320 * 240];

/* Audio */
AudioPlayQueue left_channel, right_channel;
AudioMixer4 mixer;
AudioOutputMQS mqs1;
//Connect left and right channels to mixer
AudioConnection patchCord1(left_channel, 0, mixer, 0);
AudioConnection patchCord2(right_channel, 0, mixer, 1);
//Connect output of mixer to MQS1 output (Pin 12 left channel (chan 0), Pin 10 right channel (chan 1))
AudioConnection patchCord3(mixer, 0, mqs1, 0);

/* USB Host */
USBHost usbh;
USBHub hub1(usbh);
JoystickController joy1(usbh);

//Static prototypes
static void load_boot_rom(GB_gameboy_t *gb, GB_boot_rom_t type);
static void vblank(GB_gameboy_t *gb);
static uint32_t rgb_encode(GB_gameboy_t *gb, uint8_t r, uint8_t g, uint8_t b);
static void rumble(GB_gameboy_t *gb, double amp);
static void handle_events(GB_gameboy_t *gb);
static void gb_audio_callback(GB_gameboy_t *gb, GB_sample_t *sample);
static void write_to_file(const char *filename, uint8_t *data, uint32_t len);
static void read_from_file(const char *filename, uint32_t file_offset, uint8_t *data, int32_t len);

void setup()
{
    //Start serial. Wait 3 seconds for user to connect or continue
    while (!Serial && millis() < 3000);
    Serial.begin(9600);

    Serial.printf("Start USB Host\n");
    usbh.begin();

    Serial.printf("Mount SD Card\n");
    if (SD.sdfs.begin(SdioConfig(FIFO_SDIO)) == false)
    {
        Serial.printf("ERROR: Could not mount SD Card\n");
        while (1) yield();
    }

    //Allocate memory for the audio system
    AudioMemory(16);
    mixer.gain(0,0.5f);
    mixer.gain(1,0.5f);

    Serial.printf("Setup Emulator\n");
    memset(active_pixel_buffer, 0xFF, sizeof(active_pixel_buffer));

    static GB_model_t model = GB_MODEL_CGB_E;
    GB_init(&gameboy, model);

    //Setup configuration
    GB_set_rumble_mode(&gameboy, GB_RUMBLE_ALL_GAMES);
    GB_set_sample_rate(&gameboy, ((int)AUDIO_SAMPLE_RATE_EXACT) * 2);
    GB_set_color_correction_mode(&gameboy, GB_COLOR_CORRECTION_DISABLED);
    GB_set_palette(&gameboy, &GB_PALETTE_MGB);
    GB_set_border_mode(&gameboy, GB_BORDER_NEVER);
    GB_set_highpass_filter_mode(&gameboy, GB_HIGHPASS_OFF);

    //Set callbacks
    GB_set_boot_rom_load_callback(&gameboy, load_boot_rom);
    GB_set_vblank_callback(&gameboy, (GB_vblank_callback_t) vblank);
    GB_set_pixels_output(&gameboy, active_pixel_buffer);
    GB_set_rgb_encode_callback(&gameboy, rgb_encode);
    GB_set_rumble_callback(&gameboy, rumble);
    GB_apu_set_sample_callback(&gameboy, gb_audio_callback);
    GB_set_update_input_hint_callback(&gameboy, handle_events);

    //Load ROM
    uint8_t rom_size[1] = {0xFF}; uint32_t rom_len = 0;
    //Read ROM size from header (offset 0x148)
    read_from_file(ROMNAME, 0x0148, rom_size, 1);
    switch (rom_size[0])
    {
        case (0x00): rom_len = 32768UL;   break;
        case (0x01): rom_len = 65536UL;   break;
        case (0x02): rom_len = 131072UL;  break;
        case (0x03): rom_len = 262144UL;  break;
        case (0x04): rom_len = 524288UL;  break;
        case (0x05): rom_len = 1048576UL; break;
        case (0x06): rom_len = 2097152UL; break;
        case (0x07): rom_len = 4194304UL; break;
        case (0x08): rom_len = 8388608UL; break;
        default: rom_len = 0;
    }

    if (rom_len == 0)
    {
        while(1)
        {
            Serial.printf("Error reading %s\n", ROMNAME);
            yield();
            delay(500);
        }
    }

    same_boy_setup_memory(&gameboy, gb_rom, rom_len, gb_ram, gb_vram);
    read_from_file(ROMNAME, 0, gb_rom, rom_len);
    Serial.printf("GB Name: %.15s\n", &gb_rom[0x0134]);
    Serial.printf("ROM Byte Code: %lu\n", gb_rom[0x0148]);
    Serial.printf("RAM Size Code: %lu\n", gb_rom[0x0149]);
    Serial.printf("MBC Type: 0x%02x\n", gb_rom[0x0147]);
    GB_configure_cart(&gameboy);

    //Load SRAM if needed
    Serial.printf("Battery Save Size: %u bytes\n", GB_save_battery_size(&gameboy));
    if (GB_save_battery_size(&gameboy) > 0)
    {
        uint8_t *gb_sram = (uint8_t *)malloc(GB_save_battery_size(&gameboy));
        read_from_file(SAVNAME, 0, gb_sram, GB_save_battery_size(&gameboy));
        GB_load_battery_from_buffer(&gameboy, gb_sram, GB_save_battery_size(&gameboy));
        free(gb_sram);
    }

    //Start TFT display and draw static frame
    memset(tft_buffer, 0x00, sizeof(tft_buffer));
    tft.begin();
    tft.setRotation(1); //0-3
    tft.setFrameBuffer(tft_buffer);
    tft.useFrameBuffer(true);
    tft.writeRect(0, 0, 320, 240, (uint16_t*)frame);
    tft.fillRect((320 - 160) / 2, 33, 160, 144, ILI9341_BLACK);
    tft.updateScreen();
}

void loop()
{
    GB_run(&gameboy);
}

static void load_boot_rom(GB_gameboy_t *_gb, GB_boot_rom_t type)
{
    static const char *const names[] = {
        [GB_BOOT_ROM_DMG_0] = "dmg0_boot.bin",
        [GB_BOOT_ROM_DMG]  = "dmg_boot.bin",
        [GB_BOOT_ROM_MGB]  = "mgb_boot.bin",
        [GB_BOOT_ROM_SGB]  = "sgb_boot.bin",
        [GB_BOOT_ROM_SGB2] = "sgb2_boot.bin",
        [GB_BOOT_ROM_CGB_0] = "cgb0_boot.bin",
        [GB_BOOT_ROM_CGB]  = "cgb_boot.bin",
        [GB_BOOT_ROM_AGB]  = "agb_boot.bin",
    };
    uint8_t boot_rom[0x900];
    read_from_file(names[type], 0, boot_rom, sizeof(boot_rom));
    GB_load_boot_rom_from_buffer(_gb, boot_rom, sizeof(boot_rom));
}

static void vblank(GB_gameboy_t *gb)
{
    handle_events(gb);

    static uint32_t frame_skip = 0;
    if (frame_skip++ % 2 || tft.asyncUpdateActive())
        return;

    uint32_t line = 0;
    uint32_t pixel = 0;
    //FIXME: Ideally we should DMA directly from active_pixel_buffer
    //but would require some changes to the SameBoy core code.
    for (uint32_t y = 0; y < 144; y++)
    {
        for (uint32_t x = 0; x < 160; x++)
        {
            tft_buffer[line + x] = (uint16_t)active_pixel_buffer[pixel++];
        }
        line += 160;
    }
    tft.updateScreenAsync();
}

static uint32_t rgb_encode(GB_gameboy_t *gb, uint8_t r, uint8_t g, uint8_t b)
{
    //Encode to RGB565 for the TFT
    uint16_t B = (uint16_t)((b >> 3) << 0);
    uint16_t G = (uint16_t)((g >> 2) << 5);
    uint16_t R = (uint16_t)((r >> 3) << 11);
    return (uint32_t)(R | G | B);
}

static void rumble(GB_gameboy_t *gb, double amp)
{
    joy1.setRumble(amp, amp, 20);
}

static void handle_events(GB_gameboy_t *gb)
{
    if (joy1 == true)
    {
        uint32_t b = joy1.getButtons();
        joy1.joystickDataClear();
        //Hard coded for Xbox360 wired/wiress controller
        GB_set_key_state(&gameboy, GB_KEY_UP, (b & (1 << 0)));
        GB_set_key_state(&gameboy, GB_KEY_DOWN, (b & (1 << 1)));
        GB_set_key_state(&gameboy, GB_KEY_LEFT, (b & (1 << 2)));
        GB_set_key_state(&gameboy, GB_KEY_RIGHT, (b & (1 << 3)));
        GB_set_key_state(&gameboy, GB_KEY_START, (b & (1 << 4)));
        GB_set_key_state(&gameboy, GB_KEY_SELECT, (b & (1 << 5)));
        GB_set_key_state(&gameboy, GB_KEY_A, (b & (1 << 12)));
        GB_set_key_state(&gameboy, GB_KEY_B, (b & (1 << 13)));

        //Save SRAM if you press Y
        static uint32_t save_timer = 0;
        if ((b & (1 << 15)) && GB_save_battery_size(&gameboy) > 0 && (millis() - save_timer) > 1000)
        {
            uint8_t *save = (uint8_t *)malloc(GB_save_battery_size(&gameboy));
            GB_save_battery_to_buffer(&gameboy, save, GB_save_battery_size(&gameboy));
            write_to_file(SAVNAME, save, GB_save_battery_size(&gameboy));
            free(save);
            save_timer = millis();
        }
    }
}

static void gb_audio_callback(GB_gameboy_t *gb, GB_sample_t *sample)
{
    //FIXME: Untested.
    static const int32_t BUF_SIZE = 128;
    static int16_t left_buffer[BUF_SIZE];
    static int16_t right_buffer[BUF_SIZE];
    static int16_t buffer_pos = 0;
    static int16_t *L = NULL;
    static int16_t *R = NULL;

    left_buffer[buffer_pos] = sample->left;
    right_buffer[buffer_pos] = sample->right;
    buffer_pos++;

    if (buffer_pos == BUF_SIZE)
    {
        //Get audio buffers
        L = left_channel.getBuffer();
        R = right_channel.getBuffer();

        //Push data into audio system
        if (L != NULL)
            memcpy(L, left_buffer, sizeof(left_buffer));
        if (R != NULL)
            memcpy(R, right_buffer, sizeof(right_buffer));

        //Play the two channels. These will be mixed and output on MQS1 channel 0 (Pin 12)
        if (L != NULL)
            left_channel.playBuffer();
        if (R != NULL)
            right_channel.playBuffer();

        buffer_pos = 0;
    }
}

static void write_to_file(const char *filename, uint8_t *data, uint32_t len)
{
    FsFile fil = SD.sdfs.open(filename, O_WRITE | O_CREAT);
    if (fil == false)
    {
        Serial.printf("ERROR: Could not open %s for WRITE\n", filename);
        while(1) yield();
    }
    if (fil.write(data, len) != len)
    {
        Serial.printf("ERROR: Could not write to %s\n", filename);
        while(1) yield();
    }
    fil.close();
}

static void read_from_file(const char *filename, uint32_t file_offset, uint8_t *data, int32_t len)
{
    FsFile fil = SD.sdfs.open(filename, O_READ);
    if (fil == false)
    {
        Serial.printf("ERROR: Could not open %s for READ\n", filename);
        //Who cares about errors for save games
        if (strcmp(filename, SAVNAME) != 0)
            while(1) yield();
        else
            return;
    }

    fil.seekSet(file_offset);

    if (fil.read(data, len) != (int)len)
    {
        Serial.printf("ERROR: Could not read from %s\n", filename);
        while(1) yield();
    }
    fil.close();
}
