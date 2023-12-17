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

#include "Video.h"
#include "Memory.h"
#include "Processor.h"

Video::Video(Memory* pMemory, Processor* pProcessor)
{
    m_pMemory = pMemory;
    m_pProcessor = pProcessor;
    InitPointer(m_pInfoBuffer);
    InitPointer(m_pFrameBuffer);
    InitPointer(m_pVdpVRAM);
    m_bFirstByteInSequence = true;
    for (int i = 0; i < 8; i++)
        m_VdpRegister[i] = 0;
    m_VdpBuffer = 0;
    m_VdpAddress = 0;
    m_iCycleCounter = 0;
    m_VdpStatus = 0;
    m_iLinesPerFrame = 0;
    m_bPAL = false;
    m_LineEvents.vint = false;
    m_LineEvents.render = false;
    m_LineEvents.display = false;
    m_iRenderLine = 0;
    m_iMode = 0;
    m_bDisplayEnabled = false;
    m_bSpriteOvrRequest = false;
    m_bNoSpriteLimit = false;

    for (int i = 0; i < 48; i++)
        m_CustomPalette[i] = 0;
    m_pCurrentPalette = const_cast<u8*>(kPalette_888_coleco);
}

Video::~Video()
{
    SafeDeleteArray(m_pInfoBuffer);
    SafeDeleteArray(m_pFrameBuffer);
    SafeDeleteArray(m_pVdpVRAM);
}

void Video::Init()
{
    m_pInfoBuffer = new u8[GC_RESOLUTION_MAX_WIDTH * GC_LINES_PER_FRAME_PAL];
    m_pFrameBuffer = new u16[GC_RESOLUTION_MAX_WIDTH * GC_LINES_PER_FRAME_PAL];
    m_pVdpVRAM = new u8[0x4000];
    InitPalettes();
    Reset(false);
}

void Video::Reset(bool bPAL)
{
    m_bPAL = bPAL;
    m_iLinesPerFrame = bPAL ? GC_LINES_PER_FRAME_PAL : GC_LINES_PER_FRAME_NTSC;
    m_bFirstByteInSequence = true;
    m_VdpBuffer = 0;
    m_VdpAddress = 0;
    m_VdpStatus = 0;
    for (int i = 0; i < (GC_RESOLUTION_MAX_WIDTH * GC_LINES_PER_FRAME_PAL); i++)
    {
        m_pFrameBuffer[i] = 1;
        m_pInfoBuffer[i] = 0;
    }
    for (int i = 0; i < 0x4000; i++)
        m_pVdpVRAM[i] = 0;
    for (int i = 0; i < 8; i++)
        m_VdpRegister[i] = 0;

    m_bDisplayEnabled = false;
    m_bSpriteOvrRequest = false;

    m_LineEvents.vint = false;
    m_LineEvents.display = false;
    m_LineEvents.render = false;

    m_iCycleCounter = 0;
    m_iRenderLine = 0;

    m_Timing[TIMING_VINT] = 220;
    m_Timing[TIMING_RENDER] = 195;
    m_Timing[TIMING_DISPLAY] = 37;
}

void Video::SetNoSpriteLimit(bool noSpriteLimit)
{
    m_bNoSpriteLimit = noSpriteLimit;
}

bool Video::Tick(unsigned int clockCycles)
{
    bool return_vblank = false;

    m_iCycleCounter += clockCycles;

    ///// VINT /////
    if (m_iRenderLine == GC_RESOLUTION_MAX_HEIGHT)
    {
        if (!m_LineEvents.vint && (m_iCycleCounter >= m_Timing[TIMING_VINT]))
        {
            m_LineEvents.vint = true;

            if (IsSetBit(m_VdpRegister[1], 5) && !IsSetBit(m_VdpStatus, 7))
                m_pProcessor->RequestNMI();

            m_VdpStatus = SetBit(m_VdpStatus, 7);
        }
    }

    ///// DISPLAY ON/OFF /////
    if (!m_LineEvents.display && (m_iCycleCounter >= m_Timing[TIMING_DISPLAY]))
    {
        m_LineEvents.display = true;
        m_bDisplayEnabled = IsSetBit(m_VdpRegister[1], 6);
    }

    ///// RENDER /////
    if (!m_LineEvents.render && (m_iCycleCounter >= m_Timing[TIMING_RENDER]))
    {
        m_LineEvents.render = true;
        ScanLine(m_iRenderLine);
    }

    ///// END OF LINE /////
    if (m_iCycleCounter >= GC_CYCLES_PER_LINE)
    {
        if (m_iRenderLine == GC_RESOLUTION_MAX_HEIGHT)
        {
            return_vblank = true;
        }
        m_iRenderLine++;
        m_iRenderLine %= m_iLinesPerFrame;
        m_iCycleCounter -= GC_CYCLES_PER_LINE;
        m_LineEvents.vint = false;
        m_LineEvents.render = false;
        m_LineEvents.display = false;
    }

    return return_vblank;
}

