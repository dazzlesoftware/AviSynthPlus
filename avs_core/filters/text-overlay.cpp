// Avisynth v2.5.  Copyright 2002 Ben Rudiak-Gould et al.
// http://www.avisynth.org

// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA, or visit
// http://www.gnu.org/copyleft/gpl.html .
//
// Linking Avisynth statically or dynamically with other modules is making a
// combined work based on Avisynth.  Thus, the terms and conditions of the GNU
// General Public License cover the whole combination.
//
// As a special exception, the copyright holders of Avisynth give you
// permission to link Avisynth with independent modules that communicate with
// Avisynth solely through the interfaces defined in avisynth.h, regardless of the license
// terms of these independent modules, and to copy and distribute the
// resulting combined work under terms of your choice, provided that
// every copy of the combined work is accompanied by a complete copy of
// the source code of Avisynth (the version of Avisynth used to produce the
// combined work), being distributed under the terms of the GNU General
// Public License plus this exception.  An independent module is a module
// which is not derived from or based on Avisynth, such as 3rd-party filters,
// import and export plugins, or graphical user interfaces.


#include "text-overlay.h"
#include "../convert/convert.h"  // for RGB2YUV
#include <avs/win.h>
#include <sstream>
#include <avs/config.h>
#include <avs/minmax.h>
#include <emmintrin.h>



static HFONT LoadFont(const char name[], int size, bool bold, bool italic, int width=0, int angle=0) 
{
  return CreateFont( size, width, angle, angle, bold ? FW_BOLD : FW_NORMAL,
                     italic, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                     CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, FF_DONTCARE | DEFAULT_PITCH, name );
}

/********************************************************************
***** Declare index of new filters for Avisynth's filter engine *****
********************************************************************/

extern const AVSFunction Text_filters[] = {
  { "ShowFrameNumber",BUILTIN_FUNC_PREFIX, 
	"c[scroll]b[offset]i[x]f[y]f[font]s[size]f[text_color]i[halo_color]i[font_width]f[font_angle]f",
	ShowFrameNumber::Create },

  { "ShowSMPTE",BUILTIN_FUNC_PREFIX, 
	"c[fps]f[offset]s[offset_f]i[x]f[y]f[font]s[size]f[text_color]i[halo_color]i[font_width]f[font_angle]f",
	ShowSMPTE::CreateSMTPE },

  { "ShowTime",BUILTIN_FUNC_PREFIX, 
	"c[offset_f]i[x]f[y]f[font]s[size]f[text_color]i[halo_color]i[font_width]f[font_angle]f",
	ShowSMPTE::CreateTime },

  { "Info", BUILTIN_FUNC_PREFIX, "c", FilterInfo::Create },  // clip

  { "Subtitle",BUILTIN_FUNC_PREFIX, 
	"cs[x]f[y]f[first_frame]i[last_frame]i[font]s[size]f[text_color]i[halo_color]i"
	"[align]i[spc]i[lsp]i[font_width]f[font_angle]f[interlaced]b", 
    Subtitle::Create },       // see docs!

  { "Compare",BUILTIN_FUNC_PREFIX, 
	"cc[channels]s[logfile]s[show_graph]b",
	Compare::Create },

  { 0 }
};






/******************************
 *******   Anti-alias    ******
 *****************************/

Antialiaser::Antialiaser(int width, int height, const char fontname[], int size, int _textcolor, int _halocolor, int font_width, int font_angle, bool _interlaced) :
  w(width), h(height), textcolor(_textcolor), halocolor(_halocolor), alpha_calcs(0),
  dirty(true), interlaced(_interlaced)
{
  struct {
    BITMAPINFOHEADER bih;
    RGBQUAD clr[2];
  } b;

  b.bih.biSize                    = sizeof(BITMAPINFOHEADER);
  b.bih.biWidth                   = width * 8 + 32;
  b.bih.biHeight                  = height * 8 + 32;
  b.bih.biBitCount                = 1;
  b.bih.biPlanes                  = 1;
  b.bih.biCompression             = BI_RGB;
  b.bih.biXPelsPerMeter   = 0;
  b.bih.biYPelsPerMeter   = 0;
  b.bih.biClrUsed                 = 2;
  b.bih.biClrImportant    = 2;
  b.clr[0].rgbBlue = b.clr[0].rgbGreen = b.clr[0].rgbRed = 0;
  b.clr[1].rgbBlue = b.clr[1].rgbGreen = b.clr[1].rgbRed = 255;

  hdcAntialias = CreateCompatibleDC(NULL);
  if (hdcAntialias) {
	hbmAntialias = CreateDIBSection
	  ( hdcAntialias,
		(BITMAPINFO *)&b,
		DIB_RGB_COLORS,
		&lpAntialiasBits,
		NULL,
		0 );
	if (hbmAntialias) {
	  hbmDefault = (HBITMAP)SelectObject(hdcAntialias, hbmAntialias);

	  HFONT newfont = LoadFont(fontname, size, true, false, font_width, font_angle);
	  hfontDefault = newfont ? (HFONT)SelectObject(hdcAntialias, newfont) : 0;

	  SetMapMode(hdcAntialias, MM_TEXT);
	  SetTextColor(hdcAntialias, 0xffffff);
	  SetBkColor(hdcAntialias, 0);

	  alpha_calcs = new(std::nothrow) unsigned short[width*height*4];
	  if (!alpha_calcs) FreeDC();
	}
  }
}


Antialiaser::~Antialiaser() {
  FreeDC();
  delete[] alpha_calcs;
}


HDC Antialiaser::GetDC() {
  dirty = true;
  return hdcAntialias;
}


void Antialiaser::FreeDC() {
  if (hdcAntialias) { // :FIXME: Interlocked
    if (hbmDefault) {
	  DeleteObject(SelectObject(hdcAntialias, hbmDefault));
	  hbmDefault = 0;
	}
    if (hfontDefault) {
	  DeleteObject(SelectObject(hdcAntialias, hfontDefault));
	  hfontDefault = 0;
	}
    DeleteDC(hdcAntialias);
    hdcAntialias = 0;
  }
}


void Antialiaser::Apply( const VideoInfo& vi, PVideoFrame* frame, int pitch)
{
  if (!alpha_calcs) return;

  if (vi.IsRGB32())
    ApplyRGB32((*frame)->GetWritePtr(), pitch);
  else if (vi.IsRGB24())
    ApplyRGB24((*frame)->GetWritePtr(), pitch);
  else if (vi.IsYUY2())
    ApplyYUY2((*frame)->GetWritePtr(), pitch);
  else if (vi.IsYV12())
    ApplyYV12((*frame)->GetWritePtr(), pitch,
              (*frame)->GetPitch(PLANAR_U),
			  (*frame)->GetWritePtr(PLANAR_U),
			  (*frame)->GetWritePtr(PLANAR_V) );
  else if (vi.IsY8())
    ApplyPlanar((*frame)->GetWritePtr(), pitch, 0, 0, 0, 0, 0);
  else if (vi.IsPlanar())
    ApplyPlanar((*frame)->GetWritePtr(), pitch,
              (*frame)->GetPitch(PLANAR_U),
			  (*frame)->GetWritePtr(PLANAR_U),
			  (*frame)->GetWritePtr(PLANAR_V),
			  vi.GetPlaneWidthSubsampling(PLANAR_U),
			  vi.GetPlaneHeightSubsampling(PLANAR_U));

}


void Antialiaser::ApplyYV12(BYTE* buf, int pitch, int pitchUV, BYTE* bufU, BYTE* bufV) {
  if (dirty) {
    GetAlphaRect();
	xl &= -2; xr |= 1;
	yb &= -2; yt |= 1;
  }
  const int w4 = w*4;
  unsigned short* alpha = alpha_calcs + yb*w4;
  buf  += pitch*yb;
  bufU += (pitchUV*yb)>>1;
  bufV += (pitchUV*yb)>>1;

  for (int y=yb; y<=yt; y+=2) {
    for (int x=xl; x<=xr; x+=2) {
      const int x4 = x<<2;
      const int basealpha00 = alpha[x4+0];
      const int basealpha10 = alpha[x4+4];
      const int basealpha01 = alpha[x4+0+w4];
      const int basealpha11 = alpha[x4+4+w4];
      const int basealphaUV = basealpha00 + basealpha10 + basealpha01 + basealpha11;

      if (basealphaUV != 1024) {
        buf[x+0]       = (buf[x+0]       * basealpha00 + alpha[x4+3]   ) >> 8;
        buf[x+1]       = (buf[x+1]       * basealpha10 + alpha[x4+7]   ) >> 8;
        buf[x+0+pitch] = (buf[x+0+pitch] * basealpha01 + alpha[x4+3+w4]) >> 8;
        buf[x+1+pitch] = (buf[x+1+pitch] * basealpha11 + alpha[x4+7+w4]) >> 8;

        const int au  = alpha[x4+2] + alpha[x4+6] + alpha[x4+2+w4] + alpha[x4+6+w4];
        bufU[x>>1] = (bufU[x>>1] * basealphaUV + au) >> 10;

        const int av  = alpha[x4+1] + alpha[x4+5] + alpha[x4+1+w4] + alpha[x4+5+w4];
        bufV[x>>1] = (bufV[x>>1] * basealphaUV + av) >> 10;
      }
    }
    buf += pitch<<1;
    bufU += pitchUV;
    bufV += pitchUV;
    alpha += w<<3;
  }
}


