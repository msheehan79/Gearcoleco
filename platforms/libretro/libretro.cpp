/*
 * Gearcoleco - ColecoVision Emulator
 * Copyright (C) 2021  Ignacio Sanchez

 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * any later version.

 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.

 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see http://www.gnu.org/licenses/
 *
 */

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <math.h>

#include <stdio.h>
#include "libretro.h"

#include "../../src/gearcoleco.h"

#ifdef _WIN32
static const char slash = '\\';
#else
static const char slash = '/';
#endif

#define MAX_PADS 2
#define JOYPAD_BUTTONS 16

static struct retro_log_callback logging;
static retro_log_printf_t log_cb;
static char retro_system_directory[4096];
static char retro_game_path[4096];

static s16 audio_buf[GC_AUDIO_BUFFER_SIZE];
static int audio_sample_count = 0;
static int current_screen_width = 0;
static int current_screen_height = 0;
static bool allow_up_down = false;
static bool libretro_supports_bitmasks;

static GearcolecoCore* core;
static u8* frame_buffer;
static Cartridge::ForceConfiguration config;

static int joypad[MAX_PADS][JOYPAD_BUTTONS];
static int joypre[MAX_PADS][JOYPAD_BUTTONS];
static int joypad_ext[MAX_PADS][4];
static int joypre_ext[MAX_PADS][4];

static GC_Keys keymap[] = {
    Key_Up,
    Key_Down,
    Key_Left,
    Key_Right,
    Key_Right_Button,
    Key_Left_Button,
    Keypad_2,
    Keypad_1,
    Keypad_Asterisk,
    Keypad_Hash,
    Keypad_3,
    Keypad_4,
    Keypad_5,
    Keypad_6,
    Keypad_7,
    Keypad_8,
    Keypad_0,
    Keypad_9,
    Key_Blue,
    Key_Purple
};

static void fallback_log(enum retro_log_level level, const char *fmt, ...)
{
    (void)level;
    va_list va;
    va_start(va, fmt);
    vfprintf(stderr, fmt, va);
    va_end(va);
}

static const struct retro_variable vars[] = {
    { "gearcoleco_timing", "Refresh Rate (restart); Auto|NTSC (60 Hz)|PAL (50 Hz)" },
    { "gearcoleco_up_down_allowed", "Allow Up+Down / Left+Right; Disabled|Enabled" },
    { "gearcoleco_no_sprite_limit", "No Sprite Limit; Disabled|Enabled" },
    { NULL }
};

static retro_environment_t environ_cb;

void retro_init(void)
{
    const char *dir = NULL;

    if (environ_cb(RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY, &dir) && dir) {
        snprintf(retro_system_directory, sizeof(retro_system_directory), "%s", dir);
    }
    else {
        snprintf(retro_system_directory, sizeof(retro_system_directory), "%s", ".");
    }

    core = new GearcolecoCore();

#ifdef PS2
    core->Init(GC_PIXEL_BGR555);
#else
    core->Init(GC_PIXEL_RGB565);
#endif

    frame_buffer = new u8[GC_RESOLUTION_MAX_WIDTH * GC_RESOLUTION_MAX_HEIGHT * 2];

    audio_sample_count = 0;

    config.region = Cartridge::CartridgeUnknownRegion;

    libretro_supports_bitmasks = environ_cb(RETRO_ENVIRONMENT_GET_INPUT_BITMASKS, NULL);
}

void retro_deinit(void)
{
    SafeDeleteArray(frame_buffer);
    SafeDelete(core);
}

unsigned retro_api_version(void)
{
    return RETRO_API_VERSION;
}