u8 Video::GetDataPort()
{
    m_bFirstByteInSequence = true;
    u8 ret = m_VdpBuffer;
    m_VdpBuffer = m_pVdpVRAM[m_VdpAddress];
    m_VdpAddress = (m_VdpAddress + 1) & 0x3FFF;
    return ret;
}

u8 Video::GetStatusFlags()
{
    m_bFirstByteInSequence = true;
    u8 ret = m_VdpStatus;
    m_VdpStatus &= 0x1F;

    if (IsSetBit(m_VdpRegister[1], 5) && IsSetBit(m_VdpStatus, 7))
    {
        m_pProcessor->RequestNMI();
    }

    return ret;
}

void Video::WriteData(u8 data)
{
    m_bFirstByteInSequence = true;
    m_VdpBuffer = data;
    m_pVdpVRAM[m_VdpAddress] = data;
    m_VdpAddress = (m_VdpAddress + 1) & 0x3FFF;
}

void Video::WriteControl(u8 control)
{
    if (m_bFirstByteInSequence)
    {
        m_bFirstByteInSequence = false;
        m_VdpAddress = (m_VdpAddress & 0x3F00) | control;
        m_VdpBuffer = control;
    }
    else
    {
        m_bFirstByteInSequence = true;
        m_VdpAddress = ((control & 0x3F) << 8) | m_VdpBuffer;

        switch (control & 0xC0)
        {
            case 0x00:
            {
                m_VdpBuffer = m_pVdpVRAM[m_VdpAddress];
                m_VdpAddress = (m_VdpAddress + 1) & 0x3FFF;
                break;
            }
            case 0x80:
            {
                bool old_nmi = IsSetBit(m_VdpRegister[1], 5);
                u8 masks[8] = { 0x03, 0xFB, 0x0F, 0xFF, 0x07, 0x7F, 0x07, 0xFF };
                u8 reg = control & 0x07;
                m_VdpRegister[reg] = m_VdpBuffer & masks[reg];

                if ((reg == 1) && IsSetBit(m_VdpRegister[1], 5) && (!old_nmi) && IsSetBit(m_VdpStatus, 7))
                {
                    m_pProcessor->RequestNMI();
                }

                if (reg < 2)
                {
                    m_iMode = ((m_VdpRegister[1] & 0x08) >> 1) | (m_VdpRegister[0] & 0x02) | ((m_VdpRegister[1] & 0x10) >> 4);
                }

                break;
            }
        }
    }
}

bool Video::IsPAL()
{
    return m_bPAL;
}

u8 Video::GetBufferReg()
{
    return m_VdpBuffer;
}

u16 Video::GetAddressReg()
{
    return m_VdpAddress;
}

u8 Video::GetStatusReg()
{
    return m_VdpStatus;
}

int Video::GetRenderLine()
{
    return m_iRenderLine;
}

int Video::GetCycleCounter()
{
    return m_iCycleCounter;
}

bool Video::GetLatch()
{
    return m_bFirstByteInSequence;
}

void Video::ScanLine(int line)
{
    if (m_bDisplayEnabled)
    {
        if (line < GC_RESOLUTION_MAX_HEIGHT)
        {
            RenderBackground(line);

            if (m_iMode != 0x01)
                RenderSprites(line);
        }
    }
    else
    {
        if (line < GC_RESOLUTION_MAX_HEIGHT)
        {
            int line_width = line * GC_RESOLUTION_MAX_WIDTH;

            for (int scx = 0; scx < GC_RESOLUTION_MAX_WIDTH; scx++)
            {
                int pixel = line_width + scx;
                m_pFrameBuffer[pixel] = 1;
                m_pInfoBuffer[pixel] = 0;
            }
        }
    }
}