void Antialiaser::ApplyPlanar(BYTE* buf, int pitch, int pitchUV, BYTE* bufU, BYTE* bufV, int shiftX, int shiftY) {
  const int stepX = 1<<shiftX;
  const int stepY = 1<<shiftY;

  if (dirty) {
    GetAlphaRect();
    xl &= -stepX; xr |= stepX-1;
    yb &= -stepY; yt |= stepY-1;
  }
  const int w4 = w*4;
  unsigned short* alpha = alpha_calcs + yb*w4;
  buf += pitch*yb;

  // Apply Y
  {for (int y=yb; y<=yt; y+=1) {
    for (int x=xl; x<=xr; x+=1) {
      const int x4 = x<<2;
      const int basealpha = alpha[x4+0];

      if (basealpha != 256) {
        buf[x] = (buf[x] * basealpha + alpha[x4+3]) >> 8;
      }
    }
    buf += pitch;
    alpha += w4;
  }}

  if (!bufU) return;

  // This will not be fast, but it will be generic.
  const int skipThresh = 256 << (shiftX+shiftY);
  const int shifter = 8+shiftX+shiftY;
  const int UVw4 = w<<(2+shiftY);
  const int xlshiftX = xl>>shiftX;

  alpha = alpha_calcs + yb*w4;
  bufU += (pitchUV*yb)>>shiftY;
  bufV += (pitchUV*yb)>>shiftY;

  {for (int y=yb; y<=yt; y+=stepY) {
    for (int x=xl, xs=xlshiftX; x<=xr; x+=stepX, xs+=1) {
      unsigned short* UValpha = alpha + x*4;
      int basealphaUV = 0;
      int au = 0;
      int av = 0;
      for (int i = 0; i<stepY; i++) {
        for (int j = 0; j<stepX; j++) {
          basealphaUV += UValpha[0 + j*4];
          av          += UValpha[1 + j*4];
          au          += UValpha[2 + j*4];
        }
        UValpha += w4;
      }
      if (basealphaUV != skipThresh) {
        bufU[xs] = (bufU[xs] * basealphaUV + au) >> shifter;
        bufV[xs] = (bufV[xs] * basealphaUV + av) >> shifter;
      }
    }// end for x
    bufU  += pitchUV;
    bufV  += pitchUV;
    alpha += UVw4;
  }}//end for y
}


void Antialiaser::ApplyYUY2(BYTE* buf, int pitch) {
  if (dirty) {
    GetAlphaRect();
	xl &= -2; xr |= 1;
  }
  unsigned short* alpha = alpha_calcs + yb*w*4;
  buf += pitch*yb;

  for (int y=yb; y<=yt; ++y) {
    for (int x=xl; x<=xr; x+=2) {
      const int basealpha0  = alpha[x*4+0];
      const int basealpha1  = alpha[x*4+4];
      const int basealphaUV = basealpha0 + basealpha1;

      if (basealphaUV != 512) {
        buf[x*2+0] = (buf[x*2+0] * basealpha0 + alpha[x*4+3]) >> 8;
        buf[x*2+2] = (buf[x*2+2] * basealpha1 + alpha[x*4+7]) >> 8;

        const int au  = alpha[x*4+2] + alpha[x*4+6];
        buf[x*2+1] = (buf[x*2+1] * basealphaUV + au) >> 9;

        const int av  = alpha[x*4+1] + alpha[x*4+5];
        buf[x*2+3] = (buf[x*2+3] * basealphaUV + av) >> 9;
      }
    }
    buf += pitch;
    alpha += w*4;
  }
}


void Antialiaser::ApplyRGB24(BYTE* buf, int pitch) {
  if (dirty) GetAlphaRect();
  unsigned short* alpha = alpha_calcs + yb*w*4;
  buf  += pitch*(h-yb-1);

  for (int y=yb; y<=yt; ++y) {
    for (int x=xl; x<=xr; ++x) {
      const int basealpha = alpha[x*4+0];
      if (basealpha != 256) {
        buf[x*3+0] = (buf[x*3+0] * basealpha + alpha[x*4+1]) >> 8;
        buf[x*3+1] = (buf[x*3+1] * basealpha + alpha[x*4+2]) >> 8;
        buf[x*3+2] = (buf[x*3+2] * basealpha + alpha[x*4+3]) >> 8;
      }
    }
    buf -= pitch;
    alpha += w*4;
  }
}


void Antialiaser::ApplyRGB32(BYTE* buf, int pitch) {
  if (dirty) GetAlphaRect();
  unsigned short* alpha = alpha_calcs + yb*w*4;
  buf  += pitch*(h-yb-1);

  for (int y=yb; y<=yt; ++y) {
    for (int x=xl; x<=xr; ++x) {
      const int basealpha = alpha[x*4+0];
      if (basealpha != 256) {
        buf[x*4+0] = (buf[x*4+0] * basealpha + alpha[x*4+1]) >> 8;
        buf[x*4+1] = (buf[x*4+1] * basealpha + alpha[x*4+2]) >> 8;
        buf[x*4+2] = (buf[x*4+2] * basealpha + alpha[x*4+3]) >> 8;
      }
    }
    buf -= pitch;
    alpha += w*4;
  }
}


void Antialiaser::GetAlphaRect()
{
  dirty = false;

  static BYTE bitcnt[256],    // bit count
              bitexl[256],    // expand to left bit
              bitexr[256];    // expand to right bit
  static bool fInited = false;
  static unsigned short gamma[129]; // Gamma lookups

  if (!fInited) {
    fInited = true;

    const double scale = 516*64/sqrt(128.0);
    {for(int i=0; i<=128; i++)
      gamma[i]=unsigned short(sqrt((double)i) * scale + 0.5); // Gamma = 2.0
    }

	{for(int i=0; i<256; i++) {
      BYTE b=0, l=0, r=0;

      if (i&  1) { b=1; l|=0x01; r|=0xFF; }
      if (i&  2) { ++b; l|=0x03; r|=0xFE; }
      if (i&  4) { ++b; l|=0x07; r|=0xFC; }
      if (i&  8) { ++b; l|=0x0F; r|=0xF8; }
      if (i& 16) { ++b; l|=0x1F; r|=0xF0; }
      if (i& 32) { ++b; l|=0x3F; r|=0xE0; }
      if (i& 64) { ++b; l|=0x7F; r|=0xC0; }
      if (i&128) { ++b; l|=0xFF; r|=0x80; }

      bitcnt[i] = b;
      bitexl[i] = l;
      bitexr[i] = r;
    }}
  }

  const int RYtext = ((textcolor>>16)&255), GUtext = ((textcolor>>8)&255), BVtext = (textcolor&255);
  const int RYhalo = ((halocolor>>16)&255), GUhalo = ((halocolor>>8)&255), BVhalo = (halocolor&255);

  // Scaled Alpha
  const int Atext = 255 - ((textcolor >> 24) & 0xFF);
  const int Ahalo = 255 - ((halocolor >> 24) & 0xFF);

  const int srcpitch = (w+4+3) & -4;

  xl=0;
  xr=w+1;
  yt=-1;
  yb=h;

  unsigned short* dest = alpha_calcs;
  for (int y=0; y<h; ++y) {
    BYTE* src = (BYTE*)lpAntialiasBits + ((h-y-1)*8 + 20) * srcpitch + 2;
    int wt = w;
    do {
      int i;

#pragma warning(push)
#pragma warning(disable: 4068)
      DWORD tmp = 0;

      if (interlaced) {
#pragma unroll
        for (int i = -8; i < 16; ++i) {
          tmp |= *reinterpret_cast<int*>(src + srcpitch*i - 1);
        }
      } else {
#pragma unroll
        for (int i = -12; i < 20; ++i) {
          tmp |= *reinterpret_cast<int*>(src + srcpitch*i - 1);
        }
      }

      tmp &= 0x00FFFFFF;
#pragma warning(pop)


      if (tmp != 0) {     // quick exit in a common case
        if (wt >= xl) xl=wt;
        if (wt <= xr) xr=wt;
        if (y  >= yt) yt=y;
        if (y  <= yb) yb=y;

        int alpha1, alpha2;

        alpha1 = alpha2 = 0;

        if (interlaced) {
          BYTE topmask=0, cenmask=0, botmask=0;
          BYTE hmasks[16], mask;

          for(i=-4; i<12; i++) {// For interlaced include extra half cells above and below
            mask = src[srcpitch*i];
            // How many lit pixels in the centre cell?
            alpha1 += bitcnt[mask];
            // turn on all halo bits if cell has any lit pixels
            mask = - !! mask;
            // Check left and right neighbours, extend the halo
            // mask 8 pixels in from the nearest lit pixels.
            mask |= bitexr[src[srcpitch*i-1]];
            mask |= bitexl[src[srcpitch*i+1]];
            hmasks[i+4] = mask;
          }

          // Extend halo vertically to 8x8 blocks
          for(i=-4; i<4;  i++) topmask |= hmasks[i+4];
          for(i=0;  i<8;  i++) cenmask |= hmasks[i+4];
          for(i=4;  i<12; i++) botmask |= hmasks[i+4];
          // Check the 3x1.5 cells above
          for(mask = topmask, i=-4; i<4; i++) {
            mask |= bitexr[ src[srcpitch*(i+8)-1] ];
            mask |=    - !! src[srcpitch*(i+8)  ];
            mask |= bitexl[ src[srcpitch*(i+8)+1] ];
            hmasks[i+4] |= mask;
          }
          for(mask = cenmask, i=0; i<8; i++) {
            mask |= bitexr[ src[srcpitch*(i+8)-1] ];
            mask |=    - !! src[srcpitch*(i+8)  ];
            mask |= bitexl[ src[srcpitch*(i+8)+1] ];
            hmasks[i+4] |= mask;
          }
          for(mask = botmask, i=4; i<12; i++) {
            mask |= bitexr[ src[srcpitch*(i+8)-1] ];
            mask |=    - !! src[srcpitch*(i+8)  ];
            mask |= bitexl[ src[srcpitch*(i+8)+1] ];
            hmasks[i+4] |= mask;
          }
          // Check the 3x1.5 cells below
          for(mask = botmask, i=11; i>=4; i--) {
            mask |= bitexr[ src[srcpitch*(i-8)-1] ];
            mask |=    - !! src[srcpitch*(i-8)  ];
            mask |= bitexl[ src[srcpitch*(i-8)+1] ];
            hmasks[i+4] |= mask;
          }
          for(mask = cenmask,i=7; i>=0; i--) {
            mask |= bitexr[ src[srcpitch*(i-8)-1] ];
            mask |=    - !! src[srcpitch*(i-8)  ];
            mask |= bitexl[ src[srcpitch*(i-8)+1] ];
            hmasks[i+4] |= mask;
          }
          for(mask = topmask, i=3; i>=-4; i--) {
            mask |= bitexr[ src[srcpitch*(i-8)-1] ];
            mask |=    - !! src[srcpitch*(i-8)  ];
            mask |= bitexl[ src[srcpitch*(i-8)+1] ];
            hmasks[i+4] |= mask;
          }
          // count the halo pixels
          for(i=0; i<16; i++)
            alpha2 += bitcnt[hmasks[i]];
        }
        else {
          // How many lit pixels in the centre cell?
          for(i=0; i<8; i++)
            alpha1 += bitcnt[src[srcpitch*i]];
          alpha1 *=2;

          if (alpha1) {
            // If we have any lit pixels we fully occupy the cell.
            alpha2 = 128;
          }
          else {
            // No lit pixels here so build the halo mask from the neighbours
            BYTE cenmask = 0;

            // Check left and right neighbours, extend the halo
            // mask 8 pixels in from the nearest lit pixels.
            for(i=0; i<8; i++) {
              cenmask |= bitexr[src[srcpitch*i-1]];
              cenmask |= bitexl[src[srcpitch*i+1]];
            }

            if (cenmask == 0xFF) {
              // If we have hard adjacent lit pixels we fully occupy this cell.
              alpha2 = 128;
            }
            else {
              BYTE hmasks[8], mask;

              mask = cenmask;
              for(i=0; i<8; i++) {
                // Check the 3 cells above
                mask |= bitexr[ src[srcpitch*(i+8)-1] ];
                mask |=    - !! src[srcpitch*(i+8)  ];
                mask |= bitexl[ src[srcpitch*(i+8)+1] ];
                hmasks[i] = mask;
              }

              mask = cenmask;
              for(i=7; i>=0; i--) {
                // Check the 3 cells below
                mask |= bitexr[ src[srcpitch*(i-8)-1] ];
                mask |=    - !! src[srcpitch*(i-8)  ];
                mask |= bitexl[ src[srcpitch*(i-8)+1] ];

                alpha2 += bitcnt[hmasks[i] | mask];
              }
              alpha2 *=2;
            }
          }
        }
        alpha2  = gamma[alpha2];
        alpha1  = gamma[alpha1];

        alpha2 -= alpha1;        
        alpha2 *= Ahalo;
        alpha1 *= Atext;
        // Pre calulate table for quick use  --  Pc = (Pc * dest[0] + dest[c]) >> 8;

        dest[0] = (64*516*255 - alpha1 -          alpha2)>>15;
        dest[1] = (    BVtext * alpha1 + BVhalo * alpha2)>>15;
        dest[2] = (    GUtext * alpha1 + GUhalo * alpha2)>>15;
        dest[3] = (    RYtext * alpha1 + RYhalo * alpha2)>>15;
      }
      else {
        dest[0] = 256;
        dest[1] = 0;
        dest[2] = 0;
        dest[3] = 0;
      }

      dest += 4;
      ++src;
    } while(--wt);
  }

  xl=w-xl;
  xr=w-xr;
}