void retro_set_controller_port_device(unsigned port, unsigned device)
{
    log_cb(RETRO_LOG_INFO, "Plugging device %u into port %u.\n", device, port);

    struct retro_input_descriptor joypad[] = {

        { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP,                              "Joystick Up" },
        { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN,                            "Joystick Down" },
        { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT,                            "Joystick Left" },
        { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT,                           "Joystick Right" },
        { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B,                               "Yellow (Left)" },
        { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A,                               "Red (Right)" },
        { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_Y,                               "Keypad 1" },
        { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_X,                               "Keypad 2" },
        { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L,                               "Keypad 3" },
        { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R,                               "Keypad 4" },
        { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L2,                              "Keypad 5" },
        { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R2,                              "Keypad 6" },
        { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L3,                              "Keypad 7" },
        { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R3,                              "Keypad 8" },
        { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_START,                           "Keypad *" },
        { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_SELECT,                          "Keypad #" },
        { 0, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_Y,  "Keypad 9" },
        { 0, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_X,  "Keypad 0" },
        { 0, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_Y, "Purple" },
        { 0, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_X, "Blue" },

        { 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP,                              "Joystick Up" },
        { 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN,                            "Joystick Down" },
        { 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT,                            "Joystick Left" },
        { 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT,                           "Joystick Right" },
        { 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B,                               "Yellow (Left)" },
        { 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A,                               "Red (Right)" },
        { 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_Y,                               "Keypad 1" },
        { 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_X,                               "Keypad 2" },
        { 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L,                               "Keypad 3" },
        { 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R,                               "Keypad 4" },
        { 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L2,                              "Keypad 5" },
        { 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R2,                              "Keypad 6" },
        { 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L3,                              "Keypad 7" },
        { 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R3,                              "Keypad 8" },
        { 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_START,                           "Keypad *" },
        { 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_SELECT,                          "Keypad #" },
        { 1, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_Y,  "Keypad 9" },
        { 1, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_X,  "Keypad 0" },
        { 1, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_Y, "Purple" },
        { 1, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_X, "Blue" },

        { 0, 0, 0, 0, NULL }
    };

    struct retro_input_descriptor desc[] = {
        { 0, 0, 0, 0, NULL }
    };

    if (device == RETRO_DEVICE_JOYPAD)
        environ_cb(RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS, joypad);
    else
        environ_cb(RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS, desc);
}

void retro_get_system_info(struct retro_system_info *info)
{
    memset(info, 0, sizeof(*info));
    info->library_name     = "Gearcoleco";
    info->library_version  = GEARCOLECO_VERSION;
    info->need_fullpath    = false;
    info->valid_extensions = "col|cv|bin|rom";
}

static retro_video_refresh_t video_cb;
static retro_audio_sample_t audio_cb;
static retro_audio_sample_batch_t audio_batch_cb;
static retro_input_poll_t input_poll_cb;
static retro_input_state_t input_state_cb;

void retro_get_system_av_info(struct retro_system_av_info *info)
{
    GC_RuntimeInfo runtime_info;
    core->GetRuntimeInfo(runtime_info);

    current_screen_width = runtime_info.screen_width;
    current_screen_height = runtime_info.screen_height;

    info->geometry.base_width   = runtime_info.screen_width;
    info->geometry.base_height  = runtime_info.screen_height;
    info->geometry.max_width    = runtime_info.screen_width;
    info->geometry.max_height   = runtime_info.screen_height;
    info->geometry.aspect_ratio = 0.0f;
    info->timing.fps            = runtime_info.region == Region_NTSC ? 60.0 : 50.0;
    info->timing.sample_rate    = 44100.0;
}

void retro_set_environment(retro_environment_t cb)
{
    environ_cb = cb;

    if (cb(RETRO_ENVIRONMENT_GET_LOG_INTERFACE, &logging))
        log_cb = logging.log;
    else
        log_cb = fallback_log;

    static const struct retro_controller_description port_1[] = {
        { "ColecoVision", RETRO_DEVICE_SUBCLASS(RETRO_DEVICE_JOYPAD, 0) },
    };

    static const struct retro_controller_description port_2[] = {
        { "ColecoVision", RETRO_DEVICE_SUBCLASS(RETRO_DEVICE_JOYPAD, 0) },
    };

    static const struct retro_controller_info ports[] = {
        { port_1, 1 },
        { port_2, 1 },
        { NULL, 0 },
    };

    cb(RETRO_ENVIRONMENT_SET_CONTROLLER_INFO, (void*)ports);

    environ_cb(RETRO_ENVIRONMENT_SET_VARIABLES, (void *)vars);
}

void retro_set_audio_sample(retro_audio_sample_t cb)
{
    audio_cb = cb;
}

void retro_set_audio_sample_batch(retro_audio_sample_batch_t cb)
{
    audio_batch_cb = cb;
}