void Video::RenderBackground(int line)
{
    int line_offset = line * GC_RESOLUTION_MAX_WIDTH;

    int name_table_addr = m_VdpRegister[2] << 10;
    int color_table_addr = m_VdpRegister[3] << 6;
    int pattern_table_addr = m_VdpRegister[4] << 11;
    int region_mask = ((m_VdpRegister[4] & 0x03) << 8) | 0xFF;
    int color_mask = ((m_VdpRegister[3] & 0x7F) << 3) | 0x07;
    int backdrop_color = m_VdpRegister[7] & 0x0F;
    backdrop_color = (backdrop_color > 0) ? backdrop_color : 1;

    int tile_y = line >> 3;
    int tile_y_offset = line & 7;
    int region = 0;

    switch (m_iMode)
    {
        case 1:
        {
            int fg_color = (m_VdpRegister[7] >> 4) & 0x0F;
            int bg_color = backdrop_color;
            fg_color = (fg_color > 0) ? fg_color : backdrop_color;

            for (int i = 0; i < 8; i++)
            {
                int pixel = line_offset + i;
                m_pFrameBuffer[pixel] = bg_color;
                m_pFrameBuffer[pixel + 248] = bg_color;
                m_pInfoBuffer[pixel] = 0x00;
                m_pInfoBuffer[pixel + 248] = 0x00;
            }

            for (int tile_x = 0; tile_x < 40; tile_x++)
            {
                int tile_number = (tile_y * 40) + tile_x;
                int name_tile_addr = name_table_addr + tile_number;
                int name_tile = m_pVdpVRAM[name_tile_addr];
                u8 pattern_line = m_pVdpVRAM[pattern_table_addr + (name_tile << 3) + tile_y_offset];

                int screen_offset = line_offset + (tile_x * 6) + 8;

                for (int tile_pixel = 0; tile_pixel < 6; tile_pixel++)
                {
                    int pixel = screen_offset + tile_pixel;
                    m_pFrameBuffer[pixel] = IsSetBit(pattern_line, 7 - tile_pixel) ? fg_color : bg_color;
                    m_pInfoBuffer[pixel] = 0x00;
                }
            }
            return;
        }
        case 2:
        {
            pattern_table_addr &= 0x2000;
            color_table_addr &= 0x2000;
            region = (tile_y & 0x18) << 5;
            break;
        }
        case 4:
        {
            pattern_table_addr &= 0x2000;
            break;
        }
    }

    for (int tile_x = 0; tile_x < 32; tile_x++)
    {
        int tile_number = (tile_y << 5) + tile_x;
        int name_tile_addr = name_table_addr + tile_number;
        int name_tile = m_pVdpVRAM[name_tile_addr];
        u8 pattern_line = 0;
        u8 color_line = 0;

        if (m_iMode == 4)
        {
            int offset_color = pattern_table_addr + (name_tile << 3) + ((tile_y & 0x03) << 1) + (line & 0x04 ? 1 : 0);
            color_line = m_pVdpVRAM[offset_color];

            int left_color = color_line >> 4;
            int right_color = color_line & 0x0F;
            left_color = (left_color > 0) ? left_color : backdrop_color;
            right_color = (right_color > 0) ? right_color : backdrop_color;

            int screen_offset = line_offset + (tile_x << 3);

            for (int tile_pixel = 0; tile_pixel < 4; tile_pixel++)
            {
                int pixel = screen_offset + tile_pixel;
                m_pFrameBuffer[pixel] = left_color;
                m_pInfoBuffer[pixel] = 0x00;
            }

            for (int tile_pixel = 4; tile_pixel < 8; tile_pixel++)
            {
                int pixel = screen_offset + tile_pixel;
                m_pFrameBuffer[pixel] = right_color;
                m_pInfoBuffer[pixel] = 0x00;
            }

            continue;
        }
        else if (m_iMode == 0)
        {
            pattern_line = m_pVdpVRAM[pattern_table_addr + (name_tile << 3) + tile_y_offset];
            color_line = m_pVdpVRAM[color_table_addr + (name_tile >> 3)];
        }
        else if (m_iMode == 2)
        {
            name_tile += region;
            pattern_line = m_pVdpVRAM[pattern_table_addr + ((name_tile & region_mask) << 3) + tile_y_offset];
            color_line = m_pVdpVRAM[color_table_addr + ((name_tile & color_mask) << 3) + tile_y_offset];
        }

        int fg_color = color_line >> 4;
        int bg_color = color_line & 0x0F;
        fg_color = (fg_color > 0) ? fg_color : backdrop_color;
        bg_color = (bg_color > 0) ? bg_color : backdrop_color;

        int screen_offset = line_offset + (tile_x << 3);

        for (int tile_pixel = 0; tile_pixel < 8; tile_pixel++)
        {
            int pixel = screen_offset + tile_pixel;
            m_pFrameBuffer[pixel] = IsSetBit(pattern_line, 7 - tile_pixel) ? fg_color : bg_color;
            m_pInfoBuffer[pixel] = 0x00;
        }
    }
}