/*************************************
 *******   Show Frame Number    ******
 ************************************/

ShowFrameNumber::ShowFrameNumber(PClip _child, bool _scroll, int _offset, int _x, int _y, const char _fontname[],
					 int _size, int _textcolor, int _halocolor, int font_width, int font_angle, IScriptEnvironment* env)
 : GenericVideoFilter(_child), scroll(_scroll), offset(_offset), x(_x), y(_y), size(_size),
   antialiaser(vi.width, vi.height, _fontname, _size,
               vi.IsYUV() ? RGB2YUV(_textcolor) : _textcolor,
               vi.IsYUV() ? RGB2YUV(_halocolor) : _halocolor,
			   font_width, font_angle)
{
}

enum { DefXY = 0x80000000 };

PVideoFrame ShowFrameNumber::GetFrame(int n, IScriptEnvironment* env) {
  PVideoFrame frame = child->GetFrame(n, env);
  n+=offset;
  if (n < 0) return frame;

  HDC hdc = antialiaser.GetDC();
  if (!hdc) return frame;

  env->MakeWritable(&frame);

  RECT r = { 0, 0, 32767, 32767 };
  FillRect(hdc, &r, (HBRUSH)GetStockObject(BLACK_BRUSH));
  char text[16];
  _snprintf(text, sizeof(text), "%05d", n);
  text[15] = 0;
  if (x!=DefXY || y!=DefXY) {
    SetTextAlign(hdc, TA_BASELINE|TA_LEFT);
    TextOut(hdc, x+16, y+16, text, (int)strlen(text));
  } else if (scroll) {
    int n1 = vi.IsFieldBased() ? (n/2) : n;
    int y2 = size + size*(n1%(vi.height*8/size));
    SetTextAlign(hdc, TA_BASELINE | (child->GetParity(n) ? TA_LEFT : TA_RIGHT));
    TextOut(hdc, child->GetParity(n) ? 32 : vi.width*8+8, y2, text, (int)strlen(text));
  } else {
    SetTextAlign(hdc, TA_BASELINE | (child->GetParity(n) ? TA_LEFT : TA_RIGHT));
    int text_len = (int)strlen(text);
    for (int y2=size; y2<vi.height*8; y2 += size)
	    TextOut(hdc, child->GetParity(n) ? 32 : vi.width*8+8, y2, text, text_len);
  }
  GdiFlush();

  antialiaser.Apply(vi, &frame, frame->GetPitch());

  return frame;
}


AVSValue __cdecl ShowFrameNumber::Create(AVSValue args, void*, IScriptEnvironment* env) 
{
  PClip clip = args[0].AsClip();
  bool scroll = args[1].AsBool(false);
  const int offset = args[2].AsInt(0);
  const int x = args[3].IsFloat() ? int(args[3].AsFloat()*8+0.5) : DefXY;
  const int y = args[4].IsFloat() ? int(args[4].AsFloat()*8+0.5) : DefXY;
  const char* font = args[5].AsString("Arial");
  const int size = int(args[6].AsFloat(24)*8+0.5);
  const int text_color = args[7].AsInt(0xFFFF00);
  const int halo_color = args[8].AsInt(0);
  const int font_width = int(args[9].AsFloat(0)*8+0.5);
  const int font_angle = int(args[10].AsFloat(0)*10+0.5);

  if ((x==DefXY) ^ (y==DefXY))
	env->ThrowError("ShowFrameNumber: both x and y position must be specified");

  return new ShowFrameNumber(clip, scroll, offset, x, y, font, size, text_color, halo_color, font_width, font_angle, env);
}








/***********************************
 *******   Show SMPTE code    ******
 **********************************/

ShowSMPTE::ShowSMPTE(PClip _child, double _rate, const char* offset, int _offset_f, int _x, int _y, const char _fontname[],
					 int _size, int _textcolor, int _halocolor, int font_width, int font_angle, IScriptEnvironment* env)
  : GenericVideoFilter(_child), x(_x), y(_y),
    antialiaser(vi.width, vi.height, _fontname, _size,
                vi.IsYUV() ? RGB2YUV(_textcolor) : _textcolor,
                vi.IsYUV() ? RGB2YUV(_halocolor) : _halocolor,
			    font_width, font_angle)
{
  int off_f, off_sec, off_min, off_hour;

  rate = int(_rate + 0.5);
  dropframe = false;
  if (_rate > 23.975 && _rate < 23.977) { // Pulldown drop frame rate
    rate = 24;
    dropframe = true;
  } 
  else if (_rate > 29.969 && _rate < 29.971) {
    rate = 30;
    dropframe = true;
  } 
  else if (_rate > 47.951 && _rate < 47.953) {
    rate = 48;
    dropframe = true;
  } 
  else if (_rate > 59.939 && _rate < 59.941) {
    rate = 60;
    dropframe = true;
  } 
  else if (_rate > 119.879 && _rate < 119.881) {
    rate = 120;
    dropframe = true;
  } 
  else if (abs(_rate - rate) > 0.001) {
    env->ThrowError("ShowSMPTE: rate argument must be 23.976, 29.97 or an integer");
  }

  if (offset) {
	if (strlen(offset)!=11 || offset[2] != ':' || offset[5] != ':' || offset[8] != ':')
	  env->ThrowError("ShowSMPTE:  offset should be of the form \"00:00:00:00\" ");
	if (!isdigit(offset[0]) || !isdigit(offset[1]) || !isdigit(offset[3]) || !isdigit(offset[4])
	 || !isdigit(offset[6]) || !isdigit(offset[7]) || !isdigit(offset[9]) || !isdigit(offset[10]))
	  env->ThrowError("ShowSMPTE:  offset should be of the form \"00:00:00:00\" ");

	off_hour = atoi(offset);

	off_min = atoi(offset+3);
	if (off_min > 59)
	  env->ThrowError("ShowSMPTE:  make sure that the number of minutes in the offset is in the range 0..59");

	off_sec = atoi(offset+6);
	if (off_sec > 59)
	  env->ThrowError("ShowSMPTE:  make sure that the number of seconds in the offset is in the range 0..59");

	off_f = atoi(offset+9);
	if (off_f >= rate)
	  env->ThrowError("ShowSMPTE:  make sure that the number of frames in the offset is in the range 0..%d", rate-1);

	offset_f = off_f + rate*(off_sec + 60*off_min + 3600*off_hour);
	if (dropframe) {
	  if (rate == 30) {
		int c = 0;
		c = off_min + 60*off_hour;  // number of drop events
		c -= c/10; // less non-drop events on 10 minutes
		c *=2; // drop 2 frames per drop event
		offset_f -= c;
	  }
	  else {
//  Need to cogitate with the guys about this
//  gotta drop 86.3 counts per hour. So until
//  a proper formula is found, just wing it!
		offset_f -= 2 * ((offset_f+1001)/2002);
	  }
	}
  }
  else {
	offset_f = _offset_f;
  }
}