void retro_set_input_poll(retro_input_poll_t cb)
{
    input_poll_cb = cb;
}

void retro_set_input_state(retro_input_state_t cb)
{
    input_state_cb = cb;
}

void retro_set_video_refresh(retro_video_refresh_t cb)
{
    video_cb = cb;
}

static void load_bootroms(void)
{
    char bios_path[4113];

    sprintf(bios_path, "%s%ccolecovision.rom", retro_system_directory, slash);

    core->GetMemory()->LoadBios(bios_path);
}

static void update_input(void)
{
    int16_t joypad_bits[MAX_PADS];

    input_poll_cb();

    if (libretro_supports_bitmasks)
    {
        for (int j = 0; j < MAX_PADS; j++)
            joypad_bits[j] = input_state_cb(j, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_MASK);
    }
    else
    {
        for (int j = 0; j < MAX_PADS; j++)
        {
            joypad_bits[j] = 0;
            for (int i = 0; i < (RETRO_DEVICE_ID_JOYPAD_R3+1); i++)
                joypad_bits[j] |= input_state_cb(j, RETRO_DEVICE_JOYPAD, 0, i) ? (1 << i) : 0;
        }
    }

    // Copy previous state
    for (int j = 0; j < MAX_PADS; j++)
    {
        for (int i = 0; i < JOYPAD_BUTTONS; i++)
            joypre[j][i] = joypad[j][i];
        for (int i = 0; i < 4; i++)
            joypre_ext[j][i] = joypad_ext[j][i];
    }

    // Get current state
    for (int j = 0; j < MAX_PADS; j++)
    {
        if (allow_up_down)
        {
            joypad[j][0] = joypad_bits[j] & (1 << RETRO_DEVICE_ID_JOYPAD_UP)     ? 1 : 0;
            joypad[j][1] = joypad_bits[j] & (1 << RETRO_DEVICE_ID_JOYPAD_DOWN)   ? 1 : 0;
            joypad[j][2] = joypad_bits[j] & (1 << RETRO_DEVICE_ID_JOYPAD_LEFT)   ? 1 : 0;
            joypad[j][3] = joypad_bits[j] & (1 << RETRO_DEVICE_ID_JOYPAD_RIGHT)  ? 1 : 0;
        }
        else
        {
            joypad[j][0] = (joypad_bits[j] & (1 << RETRO_DEVICE_ID_JOYPAD_UP)) && ((joypre[j][0] == 1) || !(joypad_bits[j] & (1 << RETRO_DEVICE_ID_JOYPAD_DOWN)))    ? 1 : 0;
            joypad[j][1] = (joypad_bits[j] & (1 << RETRO_DEVICE_ID_JOYPAD_DOWN)) && ((joypre[j][1] == 1) || !(joypad_bits[j] & (1 << RETRO_DEVICE_ID_JOYPAD_UP)))    ? 1 : 0;
            joypad[j][2] = (joypad_bits[j] & (1 << RETRO_DEVICE_ID_JOYPAD_LEFT)) && ((joypre[j][2] == 1) || !(joypad_bits[j] & (1 << RETRO_DEVICE_ID_JOYPAD_RIGHT))) ? 1 : 0;
            joypad[j][3] = (joypad_bits[j] & (1 << RETRO_DEVICE_ID_JOYPAD_RIGHT)) && ((joypre[j][3] == 1) || !(joypad_bits[j] & (1 << RETRO_DEVICE_ID_JOYPAD_LEFT))) ? 1 : 0;
        }
        joypad[j][4] = joypad_bits[j] & (1 << RETRO_DEVICE_ID_JOYPAD_A)      ? 1 : 0;
        joypad[j][5] = joypad_bits[j] & (1 << RETRO_DEVICE_ID_JOYPAD_B)      ? 1 : 0;
        joypad[j][6] = joypad_bits[j] & (1 << RETRO_DEVICE_ID_JOYPAD_X)      ? 1 : 0;
        joypad[j][7] = joypad_bits[j] & (1 << RETRO_DEVICE_ID_JOYPAD_Y)      ? 1 : 0;
        joypad[j][8] = joypad_bits[j] & (1 << RETRO_DEVICE_ID_JOYPAD_START)  ? 1 : 0;
        joypad[j][9] = joypad_bits[j] & (1 << RETRO_DEVICE_ID_JOYPAD_SELECT) ? 1 : 0;
        joypad[j][10] = joypad_bits[j] & (1 << RETRO_DEVICE_ID_JOYPAD_L)     ? 1 : 0;
        joypad[j][11] = joypad_bits[j] & (1 << RETRO_DEVICE_ID_JOYPAD_R)     ? 1 : 0;
        joypad[j][12] = joypad_bits[j] & (1 << RETRO_DEVICE_ID_JOYPAD_L2)    ? 1 : 0;
        joypad[j][13] = joypad_bits[j] & (1 << RETRO_DEVICE_ID_JOYPAD_R2)    ? 1 : 0;
        joypad[j][14] = joypad_bits[j] & (1 << RETRO_DEVICE_ID_JOYPAD_L3)    ? 1 : 0;
        joypad[j][15] = joypad_bits[j] & (1 << RETRO_DEVICE_ID_JOYPAD_R3)    ? 1 : 0;

        int analog_left_x = input_state_cb( j, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_X);
        int analog_left_y = input_state_cb( j, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_Y);
        int analog_right_x = input_state_cb( j, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_X);
        int analog_right_y = input_state_cb( j, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_Y);

        joypad_ext[j][0] = analog_left_x != 0 ? 1 : 0;
        joypad_ext[j][1] = analog_left_y != 0 ? 1 : 0;
        joypad_ext[j][2] = analog_right_x != 0 ? 1 : 0;
        joypad_ext[j][3] = analog_right_y != 0 ? 1 : 0;
    }

    for (int j = 0; j < MAX_PADS; j++)
    {
        for (int i = 0; i < JOYPAD_BUTTONS; i++)
        {
            if (joypad[j][i] != joypre[j][i])
            {
                if (joypad[j][i] == 1)
                    core->KeyPressed(static_cast<GC_Controllers>(j), keymap[i]);
                else
                    core->KeyReleased(static_cast<GC_Controllers>(j), keymap[i]);
            }
        }

        for (int i = 0; i < 4; i++)
        {
            if (joypad_ext[j][i] != joypre_ext[j][i])
            {
                if (joypad_ext[j][i] == 1)
                    core->KeyPressed(static_cast<GC_Controllers>(j), keymap[i + JOYPAD_BUTTONS]);
                else
                    core->KeyReleased(static_cast<GC_Controllers>(j), keymap[i + JOYPAD_BUTTONS]);
            }
        }
    }
}