void Video::RenderSprites(int line)
{
    int sprite_count = 0;
    int line_width = line * GC_RESOLUTION_MAX_WIDTH;
    int sprite_size = IsSetBit(m_VdpRegister[1], 1) ? 16 : 8;
    bool sprite_zoom = IsSetBit(m_VdpRegister[1], 0);
    if (sprite_zoom)
        sprite_size *= 2;
    u16 sprite_attribute_addr = (m_VdpRegister[5] & 0x7F) << 7;
    u16 sprite_pattern_addr = (m_VdpRegister[6] & 0x07) << 11;

    int max_sprite = 31;

    for (int sprite = 0; sprite <= max_sprite; sprite++)
    {
        if (m_pVdpVRAM[sprite_attribute_addr + (sprite << 2)] == 0xD0)
        {
            max_sprite = sprite - 1;
            break;
        }
    }

    for (int sprite = 0; sprite <= max_sprite; sprite++)
    {
        int sprite_attribute_offset = sprite_attribute_addr + (sprite << 2);
        int sprite_y = (m_pVdpVRAM[sprite_attribute_offset] + 1) & 0xFF;

        if (sprite_y >= 0xE0)
            sprite_y = -(0x100 - sprite_y);

        if ((sprite_y > line) || ((sprite_y + sprite_size) <= line))
            continue;

        sprite_count++;
        if (!IsSetBit(m_VdpStatus, 6) && (sprite_count > 4))
        {
            m_VdpStatus = SetBit(m_VdpStatus, 6);
            m_VdpStatus = (m_VdpStatus & 0xE0) | sprite;
        }

        int sprite_color = m_pVdpVRAM[sprite_attribute_offset + 3] & 0x0F;

        if (sprite_color == 0)
            continue;

        int sprite_shift = (m_pVdpVRAM[sprite_attribute_offset + 3] & 0x80) ? 32 : 0;
        int sprite_x = m_pVdpVRAM[sprite_attribute_offset + 1] - sprite_shift;

        if (sprite_x >= GC_RESOLUTION_MAX_WIDTH)
            continue;

        int sprite_tile = m_pVdpVRAM[sprite_attribute_offset + 2];
        sprite_tile &= IsSetBit(m_VdpRegister[1], 1) ? 0xFC : 0xFF;

        int sprite_line_addr = sprite_pattern_addr + (sprite_tile << 3) + ((line - sprite_y ) >> (sprite_zoom ? 1 : 0));

        for (int tile_x = 0; tile_x < sprite_size; tile_x++)
        {
            int sprite_pixel_x = sprite_x + tile_x;
            if (sprite_pixel_x >= GC_RESOLUTION_MAX_WIDTH)
                break;
            if (sprite_pixel_x < 0)
                continue;

            int pixel = line_width + sprite_pixel_x;

            bool sprite_pixel = false;

            int tile_x_adjusted = tile_x >> (sprite_zoom ? 1 : 0);

            if (tile_x_adjusted < 8)
                sprite_pixel = IsSetBit(m_pVdpVRAM[sprite_line_addr], 7 - tile_x_adjusted);
            else
                sprite_pixel = IsSetBit(m_pVdpVRAM[sprite_line_addr + 16], 15 - tile_x_adjusted);

            if (sprite_pixel && ((sprite_count < 5) || m_bNoSpriteLimit))
            {
                if (!IsSetBit(m_pInfoBuffer[pixel], 0) && (sprite_color > 0))
                {
                    m_pFrameBuffer[pixel] = sprite_color;
                    m_pInfoBuffer[pixel] = SetBit(m_pInfoBuffer[pixel], 0);
                }

                if (IsSetBit(m_pInfoBuffer[pixel], 1))
                {
                     m_VdpStatus = SetBit(m_VdpStatus, 5);
                }
                else
                {
                    m_pInfoBuffer[pixel] = SetBit(m_pInfoBuffer[pixel], 1);
                }
            }
        }
    }
}