PVideoFrame __stdcall ShowSMPTE::GetFrame(int n, IScriptEnvironment* env) 
{
  PVideoFrame frame = child->GetFrame(n, env);
  n+=offset_f;
  if (n < 0) return frame;

  HDC hdc = antialiaser.GetDC();
  if (!hdc) return frame;

  env->MakeWritable(&frame);

  if (dropframe) {
    if ((rate == 30) || (rate == 60) || (rate == 120)) {
	// at 10:00, 20:00, 30:00, etc. nothing should happen if offset=0
	  const int f = rate/30;
	  const int r = n % f;
	  n /= f;

	  const int high = n / 17982;
	  int low = n % 17982;
	  if (low>=2)
		low += 2 * ((low-2) / 1798);
	  n = high * 18000 + low;
	  
	  n = f*n + r;
	}
	else {
//  Needs some cogitating
	  n += 2 * ((n+1001)/2002);
	}
  }

  char text[16];

  if (rate > 0) {
    int frames = n % rate;
    int sec = n/rate;
    int min = sec/60;
    int hour = sec/3600;

    _snprintf(text, sizeof(text),
              rate>99 ? "%02d:%02d:%02d:%03d" : "%02d:%02d:%02d:%02d",
              hour, min%60, sec%60, frames);
  }
  else {
    int ms = (int)(((__int64)n * vi.fps_denominator * 1000 / vi.fps_numerator)%1000);
    int sec = (int)((__int64)n * vi.fps_denominator / vi.fps_numerator);
    int min = sec/60;
    int hour = sec/3600;

    _snprintf(text, sizeof(text), "%02d:%02d:%02d.%03d", hour, min%60, sec%60, ms);
  }
  text[15] = 0;

  SetTextAlign(hdc, TA_BASELINE|TA_CENTER);
  TextOut(hdc, x+16, y+16, text, (int)strlen(text));
  GdiFlush();

  antialiaser.Apply(vi, &frame, frame->GetPitch());

  return frame;
}

AVSValue __cdecl ShowSMPTE::CreateSMTPE(AVSValue args, void*, IScriptEnvironment* env)
{
  PClip clip = args[0].AsClip();
  const VideoInfo& arg0vi = args[0].AsClip()->GetVideoInfo();
  double def_rate = (double)arg0vi.fps_numerator / arg0vi.fps_denominator;
  double dfrate = args[1].AsDblDef(def_rate);
  const char* offset = args[2].AsString(0);
  const int offset_f = args[3].AsInt(0);
  const int xreal = arg0vi.width/2;
  const int yreal = arg0vi.height-8;
  const int x = int(args[4].AsDblDef(xreal)*8+0.5);
  const int y = int(args[5].AsDblDef(yreal)*8+0.5);
  const char* font = args[6].AsString("Arial");
  const int size = int(args[7].AsFloat(24)*8+0.5);
  const int text_color = args[8].AsInt(0xFFFF00);
  const int halo_color = args[9].AsInt(0);
  const int font_width = int(args[10].AsFloat(0)*8+0.5);
  const int font_angle = int(args[11].AsFloat(0)*10+0.5);
  return new ShowSMPTE(clip, dfrate, offset, offset_f, x, y, font, size, text_color, halo_color, font_width, font_angle, env);
}

AVSValue __cdecl ShowSMPTE::CreateTime(AVSValue args, void*, IScriptEnvironment* env)
{
  PClip clip = args[0].AsClip();
  const int offset_f = args[1].AsInt(0);
  const int xreal = args[0].AsClip()->GetVideoInfo().width/2;
  const int yreal = args[0].AsClip()->GetVideoInfo().height-8;
  const int x = int(args[2].AsDblDef(xreal)*8+0.5);
  const int y = int(args[3].AsDblDef(yreal)*8+0.5);
  const char* font = args[4].AsString("Arial");
  const int size = int(args[5].AsFloat(24)*8+0.5);
  const int text_color = args[6].AsInt(0xFFFF00);
  const int halo_color = args[7].AsInt(0);
  const int font_width = int(args[8].AsFloat(0)*8+0.5);
  const int font_angle = int(args[9].AsFloat(0)*10+0.5);
  return new ShowSMPTE(clip, 0.0, NULL, offset_f, x, y, font, size, text_color, halo_color, font_width, font_angle, env);
}






/***********************************
 *******   Subtitle Filter    ******
 **********************************/


Subtitle::Subtitle( PClip _child, const char _text[], int _x, int _y, int _firstframe, 
                    int _lastframe, const char _fontname[], int _size, int _textcolor, 
                    int _halocolor, int _align, int _spc, bool _multiline, int _lsp,
					int _font_width, int _font_angle, bool _interlaced )
 : GenericVideoFilter(_child), antialiaser(0), text(_text), x(_x), y(_y), 
   firstframe(_firstframe), lastframe(_lastframe), fontname(_fontname), size(_size),
   textcolor(vi.IsYUV() ? RGB2YUV(_textcolor) : _textcolor),
   halocolor(vi.IsYUV() ? RGB2YUV(_halocolor) : _halocolor),
   align(_align), spc(_spc), multiline(_multiline), lsp(_lsp),
   font_width(_font_width), font_angle(_font_angle), interlaced(_interlaced)
{
}



Subtitle::~Subtitle(void) 
{
  delete antialiaser;
}



PVideoFrame Subtitle::GetFrame(int n, IScriptEnvironment* env) 
{
  PVideoFrame frame = child->GetFrame(n, env);

  if (n >= firstframe && n <= lastframe) {
    env->MakeWritable(&frame);
    if (!antialiaser) // :FIXME: CriticalSection
	  InitAntialiaser(env);
    if (antialiaser) {
	  antialiaser->Apply(vi, &frame, frame->GetPitch());
	  // Release all the windows drawing stuff
	  // and just keep the alpha calcs
	  antialiaser->FreeDC();
	}
  }
  // if we get far enough away from the frames we're supposed to
  // subtitle, then junk the buffered drawing information
  if (antialiaser && (n < firstframe-10 || n > lastframe+10 || n == vi.num_frames-1)) {
	delete antialiaser;
	antialiaser = 0; // :FIXME: CriticalSection
  }

  return frame;
}

AVSValue __cdecl Subtitle::Create(AVSValue args, void*, IScriptEnvironment* env) 
{
    PClip clip = args[0].AsClip();
    const char* text = args[1].AsString();
    const int first_frame = args[4].AsInt(0);
    const int last_frame = args[5].AsInt(clip->GetVideoInfo().num_frames-1);
    const char* font = args[6].AsString("Arial");
    const int size = int(args[7].AsFloat(18)*8+0.5);
    const int text_color = args[8].AsInt(0xFFFF00);
    const int halo_color = args[9].AsInt(0);
    const int align = args[10].AsInt(args[2].AsFloat(0)==-1?2:7);
    const int spc = args[11].AsInt(0);
    const bool multiline = args[12].Defined();
    const int lsp = args[12].AsInt(0);
	const int font_width = int(args[13].AsFloat(0)*8+0.5);
	const int font_angle = int(args[14].AsFloat(0)*10+0.5);
	const bool interlaced = args[15].AsBool(false);

    if ((align < 1) || (align > 9))
     env->ThrowError("Subtitle: Align values are 1 - 9 mapped to your numeric pad");

    int defx, defy;
    switch (align) {
	 case 1: case 4: case 7: defx = 8; break;
     case 2: case 5: case 8: defx = -1; break;
     case 3: case 6: case 9: defx = clip->GetVideoInfo().width-8; break;
     default: defx = 8; break; }
    switch (align) {
     case 1: case 2: case 3: defy = clip->GetVideoInfo().height-2; break;
     case 4: case 5: case 6: defy = -1; break;
	 case 7: case 8: case 9: defy = 0; break;
     default: defy = (size+4)/8; break; }

    const int x = int(args[2].AsDblDef(defx)*8+0.5);
    const int y = int(args[3].AsDblDef(defy)*8+0.5);

    return new Subtitle(clip, text, x, y, first_frame, last_frame, font, size, text_color,
	                    halo_color, align, spc, multiline, lsp, font_width, font_angle, interlaced);
}