static void check_variables(void)
{
    struct retro_variable var = {0};

    var.key = "gearcoleco_up_down_allowed";
    var.value = NULL;

    if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
    {
        if (strcmp(var.value, "Enabled") == 0)
            allow_up_down = true;
        else
            allow_up_down = false;
    }

    var.key = "gearcoleco_timing";
    var.value = NULL;

    if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
    {
        if (strcmp(var.value, "Auto") == 0)
            config.region = Cartridge::CartridgeUnknownRegion;
        else if (strcmp(var.value, "NTSC (60 Hz)") == 0)
            config.region = Cartridge::CartridgeNTSC;
        else if (strcmp(var.value, "PAL (50 Hz)") == 0)
            config.region = Cartridge::CartridgePAL;
        else
            config.region = Cartridge::CartridgeUnknownRegion;
    }

    var.key = "gearcoleco_no_sprite_limit";
    var.value = NULL;

    if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
    {
        if (strcmp(var.value, "Enabled") == 0)
            core->GetVideo()->SetNoSpriteLimit(true);
        else
            core->GetVideo()->SetNoSpriteLimit(false);
    }
}

void retro_run(void)
{
    bool updated = false;
    if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE, &updated) && updated)
    {
        check_variables();
    }

    update_input();

    core->RunToVBlank(frame_buffer, audio_buf, &audio_sample_count);

    GC_RuntimeInfo runtime_info;
    core->GetRuntimeInfo(runtime_info);

    if ((runtime_info.screen_width != current_screen_width) || (runtime_info.screen_height != current_screen_height))
    {
        current_screen_width = runtime_info.screen_width;
        current_screen_height = runtime_info.screen_height;

        retro_system_av_info info;
        info.geometry.base_width   = runtime_info.screen_width;
        info.geometry.base_height  = runtime_info.screen_height;
        info.geometry.max_width    = runtime_info.screen_width;
        info.geometry.max_height   = runtime_info.screen_height;
        info.geometry.aspect_ratio = 0.0;

        environ_cb(RETRO_ENVIRONMENT_SET_GEOMETRY, &info.geometry);
    }

    video_cb((uint8_t*)frame_buffer, runtime_info.screen_width, runtime_info.screen_height, runtime_info.screen_width * sizeof(u8) * 2);

    if (audio_sample_count > 0)
        audio_batch_cb(audio_buf, audio_sample_count / 2);

    audio_sample_count = 0;
}