void Video::Render24bit(u16* srcFrameBuffer, u8* dstFrameBuffer, GC_Color_Format pixelFormat, int size)
{
    bool bgr = (pixelFormat == GC_PIXEL_BGR888);

    for (int i = 0, j = 0; i < size; i ++, j += 3)
    {
        u16 src_color = srcFrameBuffer[i] * 3;

        if (bgr)
        {
            dstFrameBuffer[j + 2] = m_pCurrentPalette[src_color];
            dstFrameBuffer[j] = m_pCurrentPalette[src_color + 2];
        }
        else
        {
            dstFrameBuffer[j] = m_pCurrentPalette[src_color];
            dstFrameBuffer[j + 2] = m_pCurrentPalette[src_color + 2];
        }
        dstFrameBuffer[j + 1] = m_pCurrentPalette[src_color + 1];
    }
}

void Video::Render16bit(u16* srcFrameBuffer, u8* dstFrameBuffer, GC_Color_Format pixelFormat, int size)
{
    bool green_6bit = (pixelFormat == GC_PIXEL_RGB565) || (pixelFormat == GC_PIXEL_BGR565);
    bool bgr = ((pixelFormat == GC_PIXEL_BGR555) || (pixelFormat == GC_PIXEL_BGR565));

    const u16* pal;

    if (bgr)
        pal = green_6bit ? m_palette_565_bgr : m_palette_555_bgr;
    else
        pal = green_6bit ? m_palette_565_rgb : m_palette_555_rgb;

    for (int i = 0, j = 0; i < size; i ++, j += 2)
    {
        u16 src_color = srcFrameBuffer[i];

        *(u16*)(&dstFrameBuffer[j]) = pal[src_color];
    }
}

void Video::SetCustomPalette(GC_Color* palette)
{
    for (int i = 0; i < 16; i++)
    {
        int p = i * 3;
        m_CustomPalette[p] = palette[i].red;
        m_CustomPalette[p + 1] = palette[i].green;
        m_CustomPalette[p + 2] = palette[i].blue;
    }

    m_pCurrentPalette = m_CustomPalette;
    InitPalettes();
}

void Video::SetPredefinedPalette(int palette)
{
    const u8* predefined;

    switch (palette)
    {
        case 0:
            predefined = kPalette_888_coleco;
            break;
        case 1:
            predefined = kPalette_888_tms9918;
            break;
        default:
            predefined = NULL;
    }

    if (IsValidPointer(predefined))
    {
        m_pCurrentPalette = const_cast<u8*>(predefined);
        InitPalettes();
    }
}

void Video::InitPalettes()
{
    for (int i=0,j=0; i<16; i++,j+=3)
    {
        u8 red = m_pCurrentPalette[j];
        u8 green = m_pCurrentPalette[j+1];
        u8 blue = m_pCurrentPalette[j+2];

        u8 red_5 = red * 31 / 255;
        u8 green_5 = green * 31 / 255;
        u8 green_6 = green * 63 / 255;
        u8 blue_5 = blue * 31 / 255;

        m_palette_565_rgb[i] = red_5 << 11 | green_6 << 5 | blue_5;
        m_palette_555_rgb[i] = red_5 << 10 | green_5 << 5 | blue_5;
        m_palette_565_bgr[i] = blue_5 << 11 | green_6 << 5 | red_5;
        m_palette_555_bgr[i] = blue_5 << 10 | green_5 << 5 | red_5;
    }
}

