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

// Overlay (c) 2003, 2004 by Klaus Post

#include "stdafx.h"

#include "444convert.h"


void Convert444FromYV12::ConvertImage(PVideoFrame src, Image444* dst, IScriptEnvironment* env) {
  env->BitBlt(dst->GetPtr(PLANAR_Y), dst->pitch, 
    src->GetReadPtr(PLANAR_Y),src->GetPitch(PLANAR_Y), src->GetRowSize(PLANAR_Y), src->GetHeight());

  const BYTE* srcU = src->GetReadPtr(PLANAR_U);
  const BYTE* srcV = src->GetReadPtr(PLANAR_V);

  int srcUVpitch = src->GetPitch(PLANAR_U);

  BYTE* dstU = dst->GetPtr(PLANAR_U);
  BYTE* dstV = dst->GetPtr(PLANAR_V);

  int dstUVpitch = dst->pitch;

  int w = src->GetRowSize(PLANAR_U);
  int h = src->GetHeight(PLANAR_U);

  for (int y=0; y<h-1; y++) {
    BYTE u = srcU[0];
    BYTE v = srcV[0];
    for (int x=0; x<w-1; x++) {
      int x2 = x<<1;
      dstU[x2] = u;
      dstV[x2] = v;
      dstU[x2+dstUVpitch] = u;
      dstV[x2+dstUVpitch] = v;
      u = dstU[x2+1] = srcU[x+1];
      v = dstV[x2+1] = srcV[x+1];
      dstU[x2+dstUVpitch+1] = u;
      dstV[x2+dstUVpitch+1] = v;
    }
    dstU[w*2-2] = dstU[w*2-1] = dstU[w*2-2+dstUVpitch] = dstU[w*2-1+dstUVpitch] = u;
    dstV[w*2-2] = dstV[w*2-1] = dstV[w*2-2+dstUVpitch] = dstV[w*2-1+dstUVpitch] = v;
    srcU+=srcUVpitch;
    srcV+=srcUVpitch;
    dstU+=dstUVpitch*2;
    dstV+=dstUVpitch*2;
  }
  for (int x=0;x<(w-1); x++) {  // Last line - point upsize
    int x2 = x<<1;
    BYTE u = dstU[x2] = dstU[x2+dstUVpitch] = srcU[x];
    BYTE v = dstV[x2] = dstV[x2+dstUVpitch] = srcV[x];
    dstU[x2+1] = dstU[x2+dstUVpitch+1] = u;
    dstV[x2+1] = dstV[x2+dstUVpitch+1] = v;
  }
  dstU[w*2-2] = dstU[w*2-1] = dstU[w*2-2+dstUVpitch] = dstU[w*2-1+dstUVpitch] = srcU[w-1];
  dstV[w*2-2] = dstV[w*2-1] = dstU[w*2-2+dstUVpitch] = dstU[w*2-1+dstUVpitch] = srcV[w-1];
}


void Convert444FromYV12::ConvertImageLumaOnly(PVideoFrame src, Image444* dst, IScriptEnvironment* env) {
  env->BitBlt(dst->GetPtr(PLANAR_Y), dst->pitch, 
    src->GetReadPtr(PLANAR_Y),src->GetPitch(PLANAR_Y), src->GetRowSize(PLANAR_Y), src->GetHeight());
}


void Convert444FromYUY2::ConvertImage(PVideoFrame src, Image444* dst, IScriptEnvironment* env) {

  const BYTE* srcP = src->GetReadPtr();
  int srcPitch = src->GetPitch();

  BYTE* dstY = dst->GetPtr(PLANAR_Y);
  BYTE* dstU = dst->GetPtr(PLANAR_U);
  BYTE* dstV = dst->GetPtr(PLANAR_V);

  int dstPitch = dst->pitch;

  int w = dst->w();
  int h = dst->h();

  for (int y=0; y<h; y++) {
    for (int x=0; x<w; x+=2) {
      int x2 = x<<1;
      dstY[x] = srcP[x2];
      dstU[x] = dstU[x+1] = srcP[x2+1];
      dstV[x] = dstV[x+1] = srcP[x2+3];
      dstY[x+1] = srcP[x2+2];
    }

    srcP+=srcPitch;

    dstY+=dstPitch;
    dstU+=dstPitch;
    dstV+=dstPitch;
  }
}

void Convert444FromYUY2::ConvertImageLumaOnly(PVideoFrame src, Image444* dst, IScriptEnvironment* env) {

  const BYTE* srcP = src->GetReadPtr();
  int srcPitch = src->GetPitch();

  BYTE* dstY = dst->GetPtr(PLANAR_Y);

  int dstPitch = dst->pitch;

  int w = dst->w();
  int h = dst->h();

  for (int y=0; y<h; y++) {
    for (int x=0; x<w; x+=2) {
      int x2 = x<<1;
      dstY[x] = srcP[x2];
      dstY[x+1] = srcP[x2+2];
    }

    srcP+=srcPitch;
    dstY+=dstPitch;
  }
}