void retro_reset(void)
{
    check_variables();
    load_bootroms();
    core->ResetROMPreservingRAM(&config);
}

bool retro_load_game(const struct retro_game_info *info)
{
    check_variables();
    load_bootroms();

    if (!core->LoadROMFromBuffer(reinterpret_cast<const u8*>(info->data), info->size, &config))
    {
        log_cb(RETRO_LOG_ERROR, "Invalid or corrupted ROM.\n");
        return false;
    }

    enum retro_pixel_format fmt = RETRO_PIXEL_FORMAT_RGB565;
    if (!environ_cb(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &fmt))
    {
        log_cb(RETRO_LOG_INFO, "RGB565 is not supported.\n");
        return false;
    }

    snprintf(retro_game_path, sizeof(retro_game_path), "%s", info->path);

    struct retro_memory_descriptor descs[4];

    memset(descs, 0, sizeof(descs));

    // BIOS
    descs[0].ptr   = core->GetMemory()->GetBios();
    descs[0].start = 0x0000;
    descs[0].len   = 0x2000;
    // EXPANSION
    descs[1].ptr   = NULL;
    descs[1].start = 0x2000;
    descs[1].len   = 0x4000;
    // RAM
    descs[2].ptr   = core->GetMemory()->GetRam();
    descs[2].start = 0x6000;
    descs[2].len   = 0x0400;
    // CART
    descs[3].ptr   = core->GetCartridge()->GetROM();
    descs[3].start = 0x8000;
    descs[3].len   = 0x8000;

    struct retro_memory_map mmaps;
    mmaps.descriptors = descs;
    mmaps.num_descriptors = sizeof(descs) / sizeof(descs[0]);
    environ_cb(RETRO_ENVIRONMENT_SET_MEMORY_MAPS, &mmaps);

    bool achievements = true;
    environ_cb(RETRO_ENVIRONMENT_SET_SUPPORT_ACHIEVEMENTS, &achievements);

    return true;
}

void retro_unload_game(void)
{
}

unsigned retro_get_region(void)
{
    return core->GetCartridge()->IsPAL() ? RETRO_REGION_PAL : RETRO_REGION_NTSC;
}

bool retro_load_game_special(unsigned type, const struct retro_game_info *info, size_t num)
{
    return false;
}

size_t retro_serialize_size(void)
{
    size_t size = 0;
    core->SaveState(NULL, size);
    return size;
}

bool retro_serialize(void *data, size_t size)
{
    return core->SaveState(reinterpret_cast<u8*>(data), size);
}

bool retro_unserialize(const void *data, size_t size)
{
    return core->LoadState(reinterpret_cast<const u8*>(data), size);
}

void *retro_get_memory_data(unsigned id)
{
    switch (id)
    {
        case RETRO_MEMORY_SAVE_RAM:
            return NULL;
        case RETRO_MEMORY_SYSTEM_RAM:
            return core->GetMemory()->GetRam();
    }

    return NULL;
}

size_t retro_get_memory_size(unsigned id)
{
    switch (id)
    {
        case RETRO_MEMORY_SAVE_RAM:
            return 0;
        case RETRO_MEMORY_SYSTEM_RAM:
            return 0x400;
    }

    return 0;
}

void retro_cheat_reset(void)
{
    // TODO
}

void retro_cheat_set(unsigned index, bool enabled, const char *code)
{
    // TODO
}