void Video::SaveState(std::ostream& stream)
{
    stream.write(reinterpret_cast<const char*> (m_pInfoBuffer), GC_RESOLUTION_MAX_WIDTH * GC_LINES_PER_FRAME_PAL);
    stream.write(reinterpret_cast<const char*> (m_pVdpVRAM), 0x4000);
    stream.write(reinterpret_cast<const char*> (&m_bFirstByteInSequence), sizeof(m_bFirstByteInSequence));
    stream.write(reinterpret_cast<const char*> (m_VdpRegister), sizeof(m_VdpRegister));
    stream.write(reinterpret_cast<const char*> (&m_VdpBuffer), sizeof(m_VdpBuffer));
    stream.write(reinterpret_cast<const char*> (&m_VdpAddress), sizeof(m_VdpAddress));
    stream.write(reinterpret_cast<const char*> (&m_iCycleCounter), sizeof(m_iCycleCounter));
    stream.write(reinterpret_cast<const char*> (&m_VdpStatus), sizeof(m_VdpStatus));
    stream.write(reinterpret_cast<const char*> (&m_iLinesPerFrame), sizeof(m_iLinesPerFrame));
    stream.write(reinterpret_cast<const char*> (&m_LineEvents), sizeof(m_LineEvents));
    stream.write(reinterpret_cast<const char*> (&m_iRenderLine), sizeof(m_iRenderLine));
    stream.write(reinterpret_cast<const char*> (&m_bPAL), sizeof(m_bPAL));
    stream.write(reinterpret_cast<const char*> (&m_iMode), sizeof(m_iMode));
    stream.write(reinterpret_cast<const char*> (&m_Timing), sizeof(m_Timing));
    stream.write(reinterpret_cast<const char*> (&m_bDisplayEnabled), sizeof(m_bDisplayEnabled));
    stream.write(reinterpret_cast<const char*> (&m_bSpriteOvrRequest), sizeof(m_bSpriteOvrRequest));
}

void Video::LoadState(std::istream& stream)
{
    stream.read(reinterpret_cast<char*> (m_pInfoBuffer), GC_RESOLUTION_MAX_WIDTH * GC_LINES_PER_FRAME_PAL);
    stream.read(reinterpret_cast<char*> (m_pVdpVRAM), 0x4000);
    stream.read(reinterpret_cast<char*> (&m_bFirstByteInSequence), sizeof(m_bFirstByteInSequence));
    stream.read(reinterpret_cast<char*> (m_VdpRegister), sizeof(m_VdpRegister));
    stream.read(reinterpret_cast<char*> (&m_VdpBuffer), sizeof(m_VdpBuffer));
    stream.read(reinterpret_cast<char*> (&m_VdpAddress), sizeof(m_VdpAddress));
    stream.read(reinterpret_cast<char*> (&m_iCycleCounter), sizeof(m_iCycleCounter));
    stream.read(reinterpret_cast<char*> (&m_VdpStatus), sizeof(m_VdpStatus));
    stream.read(reinterpret_cast<char*> (&m_iLinesPerFrame), sizeof(m_iLinesPerFrame));
    stream.read(reinterpret_cast<char*> (&m_LineEvents), sizeof(m_LineEvents));
    stream.read(reinterpret_cast<char*> (&m_iRenderLine), sizeof(m_iRenderLine));
    stream.read(reinterpret_cast<char*> (&m_bPAL), sizeof(m_bPAL));
    stream.read(reinterpret_cast<char*> (&m_iMode), sizeof(m_iMode));
    stream.read(reinterpret_cast<char*> (&m_Timing), sizeof(m_Timing));
    stream.read(reinterpret_cast<char*> (&m_bDisplayEnabled), sizeof(m_bDisplayEnabled));
    stream.read(reinterpret_cast<char*> (&m_bSpriteOvrRequest), sizeof(m_bSpriteOvrRequest));
}