void Subtitle::InitAntialiaser(IScriptEnvironment* env) 
{
  antialiaser = new Antialiaser(vi.width, vi.height, fontname, size, textcolor, halocolor,
                                font_width, font_angle, interlaced);

  int real_x = x;
  int real_y = y;
  unsigned int al = 0;
  char *_text = 0;

  HDC hdcAntialias = antialiaser->GetDC();
  if (!hdcAntialias) goto GDIError;

  switch (align) // This spec where [X, Y] is relative to the text (inverted logic)
  { case 1: al = TA_BOTTOM   | TA_LEFT; break;		// .----
    case 2: al = TA_BOTTOM   | TA_CENTER; break;	// --.--
    case 3: al = TA_BOTTOM   | TA_RIGHT; break;		// ----.
    case 4: al = TA_BASELINE | TA_LEFT; break;		// .____
    case 5: al = TA_BASELINE | TA_CENTER; break;	// __.__
    case 6: al = TA_BASELINE | TA_RIGHT; break;		// ____.
    case 7: al = TA_TOP      | TA_LEFT; break;		// `----
    case 8: al = TA_TOP      | TA_CENTER; break;	// --`--
    case 9: al = TA_TOP      | TA_RIGHT; break;		// ----`
    default: al= TA_BASELINE | TA_LEFT; break;		// .____
  }
  if (SetTextCharacterExtra(hdcAntialias, spc) == 0x80000000) goto GDIError;
  if (SetTextAlign(hdcAntialias, al) == GDI_ERROR) goto GDIError;

  if (x==-7) real_x = (vi.width>>1)*8;
  if (y==-7) real_y = (vi.height>>1)*8;

  if (!multiline) {
	if (!TextOut(hdcAntialias, real_x+16, real_y+16, text, (int)strlen(text))) goto GDIError;
  }
  else {
	// multiline patch -- tateu
	char *pdest, *psrc;
	int result, y_inc = real_y+16;
	char search[] = "\\n";
	psrc = _text = _strdup(text); // don't mangle the string constant -- Gavino
	if (!_text) goto GDIError;
	int length = (int)strlen(psrc);

	do {
	  pdest = strstr(psrc, search);
	  while (pdest != NULL && pdest != psrc && *(pdest-1)=='\\') { // \n escape -- foxyshadis
		for (int i=pdest-psrc; i>0; i--) psrc[i] = psrc[i-1];
		psrc++;
		--length;
		pdest = strstr(pdest+1, search);
	  }
	  result = pdest == NULL ? length : pdest - psrc;
	  if (!TextOut(hdcAntialias, real_x+16, y_inc, psrc, result)) goto GDIError;
	  y_inc += size + lsp;
	  psrc = pdest + 2;
	  length -= result + 2;
	} while (pdest != NULL && length > 0);
	free(_text);
	_text = NULL;
  }
  if (!GdiFlush()) goto GDIError;
  return;

GDIError:
  delete antialiaser;
  antialiaser = 0;
  free(_text);

  env->ThrowError("Subtitle: GDI or Insufficient Memory Error");
}






inline int CalcFontSize(int w, int h)
{
  enum { minFS=8, FS=128, minH=224, minW=388 };

  const int ws = (w < minW) ? (FS*w)/minW : FS;
  const int hs = (h < minH) ? (FS*h)/minH : FS;
  const int fs = (ws < hs) ? ws : hs;
  return ( (fs < minFS) ? minFS : fs );
}


/***********************************
 *******   FilterInfo Filter    ******
 **********************************/


FilterInfo::FilterInfo( PClip _child)
: GenericVideoFilter(_child), vii(AdjustVi()),
  antialiaser(vi.width, vi.height, "Courier New", CalcFontSize(vi.width, vi.height), vi.IsYUV() ? 0xD21092 : 0xFFFF00, vi.IsYUV() ? 0x108080 : 0) {
}


FilterInfo::~FilterInfo(void) 
{
}


const VideoInfo& FilterInfo::AdjustVi() 
{
  if ( !vi.HasVideo() ) {
    vi.fps_denominator=1;
    vi.fps_numerator=24;
    vi.height=480;
    vi.num_frames=240;
    vi.pixel_type=VideoInfo::CS_BGR32;
    vi.width=640;
    vi.SetFieldBased(false);
  }
  return child->GetVideoInfo();
}


const char* const t_YV12="YV12";
const char* const t_YUY2="YUY2";
const char* const t_RGB32="RGB32";
const char* const t_RGB24="RGB24";
const char* const t_YV24="YV24";
const char* const t_Y8="Y8";
const char* const t_YV16="YV16";
const char* const t_Y41P="YUV 411 Planar";
const char* const t_INT8="Integer 8 bit";
const char* const t_INT16="Integer 16 bit";
const char* const t_INT24="Integer 24 bit";
const char* const t_INT32="Integer 32 bit";
const char* const t_FLOAT32="Float 32 bit";
const char* const t_YES="YES";
const char* const t_NO="NO";
const char* const t_NONE="NONE";
const char* const t_TFF ="Top Field First            ";
const char* const t_BFF ="Bottom Field First         ";
const char* const t_ATFF="Assumed Top Field First    ";
const char* const t_ABFF="Assumed Bottom Field First ";
const char* const t_STFF="Top Field (Separated)      ";
const char* const t_SBFF="Bottom Field (Separated)   ";


std::string GetCpuMsg(IScriptEnvironment * env)
{
  int flags = env->GetCPUFlags();
  std::stringstream ss;

  if (flags & CPUF_FPU)
    ss << "x87  ";
  if (flags & CPUF_MMX)
    ss << "MMX  ";
  if (flags & CPUF_INTEGER_SSE)
    ss << "ISSE  ";

  if (flags & CPUF_SSE4_2)
    ss << "SSE4.2 ";
  else if (flags & CPUF_SSE4_1)
    ss << "SSE4.1 ";
  else if (flags & CPUF_SSE3)
    ss << "SSE3 ";
  else if (flags & CPUF_SSE2)
    ss << "SSE2 ";
  else if (flags & CPUF_SSE)
    ss << "SSE  ";

  if (flags & CPUF_SSSE3)
    ss << "SSSE3 ";

  if (flags & CPUF_3DNOW_EXT)
    ss << "3DNOW_EXT";
  else if (flags & CPUF_3DNOW)
    ss << "3DNOW ";

  return ss.str();
}


bool FilterInfo::GetParity(int n)
{
  return vii.HasVideo() ? child->GetParity(n) : false;
}


PVideoFrame FilterInfo::GetFrame(int n, IScriptEnvironment* env) 
{
  PVideoFrame frame = vii.HasVideo() ? child->GetFrame(n, env) : env->NewVideoFrame(vi);

  if ( !vii.HasVideo() ) {
	memset(frame->GetWritePtr(), 0, frame->GetPitch()*frame->GetHeight()); // Blank frame
  }

  HDC hdcAntialias = antialiaser.GetDC();
  if (hdcAntialias) {
    const char* c_space = "Unknown";
    const char* s_type = t_NONE;
    const char* s_parity;
    char text[512];
	int tlen;
    RECT r= { 32, 16, min(3440,vi.width*8), 900*2 };

    if (vii.HasVideo()) {
      if      (vii.IsRGB24()) c_space=t_RGB24;
      else if (vii.IsRGB32()) c_space=t_RGB32;
      else if (vii.IsYV12())  c_space=t_YV12;
      else if (vii.IsYUY2())  c_space=t_YUY2;
      else if (vii.IsYV24())  c_space=t_YV24;
      else if (vii.IsY8())    c_space=t_Y8;
      else if (vii.IsYV16())  c_space=t_YV16;
      else if (vii.IsYV411()) c_space=t_Y41P;

      if (vii.IsFieldBased()) {
        if (child->GetParity(n)) {
          s_parity = t_STFF;
        } else {
          s_parity = t_SBFF;
        }
      } else {
        if (child->GetParity(n)) {
          s_parity = vii.IsTFF() ? t_ATFF : t_TFF;
        } else {
          s_parity = vii.IsBFF() ? t_ABFF : t_BFF;
        }
      }
      int vLenInMsecs = (int)(1000.0 * (double)vii.num_frames * (double)vii.fps_denominator / (double)vii.fps_numerator);
      int cPosInMsecs = (int)(1000.0 * (double)n * (double)vii.fps_denominator / (double)vii.fps_numerator);

      tlen = _snprintf(text, sizeof(text),
        "Frame: %8u of %-8u\n"                                //  28
        "Time: %02d:%02d:%02d.%03d of %02d:%02d:%02d.%03d\n"  //  35
        "ColorSpace: %s\n"                                    //  18=13+5
        "Width:%4u pixels, Height:%4u pixels.\n"              //  39
        "Frames per second: %7.4f (%u/%u)\n"                  //  51=31+20
        "FieldBased (Separated) Video: %s\n"                  //  35=32+3
        "Parity: %s\n"                                        //  35=9+26
        "Video Pitch: %5u bytes.\n"                           //  25
        "Has Audio: %s\n"                                     //  15=12+3
        , n, vii.num_frames
        , (cPosInMsecs/(60*60*1000)), (cPosInMsecs/(60*1000))%60 ,(cPosInMsecs/1000)%60, cPosInMsecs%1000,
          (vLenInMsecs/(60*60*1000)), (vLenInMsecs/(60*1000))%60 ,(vLenInMsecs/1000)%60, vLenInMsecs%1000 
        , c_space
        , vii.width, vii.height
        , (float)vii.fps_numerator/(float)vii.fps_denominator, vii.fps_numerator, vii.fps_denominator
        , vii.IsFieldBased() ? t_YES : t_NO
        , s_parity
        , frame->GetPitch()
        , vii.HasAudio() ? t_YES : t_NO
      );
    }
    else {
      tlen = _snprintf(text, sizeof(text),
        "Frame: %8u of %-8u\n"
        "Has Video: NO\n"
        "Has Audio: %s\n"
        , n, vi.num_frames
        , vii.HasAudio() ? t_YES : t_NO
      );
    }
    if (vii.HasAudio()) {
      if      (vii.SampleType()==SAMPLE_INT8)  s_type=t_INT8;
      else if (vii.SampleType()==SAMPLE_INT16) s_type=t_INT16;
      else if (vii.SampleType()==SAMPLE_INT24) s_type=t_INT24;
      else if (vii.SampleType()==SAMPLE_INT32) s_type=t_INT32;
      else if (vii.SampleType()==SAMPLE_FLOAT) s_type=t_FLOAT32;

      int aLenInMsecs = (int)(1000.0 * (double)vii.num_audio_samples / (double)vii.audio_samples_per_second);
	  tlen += _snprintf(text+tlen, sizeof(text)-tlen,
		"Audio Channels: %-8u\n"                              //  25
		"Sample Type: %s\n"                                   //  28=14+14
		"Samples Per Second: %5d\n"                           //  26
		"Audio length: %I64u samples. %02d:%02d:%02d.%03d\n"  //  57=37+20
		, vii.AudioChannels()
		, s_type
		, vii.audio_samples_per_second
		, vii.num_audio_samples,
		  (aLenInMsecs/(60*60*1000)), (aLenInMsecs/(60*1000))%60, (aLenInMsecs/1000)%60, aLenInMsecs%1000
	  );
    }
	else {
	  strcpy(text+tlen,"\n");
	  tlen += 1;
	}
    tlen += _snprintf(text+tlen, sizeof(text)-tlen,
      "CPU detected: %s\n"                                  //  60=15+45
      , GetCpuMsg(env).c_str()                              // 442
    );

    DrawText(hdcAntialias, text, -1, &r, 0);
    GdiFlush();

	env->MakeWritable(&frame);
    BYTE* dstp = frame->GetWritePtr();
    int dst_pitch = frame->GetPitch();
    antialiaser.Apply(vi, &frame, dst_pitch );
  }

  return frame;
}