PVideoFrame Convert444ToYV12::ConvertImage(Image444* src, PVideoFrame dst, IScriptEnvironment* env) {
  env->MakeWritable(&dst);

  env->BitBlt(dst->GetWritePtr(PLANAR_Y), dst->GetPitch(PLANAR_Y),
    src->GetPtr(PLANAR_Y), src->pitch, dst->GetRowSize(PLANAR_Y), dst->GetHeight());

  const BYTE* srcU = src->GetPtr(PLANAR_U);
  const BYTE* srcV = src->GetPtr(PLANAR_V);

  int srcUVpitch = src->pitch;

  BYTE* dstU = dst->GetWritePtr(PLANAR_U);
  BYTE* dstV = dst->GetWritePtr(PLANAR_V);

  int dstUVpitch = dst->GetPitch(PLANAR_U);

  int w = dst->GetRowSize(PLANAR_U);
  int h = dst->GetHeight(PLANAR_U);
  
  for (int y=0; y<h; y++) {
    for (int x=0; x<w; x++) {
      int x2 = x<<1;
      dstU[x] = (srcU[x2] + srcU[x2+1] + srcU[x2+srcUVpitch] + srcU[x2+srcUVpitch+1] + 2)>>2;
      dstV[x] = (srcV[x2] + srcV[x2+1] + srcV[x2+srcUVpitch] + srcV[x2+srcUVpitch+1] + 2)>>2;
    }
    srcU+=srcUVpitch*2;
    srcV+=srcUVpitch*2;
    dstU+=dstUVpitch;
    dstV+=dstUVpitch;
  }
  return dst;
}


PVideoFrame Convert444ToYUY2::ConvertImage(Image444* src, PVideoFrame dst, IScriptEnvironment* env) {
  env->MakeWritable(&dst);

  const BYTE* srcY = src->GetPtr(PLANAR_Y);
  const BYTE* srcU = src->GetPtr(PLANAR_U);
  const BYTE* srcV = src->GetPtr(PLANAR_V);

  int srcPitch = src->pitch;

  BYTE* dstP = dst->GetWritePtr();

  int dstPitch = dst->GetPitch();

  int w = src->w();
  int h = src->h();
  
  for (int y=0; y<h; y++) {
    for (int x=0; x<w; x+=2) {
      int x2 = x<<1;
      dstP[x2] = srcY[x];
      dstP[x2+1] = (srcU[x] + srcU[x+1] + 1)>>1;
      dstP[x2+2] = srcY[x+1];
      dstP[x2+3] = (srcV[x] + srcV[x+1] + 1)>>1;
    }
    srcY+=srcPitch;
    srcU+=srcPitch;
    srcV+=srcPitch;
    dstP+=dstPitch;
  }
  return dst;
}


void Convert444FromRGB::ConvertImageLumaOnly(PVideoFrame src, Image444* dst, IScriptEnvironment* env) {
  const BYTE* srcP = src->GetReadPtr();
  int srcPitch = src->GetPitch();

  BYTE* dstY = dst->GetPtr(PLANAR_Y);

  int dstPitch = dst->pitch;

  int w = dst->w();
  int h = dst->h();

  int bpp = src->GetRowSize()/w;
  srcP += h*srcPitch-srcPitch;

  for (int y=0; y<h; y++) {
    int RGBx = 0;
    for (int x=0; x<w; x++) {
      dstY[x] = srcP[RGBx];
      RGBx+=bpp;
    }
    srcP-=srcPitch;
    dstY+=dstPitch;
  }

}

void Convert444FromRGB::ConvertImage(PVideoFrame src, Image444* dst, IScriptEnvironment* env) {
  const int cyb = int(0.114*219/255*65536+0.5);
  const int cyg = int(0.587*219/255*65536+0.5);
  const int cyr = int(0.299*219/255*65536+0.5);

  const BYTE* srcP = src->GetReadPtr();
  int srcPitch = src->GetPitch();

  BYTE* dstY = dst->GetPtr(PLANAR_Y);
  BYTE* dstU = dst->GetPtr(PLANAR_U);
  BYTE* dstV = dst->GetPtr(PLANAR_V);

  int dstPitch = dst->pitch;

  int w = dst->w();
  int h = dst->h();

  int bpp = src->GetRowSize()/w;
  srcP += h*srcPitch-srcPitch;

  for (int y=0; y<h; y++) {
    int RGBx = 0;
    for (int x=0; x<w; x++) {
      int b = srcP[RGBx];
      int g = srcP[RGBx+1];
      int r = srcP[RGBx+2];

      int y = (cyb*b + cyg*g + cyr*r + 0x108000) >> 16;
      int scaled_y = (y - 16) * int(255.0/219.0*65536+0.5);
      int b_y = (b << 16) - scaled_y;
      int r_y = (r << 16) - scaled_y;

      dstY[x] = y;
      dstU[x] = ((b_y >> 10) * int(1/2.018*1024+0.5) + 0x800000)>>16;
      dstV[x] = ((r_y >> 10) * int(1/1.596*1024+0.5) + 0x800000)>>16;

      RGBx+=bpp;
    }

    srcP-=srcPitch;

    dstY+=dstPitch;
    dstU+=dstPitch;
    dstV+=dstPitch;
  }
}