AVSValue __cdecl FilterInfo::Create(AVSValue args, void*, IScriptEnvironment* env) 
{
    PClip clip = args[0].AsClip();
    return new FilterInfo(clip);
}










/************************************
 *******    Compare Filter    *******
 ***********************************/


Compare::Compare(PClip _child1, PClip _child2, const char* channels, const char *fname, bool _show_graph, IScriptEnvironment* env)
  : GenericVideoFilter(_child1),
    child2(_child2),
    log(NULL),
    show_graph(_show_graph),
    antialiaser(vi.width, vi.height, "Courier New", 128, vi.IsYUV() ? 0xD21092 : 0xFFFF00, vi.IsYUV() ? 0x108080 : 0),
    framecount(0)
{
  const VideoInfo& vi2 = child2->GetVideoInfo();
  psnrs = 0;

  if (!vi.IsSameColorspace(vi2))
    env->ThrowError("Compare: Clips are not same colorspace.");

  if (vi.width != vi2.width || vi.height != vi2.height)
    env->ThrowError("Compare: Clips must have same size.");

  if (!(vi.IsRGB24() || vi.IsYUY2() || vi.IsRGB32() || vi.IsPlanar()))
    env->ThrowError("Compare: Clips have unknown pixel format. RGB24, RGB32, YUY2 and YUV Planar supported.");

  if (channels[0] == 0) {
    if (vi.IsRGB())
      channels = "RGB";
    else if (vi.IsYUV())
      channels = "YUV";
    else env->ThrowError("Compare: Clips have unknown colorspace. RGB and YUV supported.");
  }

  planar_plane = 0;
  mask = 0;
  const size_t length = strlen(channels);
  for (size_t i = 0; i < length; i++) {
    if (vi.IsRGB()) {
      switch (channels[i]) {
      case 'b':
      case 'B': mask |= 0x000000ff; break;
      case 'g':
      case 'G': mask |= 0x0000ff00; break;
      case 'r':
      case 'R': mask |= 0x00ff0000; break;
      case 'a':
      case 'A': mask |= 0xff000000; if (vi.IsRGB32()) break; // else fall thru
      default: env->ThrowError("Compare: invalid channel: %c", channels[i]);
      }
      if (vi.IsRGB24()) mask &= 0x00ffffff;   // no alpha channel in RGB24
    } else if (vi.IsPlanar()) {
      switch (channels[i]) {
      case 'y':
      case 'Y': mask |= 0xffffffff; planar_plane |= PLANAR_Y; break;
      case 'u':
      case 'U': mask |= 0xffffffff; planar_plane |= PLANAR_U; break;
      case 'v':
      case 'V': mask |= 0xffffffff; planar_plane |= PLANAR_V; break;
      default: env->ThrowError("Compare: invalid channel: %c", channels[i]);
      }
    } else {  // YUY2
      switch (channels[i]) {
      case 'y':
      case 'Y': mask |= 0x00ff00ff; break;
      case 'u':
      case 'U': mask |= 0x0000ff00; break;
      case 'v':
      case 'V': mask |= 0xff000000; break;
      default: env->ThrowError("Compare: invalid channel: %c", channels[i]);
      }
    }
  }

  masked_bytes = 0;
  for (DWORD temp = mask; temp != 0; temp >>=8)
    masked_bytes += (temp & 1);

  if (fname[0] != 0) {
    log = fopen(fname, "wt");
    if (log) {
      fprintf(log,"Comparing channel(s) %s\n\n",channels);
      fprintf(log,"           Mean               Max    Max             \n");
      fprintf(log,"         Absolute     Mean    Pos.   Neg.            \n");
      fprintf(log," Frame     Dev.       Dev.    Dev.   Dev.  PSNR (dB) \n");
      fprintf(log,"-----------------------------------------------------\n");
    } else
      env->ThrowError("Compare: unable to create file %s", fname);
  } else {
	  psnrs = new(std::nothrow) int[vi.num_frames];
    if (psnrs)
      for (int i = 0; i < vi.num_frames; i++)
        psnrs[i] = 0;
  }

}


Compare::~Compare()
{
  if (log) {
    fprintf(log,"\n\n\nTotal frames processed: %d\n\n", framecount);
    fprintf(log,"                           Minimum   Average   Maximum\n");
    fprintf(log,"Mean Absolute Deviation: %9.4f %9.4f %9.4f\n", MAD_min, MAD_tot/framecount, MAD_max);
    fprintf(log,"         Mean Deviation: %+9.4f %+9.4f %+9.4f\n", MD_min, MD_tot/framecount, MD_max);
    fprintf(log,"                   PSNR: %9.4f %9.4f %9.4f\n", PSNR_min, PSNR_tot/framecount, PSNR_max);
    double PSNR_overall = 10.0 * log10(bytecount_overall * 255.0 * 255.0 / SSD_overall);
    fprintf(log,"           Overall PSNR: %9.4f\n", PSNR_overall);
    fclose(log);
  }
  delete[] psnrs;
}


AVSValue __cdecl Compare::Create(AVSValue args, void*, IScriptEnvironment *env)
{
  return new Compare( args[0].AsClip(),     // clip
            args[1].AsClip(),     // base clip
            args[2].AsString(""),   // channels
            args[3].AsString(""),   // logfile
            args[4].AsBool(true),   // show_graph
            env);
}

static void compare_sse2(DWORD mask, int increment,
                         const BYTE * f1ptr, int pitch1, 
                         const BYTE * f2ptr, int pitch2,
                         int width, int height,
                         int &SAD_sum, int &SD_sum, int &pos_D,  int &neg_D, double &SSD_sum)
{ 
  // rowsize multiple of 16 for YUV Planar, RGB32 and YUY2; 12 for RGB24
  // increment must be 3 for RGB24 and 4 for others

  __int64 issd = 0;
  __m128i sad_vector = _mm_setzero_si128(); //sum of absolute differences
  __m128i sd_vector = _mm_setzero_si128(); // sum of differences
  __m128i positive_diff = _mm_setzero_si128();
  __m128i negative_diff = _mm_setzero_si128();
  __m128i zero = _mm_setzero_si128();

  __m128i mask64 = _mm_set_epi32(0, 0, 0, mask);
  if (increment == 3) {
    mask64 = _mm_or_si128(mask64, _mm_slli_si128(mask64, 3));
    mask64 = _mm_or_si128(mask64, _mm_slli_si128(mask64, 6));
  } else {
    mask64 = _mm_or_si128(mask64, _mm_slli_si128(mask64, 4));
    mask64 = _mm_or_si128(mask64, _mm_slli_si128(mask64, 8));
  }
  


  for (int y = 0; y < height; ++y) {
    __m128i row_ssd = _mm_setzero_si128();  // sum of squared differences (row_SSD)

    for (int x = 0; x < width; x+=increment*4) {
      __m128i src1 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(f1ptr+x));
      __m128i src2 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(f2ptr+x));

      src1 = _mm_and_si128(src1, mask64);
      src2 = _mm_and_si128(src2, mask64);

      __m128i diff_1_minus_2 = _mm_subs_epu8(src1, src2);
      __m128i diff_2_minus_1 = _mm_subs_epu8(src2, src1);

      positive_diff = _mm_max_epu8(positive_diff, diff_1_minus_2);
      negative_diff = _mm_max_epu8(negative_diff, diff_2_minus_1);

      __m128i absdiff1 = _mm_sad_epu8(diff_1_minus_2, zero);
      __m128i absdiff2 = _mm_sad_epu8(diff_2_minus_1, zero);

      sad_vector = _mm_add_epi32(sad_vector, absdiff1);
      sad_vector = _mm_add_epi32(sad_vector, absdiff2);

      sd_vector = _mm_add_epi32(sd_vector, absdiff1);
      sd_vector = _mm_sub_epi32(sd_vector, absdiff2);

      __m128i ssd = _mm_or_si128(diff_1_minus_2, diff_2_minus_1);
      __m128i ssd_lo = _mm_unpacklo_epi8(ssd, zero);
      __m128i ssd_hi = _mm_unpackhi_epi8(ssd, zero);
      ssd_lo   = _mm_madd_epi16(ssd_lo, ssd_lo);
      ssd_hi   = _mm_madd_epi16(ssd_hi, ssd_hi);
      row_ssd = _mm_add_epi32(row_ssd, ssd_lo);
      row_ssd = _mm_add_epi32(row_ssd, ssd_hi);
    }

    f1ptr += pitch1;
    f2ptr += pitch2;

    __m128i tmp = _mm_srli_si128(row_ssd, 8);
    row_ssd = _mm_add_epi32(row_ssd, tmp);
    tmp = _mm_srli_si128(row_ssd, 4);
    row_ssd = _mm_add_epi32(row_ssd, tmp);

    issd += _mm_cvtsi128_si32(row_ssd);
  }

  SAD_sum += _mm_cvtsi128_si32(sad_vector);
  SAD_sum += _mm_cvtsi128_si32(_mm_srli_si128(sad_vector, 8));
  SD_sum  += _mm_cvtsi128_si32(sd_vector);
  SD_sum += _mm_cvtsi128_si32(_mm_srli_si128(sd_vector, 8));

  BYTE posdiff_tmp[16];
  BYTE negdiff_tmp[16];
  _mm_store_si128(reinterpret_cast<__m128i*>(posdiff_tmp), positive_diff);
  _mm_store_si128(reinterpret_cast<__m128i*>(negdiff_tmp), negative_diff);

  SSD_sum += (double)issd;
  for (int i = 0; i < increment*4; ++i) {
    pos_D = max(pos_D, (int)(posdiff_tmp[i]));
    neg_D = max(neg_D, (int)(negdiff_tmp[i]));
  }

  neg_D = -neg_D;
}

#ifdef X86_32

static void compare_isse(DWORD mask, int increment,
                         const BYTE * f1ptr, int pitch1, 
                         const BYTE * f2ptr, int pitch2,
                         int width, int height,
                         int &SAD_sum, int &SD_sum, int &pos_D,  int &neg_D, double &SSD_sum)
{ 
  // rowsize multiple of 8 for YUV Planar, RGB32 and YUY2; 6 for RGB24
  // increment must be 3 for RGB24 and 4 for others

  __int64 issd = 0;
  __m64 sad_vector = _mm_setzero_si64(); //sum of absolute differences
  __m64 sd_vector = _mm_setzero_si64(); // sum of differences
  __m64 positive_diff = _mm_setzero_si64();
  __m64 negative_diff = _mm_setzero_si64();
  __m64 zero = _mm_setzero_si64();

  __m64 mask64 = _mm_set_pi32(0, mask);
  mask64 = _mm_or_si64(mask64, _mm_slli_si64(mask64, increment*8));


  for (int y = 0; y < height; ++y) {
    __m64 row_ssd = _mm_setzero_si64();  // sum of squared differences (row_SSD)

    for (int x = 0; x < width; x+=increment*2) {
      __m64 src1 = *reinterpret_cast<const __m64*>(f1ptr+x);
      __m64 src2 = *reinterpret_cast<const __m64*>(f2ptr+x);

      src1 = _mm_and_si64(src1, mask64);
      src2 = _mm_and_si64(src2, mask64);

      __m64 diff_1_minus_2 = _mm_subs_pu8(src1, src2);
      __m64 diff_2_minus_1 = _mm_subs_pu8(src2, src1);

      positive_diff = _mm_max_pu8(positive_diff, diff_1_minus_2);
      negative_diff = _mm_max_pu8(negative_diff, diff_2_minus_1);

      __m64 absdiff1 = _mm_sad_pu8(diff_1_minus_2, zero);
      __m64 absdiff2 = _mm_sad_pu8(diff_2_minus_1, zero);

      sad_vector = _mm_add_pi32(sad_vector, absdiff1);
      sad_vector = _mm_add_pi32(sad_vector, absdiff2);

      sd_vector = _mm_add_pi32(sd_vector, absdiff1);
      sd_vector = _mm_sub_pi32(sd_vector, absdiff2);

      __m64 ssd = _mm_or_si64(diff_1_minus_2, diff_2_minus_1);
      __m64 ssd_lo = _mm_unpacklo_pi8(ssd, zero);
      __m64 ssd_hi = _mm_unpackhi_pi8(ssd, zero);
      ssd_lo   = _mm_madd_pi16(ssd_lo, ssd_lo);
      ssd_hi   = _mm_madd_pi16(ssd_hi, ssd_hi);
      row_ssd = _mm_add_pi32(row_ssd, ssd_lo);
      row_ssd = _mm_add_pi32(row_ssd, ssd_hi);
    }

    f1ptr += pitch1;
    f2ptr += pitch2;

    __m64 tmp = _mm_unpackhi_pi32(row_ssd, zero);
    row_ssd = _mm_add_pi32(row_ssd, tmp);

    issd += _mm_cvtsi64_si32(row_ssd);
  }

  SAD_sum += _mm_cvtsi64_si32(sad_vector);
  SD_sum  += _mm_cvtsi64_si32(sd_vector);

  BYTE posdiff_tmp[8];
  BYTE negdiff_tmp[8];
  *reinterpret_cast<__m64*>(posdiff_tmp) = positive_diff;
  *reinterpret_cast<__m64*>(negdiff_tmp) = negative_diff;
  _mm_empty();

  SSD_sum += (double)issd;
  for (int i = 0; i < increment*2; ++i) {
    pos_D = max(pos_D, (int)(posdiff_tmp[i]));
    neg_D = max(neg_D, (int)(negdiff_tmp[i]));
  }

  neg_D = -neg_D;
}

#endif


PVideoFrame __stdcall Compare::GetFrame(int n, IScriptEnvironment* env)
{
  PVideoFrame f1 = child->GetFrame(n, env);
  PVideoFrame f2 = child2->GetFrame(n, env);

  int SD = 0;
  int SAD = 0;
  int pos_D = 0;
  int neg_D = 0;
  double SSD = 0;
  int row_SSD;

  int bytecount = 0;
  const int incr = vi.IsRGB24() ? 3 : 4;

  if (vi.IsRGB24() || vi.IsYUY2() || vi.IsRGB32()) {

    const BYTE* f1ptr = f1->GetReadPtr();
    const BYTE* f2ptr = f2->GetReadPtr();
    const int pitch1 = f1->GetPitch();
    const int pitch2 = f2->GetPitch();
    const int rowsize = f1->GetRowSize();
    const int height = f1->GetHeight();

    bytecount = rowsize * height * masked_bytes / 4;

    if (((vi.IsRGB32() && (rowsize % 16 == 0)) || (vi.IsRGB24() && (rowsize % 12 == 0)) || (vi.IsYUY2() && (rowsize % 16 == 0))) &&
      (env->GetCPUFlags() & CPUF_SSE2))
    {
      compare_sse2(mask, incr, f1ptr, pitch1, f2ptr, pitch2, rowsize, height, SAD, SD, pos_D, neg_D, SSD);
    }
    else
#ifdef X86_32
    if (((vi.IsRGB32() && (rowsize % 8 == 0)) || (vi.IsRGB24() && (rowsize % 6 == 0)) || (vi.IsYUY2() && (rowsize % 8 == 0))) &&
      (env->GetCPUFlags() & CPUF_INTEGER_SSE))
    {
      compare_isse(mask, incr, f1ptr, pitch1, f2ptr, pitch2, rowsize, height, SAD, SD, pos_D, neg_D, SSD);
    }
    else
#endif
    {
      for (int y = 0; y < height; y++) {
        row_SSD = 0;
        for (int x = 0; x < rowsize; x += incr) {
          DWORD p1 = *(DWORD *)(f1ptr + x) & mask;
          DWORD p2 = *(DWORD *)(f2ptr + x) & mask;
          int d0 = (p1 & 0xff) - (p2 & 0xff);
          int d1 = ((p1 >> 8) & 0xff) - ((p2 & 0xff00) >> 8);
          int d2 = ((p1 >>16) & 0xff) - ((p2 & 0xff0000) >> 16);
          int d3 = (p1 >> 24) - (p2 >> 24);
          SD += d0 + d1 + d2 + d3;
          SAD += abs(d0) + abs(d1) + abs(d2) + abs(d3);
          row_SSD += d0 * d0 + d1 * d1 + d2 * d2 + d3 * d3;
          pos_D = max(max(max(max(pos_D,d0),d1),d2),d3);
          neg_D = min(min(min(min(neg_D,d0),d1),d2),d3);
        }
        SSD += row_SSD;
        f1ptr += pitch1;
        f2ptr += pitch2;
      }
    }
  }
  else { // Planar
  
    int planes[3] = {PLANAR_Y, PLANAR_U, PLANAR_V};
    for (int p=0; p<3; p++) {
      const int plane = planes[p];

	  if (planar_plane & plane) {

        const BYTE* f1ptr = f1->GetReadPtr(plane);
        const BYTE* f2ptr = f2->GetReadPtr(plane);
        const int pitch1 = f1->GetPitch(plane);
        const int pitch2 = f2->GetPitch(plane);
        const int rowsize = f1->GetRowSize(plane);
        const int height = f1->GetHeight(plane);

        bytecount += rowsize * height;

        if ((rowsize % 16 == 0) && (env->GetCPUFlags() & CPUF_SSE2)) 
        {
          compare_sse2(mask, incr, f1ptr, pitch1, f2ptr, pitch2, rowsize, height, SAD, SD, pos_D, neg_D, SSD);
        }
        else
#ifdef X86_32
        if ((rowsize % 8 == 0) && (env->GetCPUFlags() & CPUF_INTEGER_SSE)) 
        {
         compare_isse(mask, incr, f1ptr, pitch1, f2ptr, pitch2, rowsize, height, SAD, SD, pos_D, neg_D, SSD);
        }
        else
#endif
        {
          // rowsize must be a multiple 8 to use the ISSE routine
          for (int y = 0; y < height; y++) {
            row_SSD = 0;
            for (int x = 0; x < rowsize; x += 1) {
              int p1 = *(f1ptr + x);
              int p2 = *(f2ptr + x);
              int d0 = p1 - p2;
              SD += d0;
              SAD += abs(d0);
              row_SSD += d0 * d0;
              pos_D = max(pos_D,d0);
              neg_D = min(neg_D,d0);
            }
            SSD += row_SSD;
            f1ptr += pitch1;
            f2ptr += pitch2;
          }
        }
      }
    }
  }

  double MAD = (double)SAD / bytecount;
  double MD = (double)SD / bytecount;
  if (SSD == 0.0) SSD = 1.0;
  double PSNR = 10.0 * log10(bytecount * 255.0 * 255.0 / SSD);

  framecount++;
  if (framecount == 1) {
    MAD_min = MAD_tot = MAD_max = MAD;
    MD_min = MD_tot = MD_max = MD;
    PSNR_min = PSNR_tot = PSNR_max = PSNR;
    bytecount_overall = double(bytecount);
    SSD_overall = SSD;
  } else {
    MAD_min = min(MAD_min, MAD);
    MAD_tot += MAD;
    MAD_max = max(MAD_max, MAD);
    MD_min = min(MD_min, MD);
    MD_tot += MD;
    MD_max = max(MD_max, MD);
    PSNR_min = min(PSNR_min, PSNR);
    PSNR_tot += PSNR;
    PSNR_max = max(PSNR_max, PSNR);
    bytecount_overall += double(bytecount);
    SSD_overall += SSD;  
  }

  if (log)
    fprintf(log,"%6u  %8.4f  %+9.4f  %3d    %3d    %8.4f\n", (unsigned int)n, MAD, MD, pos_D, neg_D, PSNR);
  else {
    env->MakeWritable(&f1);
    BYTE* dstp = f1->GetWritePtr();
    int dst_pitch = f1->GetPitch();

    HDC hdc = antialiaser.GetDC();
	if (hdc) {
	  char text[400];
	  RECT r= { 32, 16, min(3440,vi.width*8), 768+128 };
	  double PSNR_overall = 10.0 * log10(bytecount_overall * 255.0 * 255.0 / SSD_overall);
	  _snprintf(text, sizeof(text), 
		"       Frame:  %-8u(   min  /   avg  /   max  )\n"
		"Mean Abs Dev:%8.4f  (%7.3f /%7.3f /%7.3f )\n"
		"    Mean Dev:%+8.4f  (%+7.3f /%+7.3f /%+7.3f )\n"
		" Max Pos Dev:%4d  \n"
		" Max Neg Dev:%4d  \n"
		"        PSNR:%6.2f dB ( %6.2f / %6.2f / %6.2f )\n"
		"Overall PSNR:%6.2f dB\n", 
		n,
		MAD, MAD_min, MAD_tot / framecount, MD_max,
		MD, MD_min, MD_tot / framecount, MD_max,
		pos_D,
		neg_D,
		PSNR, PSNR_min, PSNR_tot / framecount, PSNR_max,
		PSNR_overall
	  );
	  DrawText(hdc, text, -1, &r, 0);
	  GdiFlush();

	  antialiaser.Apply( vi, &f1, dst_pitch );
	}

    if (show_graph) {
      // original idea by Marc_FD
      psnrs[n] = min((int)(PSNR + 0.5), 100);
      if (vi.height > 196) {
        if (vi.IsYUY2()) {
          dstp += (vi.height - 1) * dst_pitch;
          for (int y = 0; y <= 100; y++) {            
            for (int x = max(0, vi.width - n - 1); x < vi.width; x++) {
              if (y <= psnrs[n - vi.width + 1 + x]) {
                if (y <= psnrs[n - vi.width + 1 + x] - 2) {
                  dstp[x << 1] = 16;                // Y
                  dstp[((x & -1) << 1) + 1] = 0x80; // U
                  dstp[((x & -1) << 1) + 3] = 0x80; // V
                } else {
                  dstp[x << 1] = 235;               // Y
                  dstp[((x & -1) << 1) + 1] = 0x80; // U
                  dstp[((x & -1) << 1) + 3] = 0x80; // V
                }
              }
            } // for x
            dstp -= dst_pitch;
          } // for y
        }
		else if (vi.IsPlanar()) {
          dstp += (vi.height - 1) * dst_pitch;
          for (int y = 0; y <= 100; y++) {            
            for (int x = max(0, vi.width - n - 1); x < vi.width; x++) {
              if (y <= psnrs[n - vi.width + 1 + x]) {
                if (y <= psnrs[n - vi.width + 1 + x] - 2) {
                  dstp[x] = 16;                // Y
                } else {
                  dstp[x] = 235;               // Y
                }
              }
            } // for x
            dstp -= dst_pitch;
          } // for y
        } else {  // RGB
          for (int y = 0; y <= 100; y++) {
            for (int x = max(0, vi.width - n - 1); x < vi.width; x++) {
              if (y <= psnrs[n - vi.width + 1 + x]) {
                const int xx = x * incr;
                if (y <= psnrs[n - vi.width + 1 + x] -2) {
                  dstp[xx] = 0x00;        // B
                  dstp[xx + 1] = 0x00;    // G
                  dstp[xx + 2] = 0x00;    // R
                } else {
                  dstp[xx] = 0xFF;        // B
                  dstp[xx + 1] = 0xFF;    // G
                  dstp[xx + 2] = 0xFF;    // R
                }
              }
            } // for x
            dstp += dst_pitch;
          } // for y
        } // RGB
      } // height > 100
    } // show_graph
  } // no logfile

  return f1;
}











/************************************
 *******   Helper Functions    ******
 ***********************************/

bool GetTextBoundingBox( const char* text, const char* fontname, int size, bool bold, 
                         bool italic, int align, int* width, int* height )
{
  HFONT hfont = LoadFont(fontname, size, bold, italic);
  if (hfont == NULL)
    return false;
  HDC hdc = GetDC(NULL);
  if (hdc == NULL)
	return false;
  HFONT hfontDefault = (HFONT)SelectObject(hdc, hfont);
  int old_map_mode = SetMapMode(hdc, MM_TEXT);
  UINT old_text_align = SetTextAlign(hdc, align);

  *height = *width = 8;
  bool success = true;
  RECT r = { 0, 0, 0, 0 };
  for (;;) {
    const char* nl = strchr(text, '\n');
    if (nl-text) {
      success &= !!DrawText(hdc, text, nl ? nl-text : (int)lstrlen(text), &r, DT_CALCRECT | DT_NOPREFIX);
      *width = max(*width, int(r.right+8));
    }
    *height += r.bottom;
    if (nl) {
      text = nl+1;
      if (*text)
        continue;
      else
        break;
    } else {
      break;
    }
  }

  SetTextAlign(hdc, old_text_align);
  SetMapMode(hdc, old_map_mode);
  SelectObject(hdc, hfontDefault);
  DeleteObject(hfont);
  ReleaseDC(NULL, hdc);

  return success;
}


void ApplyMessage( PVideoFrame* frame, const VideoInfo& vi, const char* message, int size, 
                   int textcolor, int halocolor, int bgcolor, IScriptEnvironment* env ) 
{
  if (vi.IsYUV()) {
    textcolor = RGB2YUV(textcolor);
    halocolor = RGB2YUV(halocolor);
  }
  Antialiaser antialiaser(vi.width, vi.height, "Arial", size, textcolor, halocolor);
  HDC hdcAntialias = antialiaser.GetDC();
  if  (hdcAntialias)
  {
	RECT r = { 4*8, 4*8, vi.width*8, vi.height*8 };
	DrawText(hdcAntialias, message, lstrlen(message), &r, DT_NOPREFIX|DT_CENTER);
	GdiFlush();
	antialiaser.Apply(vi, frame, (*frame)->GetPitch());
  }
}

