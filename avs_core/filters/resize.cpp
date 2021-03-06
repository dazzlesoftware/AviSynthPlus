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


#include "resize.h"
#include "../core/internal.h"
#include <emmintrin.h>
#include <avs/alignment.h>



/********************************************************************
***** Declare index of new filters for Avisynth's filter engine *****
********************************************************************/

extern const AVSFunction Resize_filters[] = {
  { "VerticalReduceBy2",   BUILTIN_FUNC_PREFIX, "c", VerticalReduceBy2::Create },        // src clip
  { "HorizontalReduceBy2", BUILTIN_FUNC_PREFIX, "c", HorizontalReduceBy2::Create },    // src clip
  { "ReduceBy2",           BUILTIN_FUNC_PREFIX, "c", Create_ReduceBy2 },                         // src clip
  { 0 }
};

//todo: think of a way to do this with pavgb
static __forceinline __m128i vertical_reduce_sse2_blend(__m128i &src, __m128i &src_next, __m128i &src_next2, __m128i &zero, __m128i &two) {
  __m128i src_unpck_lo = _mm_unpacklo_epi8(src, zero);
  __m128i src_unpck_hi = _mm_unpackhi_epi8(src, zero);

  __m128i src_next_unpck_lo = _mm_unpacklo_epi8(src_next, zero);
  __m128i src_next_unpck_hi = _mm_unpackhi_epi8(src_next, zero);

  __m128i src_next2_unpck_lo = _mm_unpacklo_epi8(src_next2, zero);
  __m128i src_next2_unpck_hi = _mm_unpackhi_epi8(src_next2, zero);

  __m128i acc_lo = _mm_adds_epu16(src_next_unpck_lo, src_next_unpck_lo);
  acc_lo = _mm_adds_epu16(acc_lo, src_unpck_lo);
  acc_lo = _mm_adds_epu16(acc_lo, src_next2_unpck_lo);

  __m128i acc_hi = _mm_adds_epu16(src_next_unpck_hi, src_next_unpck_hi);
  acc_hi = _mm_adds_epu16(acc_hi, src_unpck_hi);
  acc_hi = _mm_adds_epu16(acc_hi, src_next2_unpck_hi);

  acc_lo = _mm_adds_epu16(acc_lo, two);
  acc_hi = _mm_adds_epu16(acc_hi, two);

  acc_lo = _mm_srai_epi16(acc_lo, 2);
  acc_hi = _mm_srai_epi16(acc_hi, 2);

  return _mm_packus_epi16(acc_lo, acc_hi);
}

static void vertical_reduce_sse2(BYTE* dstp, const BYTE* srcp, int dst_pitch, int src_pitch, size_t width, size_t height) {
  const BYTE* srcp_next = srcp + src_pitch;
  const BYTE* srcp_next2 = srcp + src_pitch*2;
  __m128i zero = _mm_setzero_si128();
  __m128i two = _mm_set1_epi16(2);

  for (size_t y = 0; y < height-1; ++y) {
    for (size_t x = 0; x < width; x+=16) {
      __m128i src = _mm_load_si128(reinterpret_cast<const __m128i*>(srcp+x));
      __m128i src_next = _mm_load_si128(reinterpret_cast<const __m128i*>(srcp_next+x));
      __m128i src_next2 = _mm_load_si128(reinterpret_cast<const __m128i*>(srcp_next2+x));
     
      __m128i avg = vertical_reduce_sse2_blend(src, src_next, src_next2, zero, two);

      _mm_store_si128(reinterpret_cast<__m128i*>(dstp+x), avg);
    }
    
    dstp += dst_pitch;
    srcp += src_pitch*2;
    srcp_next += src_pitch*2;
    srcp_next2 += src_pitch*2;
  }
  //last line
  for (size_t x = 0; x < width; x+=16) {
    __m128i src = _mm_load_si128(reinterpret_cast<const __m128i*>(srcp+x));
    __m128i src_next = _mm_load_si128(reinterpret_cast<const __m128i*>(srcp_next+x));

    __m128i avg = vertical_reduce_sse2_blend(src, src_next, src_next, zero, two);

    _mm_store_si128(reinterpret_cast<__m128i*>(dstp+x), avg);
  }
}

#ifdef X86_32

//todo: think of a way to do this with pavgb
static __forceinline __m64 vertical_reduce_mmx_blend(__m64 &src, __m64 &src_next, __m64 &src_next2, __m64 &zero, __m64 &two) {
  __m64 src_unpck_lo = _mm_unpacklo_pi8(src, zero);
  __m64 src_unpck_hi = _mm_unpackhi_pi8(src, zero);

  __m64 src_next_unpck_lo = _mm_unpacklo_pi8(src_next, zero);
  __m64 src_next_unpck_hi = _mm_unpackhi_pi8(src_next, zero);

  __m64 src_next2_unpck_lo = _mm_unpacklo_pi8(src_next2, zero);
  __m64 src_next2_unpck_hi = _mm_unpackhi_pi8(src_next2, zero);

  __m64 acc_lo = _mm_adds_pu16(src_next_unpck_lo, src_next_unpck_lo);
  acc_lo = _mm_adds_pu16(acc_lo, src_unpck_lo);
  acc_lo = _mm_adds_pu16(acc_lo, src_next2_unpck_lo);

  __m64 acc_hi = _mm_adds_pu16(src_next_unpck_hi, src_next_unpck_hi);
  acc_hi = _mm_adds_pu16(acc_hi, src_unpck_hi);
  acc_hi = _mm_adds_pu16(acc_hi, src_next2_unpck_hi);

  acc_lo = _mm_adds_pu16(acc_lo, two);
  acc_hi = _mm_adds_pu16(acc_hi, two);

  acc_lo = _mm_srai_pi16(acc_lo, 2);
  acc_hi = _mm_srai_pi16(acc_hi, 2);

  return _mm_packs_pu16(acc_lo, acc_hi);
}

static void vertical_reduce_mmx(BYTE* dstp, const BYTE* srcp, int dst_pitch, int src_pitch, size_t width, size_t height) {
  const BYTE* srcp_next = srcp + src_pitch;
  const BYTE* srcp_next2 = srcp + src_pitch*2;
  __m64 zero = _mm_setzero_si64();
  __m64 two = _mm_set1_pi16(2);

  size_t mod8_width = width / 8 * 8;

  for (size_t y = 0; y < height-1; ++y) {
    for (size_t x = 0; x < mod8_width; x+=8) {
      __m64 src = *reinterpret_cast<const __m64*>(srcp+x);
      __m64 src_next = *reinterpret_cast<const __m64*>(srcp_next+x);
      __m64 src_next2 = *reinterpret_cast<const __m64*>(srcp_next2+x);

      __m64 avg = vertical_reduce_mmx_blend(src, src_next, src_next2, zero, two);

      *reinterpret_cast<__m64*>(dstp+x) = avg;
    }

    if (mod8_width != width) {
      size_t x = width - 8;
      __m64 src = *reinterpret_cast<const __m64*>(srcp+x);
      __m64 src_next = *reinterpret_cast<const __m64*>(srcp_next+x);
      __m64 src_next2 = *reinterpret_cast<const __m64*>(srcp_next2+x);

      __m64 avg = vertical_reduce_mmx_blend(src, src_next, src_next2, zero, two);

      *reinterpret_cast<__m64*>(dstp+x) = avg;
    }

    dstp += dst_pitch;
    srcp += src_pitch*2;
    srcp_next += src_pitch*2;
    srcp_next2 += src_pitch*2;
  }
  //last line
  for (size_t x = 0; x < mod8_width; x+=8) {
    __m64 src = *reinterpret_cast<const __m64*>(srcp+x);
    __m64 src_next = *reinterpret_cast<const __m64*>(srcp_next+x);

    __m64 avg = vertical_reduce_mmx_blend(src, src_next, src_next, zero, two);

    *reinterpret_cast<__m64*>(dstp+x)= avg;
  }

  if (mod8_width != width) {
    size_t x = width - 8;
    __m64 src = *reinterpret_cast<const __m64*>(srcp+x);
    __m64 src_next = *reinterpret_cast<const __m64*>(srcp_next+x);

    __m64 avg = vertical_reduce_mmx_blend(src, src_next, src_next, zero, two);

    *reinterpret_cast<__m64*>(dstp+x)= avg;
  }

  _mm_empty();
}
#endif

static void vertical_reduce_c(BYTE* dstp, const BYTE* srcp, int dst_pitch, int src_pitch, size_t width, size_t height) {
  const BYTE* srcp_next = srcp + src_pitch;
  const BYTE* srcp_next2 = srcp + src_pitch*2;

  for (size_t y = 0; y < height-1; ++y) {
    for (size_t x = 0; x < width; ++x) {
      dstp[x] = (srcp[x] + 2*srcp_next[x] + srcp_next2[x] + 2) >> 2;
    }
    dstp += dst_pitch;
    srcp += src_pitch*2;
    srcp_next += src_pitch*2;
    srcp_next2 += src_pitch*2;
  }
  for(size_t x = 0; x < width; ++x) {
    dstp[x] = (srcp[x] + 3*srcp_next[x] + 2) >> 2;
  }
}

void vertical_reduce_core(BYTE* dstp, const BYTE* srcp, int dst_pitch, int src_pitch, int width, int height, IScriptEnvironment* env) {
  if (!srcp) {
    return;
  }
  if ((env->GetCPUFlags() & CPUF_SSE2) && IsPtrAligned(srcp, 16) && width >= 16) {
    vertical_reduce_sse2(dstp, srcp, dst_pitch, src_pitch, width, height);
  } else
#ifdef X86_32
    if ((env->GetCPUFlags() & CPUF_MMX) && width >= 8) {
      vertical_reduce_mmx(dstp, srcp, dst_pitch, src_pitch, width, height);
    } else
#endif
    vertical_reduce_c(dstp, srcp, dst_pitch, src_pitch, width, height);
}


/*************************************
 ******* Vertical 2:1 Reduction ******
 ************************************/


VerticalReduceBy2::VerticalReduceBy2(PClip _child, IScriptEnvironment* env)
 : GenericVideoFilter(_child)
{
  if (vi.IsPlanar() && !vi.IsY8()) {
    const int mod  = 2 << vi.GetPlaneHeightSubsampling(PLANAR_U);
    const int mask = mod - 1;
    if (vi.height & mask)
      env->ThrowError("VerticalReduceBy2: Planar source height must be divisible by %d.", mod);    
  }

  if (vi.height & 1)
    env->ThrowError("VerticalReduceBy2: Image height must be even");

  original_height = vi.height;
  vi.height >>= 1;

  if (vi.height<3) {
    env->ThrowError("VerticalReduceBy2: Image too small to be reduced by 2.");    
  }
}


PVideoFrame VerticalReduceBy2::GetFrame(int n, IScriptEnvironment* env) {
  PVideoFrame src = child->GetFrame(n, env);
  PVideoFrame dst = env->NewVideoFrame(vi);
  int src_pitch = src->GetPitch();
  int dst_pitch = dst->GetPitch();
  int row_size = src->GetRowSize();
  int a_row_size = (row_size+3) & ~3;
  BYTE* dstp = dst->GetWritePtr();
  const BYTE* srcp = src->GetReadPtr();

  if (vi.IsPlanar()) {
    vertical_reduce_core(dstp, srcp, dst_pitch, src_pitch, row_size, dst->GetHeight(PLANAR_Y), env);
    if (!vi.IsY8()) {
      vertical_reduce_core(dst->GetWritePtr(PLANAR_U), src->GetReadPtr(PLANAR_U), dst->GetPitch(PLANAR_U),
        src->GetPitch(PLANAR_U), dst->GetRowSize(PLANAR_U), dst->GetHeight(PLANAR_U), env);
      vertical_reduce_core(dst->GetWritePtr(PLANAR_V), src->GetReadPtr(PLANAR_V), dst->GetPitch(PLANAR_V),
        src->GetPitch(PLANAR_V), dst->GetRowSize(PLANAR_V), dst->GetHeight(PLANAR_V), env);
    }
  } else {
    vertical_reduce_core(dstp, srcp, dst_pitch, src_pitch, row_size, vi.height, env);
  }
  return dst;
}


/************************************
 **** Horizontal 2:1 Reduction ******
 ***********************************/

HorizontalReduceBy2::HorizontalReduceBy2(PClip _child, IScriptEnvironment* env)
: GenericVideoFilter(_child), mybuffer(0)
{
  if (vi.IsPlanar() && !vi.IsY8()) {
    const int mod  = 2 << vi.GetPlaneWidthSubsampling(PLANAR_U);
    const int mask = mod - 1;
    if (vi.width & mask)
      env->ThrowError("HorizontalReduceBy2: Planar source width must be divisible by %d.", mod);    
  }

  if (vi.width & 1)
    env->ThrowError("HorizontalReduceBy2: Image width must be even");

  if (vi.IsYUY2() && (vi.width & 3))
    env->ThrowError("HorizontalReduceBy2: YUY2 output image width must be even");

  source_width = vi.width;
  vi.width >>= 1;
}
 

PVideoFrame HorizontalReduceBy2::GetFrame(int n, IScriptEnvironment* env)
{
  PVideoFrame src = child->GetFrame(n, env);
  PVideoFrame dst = env->NewVideoFrame(vi);
  int src_gap = src->GetPitch() - src->GetRowSize();  //aka 'modulo' in VDub filter terminology
  int dst_gap = dst->GetPitch() - dst->GetRowSize();
  const int dst_pitch = dst->GetPitch();

  BYTE* dstp = dst->GetWritePtr();

  if (vi.IsPlanar()) {
    const BYTE* srcp = src->GetReadPtr(PLANAR_Y);
    int yloops = dst->GetHeight(PLANAR_Y);
    int xloops = dst->GetRowSize(PLANAR_Y)-1;
    for (int y = 0; y<yloops; y++) {
      for (int x = 0; x<xloops; x++) {
        *dstp = (srcp[0] + 2*srcp[1] + srcp[2] + 2) >> 2;
        dstp++;
        srcp += 2;
      }
      *dstp = (srcp[0] + srcp[1] +1) >> 1;
      dstp += dst_gap+1;
      srcp += src_gap+2;
    }
    srcp = src->GetReadPtr(PLANAR_U);
    dstp = dst->GetWritePtr(PLANAR_U);
    src_gap = src->GetPitch(PLANAR_U) - src->GetRowSize(PLANAR_U);
    dst_gap = dst->GetPitch(PLANAR_U) - dst->GetRowSize(PLANAR_U);
    yloops = dst->GetHeight(PLANAR_U);
    xloops = dst->GetRowSize(PLANAR_U)-1;
    for (int y = 0; y<yloops; y++) {
      for (int x = 0; x<xloops; x++) {
        dstp[0] = (srcp[0] + 2*srcp[1] + srcp[2] + 2) >> 2;
        dstp++;
        srcp += 2;
      }
      dstp[0] = (srcp[0] + srcp[1] +1) >> 1;
      dstp += dst_gap+1;
      srcp += src_gap+2;
    }
    srcp = src->GetReadPtr(PLANAR_V);
    dstp = dst->GetWritePtr(PLANAR_V);
    for (int y = 0; y<yloops; y++) {
      for (int x = 0; x<xloops; x++) {
        dstp[0] = (srcp[0] + 2*srcp[1] + srcp[2] + 2) >> 2;
        dstp++;
        srcp += 2;
      }
      dstp[0] = (srcp[0] + srcp[1] +1) >> 1;
      dstp += dst_gap+1;
      srcp += src_gap+2;
    }
  } else if (vi.IsYUY2()  && (!(vi.width&3))) {

    const BYTE* srcp = src->GetReadPtr();
    for (int y = vi.height; y>0; --y) {
      for (int x = (vi.width>>1)-1; x; --x) {
        dstp[0] = (srcp[0] + 2*srcp[2] + srcp[4] + 2) >> 2;
        dstp[1] = (srcp[1] + 2*srcp[5] + srcp[9] + 2) >> 2;
        dstp[2] = (srcp[4] + 2*srcp[6] + srcp[8] + 2) >> 2;
        dstp[3] = (srcp[3] + 2*srcp[7] + srcp[11] + 2) >> 2;
        dstp += 4;
        srcp += 8;
      }
      dstp[0] = (srcp[0] + 2*srcp[2] + srcp[4] + 2) >> 2;
      dstp[1] = (srcp[1] + srcp[5] + 1) >> 1;
      dstp[2] = (srcp[4] + srcp[6] + 1) >> 1;
      dstp[3] = (srcp[3] + srcp[7] + 1) >> 1;
      dstp += dst_gap+4;
      srcp += src_gap+8;

    }
  } else if (vi.IsRGB24()) {
    const BYTE* srcp = src->GetReadPtr();
    for (int y = vi.height; y>0; --y) {
      for (int x = (source_width-1)>>1; x; --x) {
        dstp[0] = (srcp[0] + 2*srcp[3] + srcp[6] + 2) >> 2;
        dstp[1] = (srcp[1] + 2*srcp[4] + srcp[7] + 2) >> 2;
        dstp[2] = (srcp[2] + 2*srcp[5] + srcp[8] + 2) >> 2;
        dstp += 3;
        srcp += 6;
      }
      if (source_width&1) {
        dstp += dst_gap;
        srcp += src_gap+3;
      } else {
        dstp[0] = (srcp[0] + srcp[3] + 1) >> 1;
        dstp[1] = (srcp[1] + srcp[4] + 1) >> 1;
        dstp[2] = (srcp[2] + srcp[5] + 1) >> 1;
        dstp += dst_gap+3;
        srcp += src_gap+6;
      }
    }
  } else if (vi.IsRGB32()) {  //rgb32
    const BYTE* srcp = src->GetReadPtr();
    for (int y = vi.height; y>0; --y) {
      for (int x = (source_width-1)>>1; x; --x) {
        dstp[0] = (srcp[0] + 2*srcp[4] + srcp[8] + 2) >> 2;
        dstp[1] = (srcp[1] + 2*srcp[5] + srcp[9] + 2) >> 2;
        dstp[2] = (srcp[2] + 2*srcp[6] + srcp[10] + 2) >> 2;
        dstp[3] = (srcp[3] + 2*srcp[7] + srcp[11] + 2) >> 2;
        dstp += 4;
        srcp += 8;
      }
      if (source_width&1) {
        dstp += dst_gap;
        srcp += src_gap+4;
      } else {
        dstp[0] = (srcp[0] + srcp[4] + 1) >> 1;
        dstp[1] = (srcp[1] + srcp[5] + 1) >> 1;
        dstp[2] = (srcp[2] + srcp[6] + 1) >> 1;
        dstp[3] = (srcp[3] + srcp[7] + 1) >> 1;
        dstp += dst_gap+4;
        srcp += src_gap+8;
      }
    }
  }
  return dst;
}

/**************************************
 *****  ReduceBy2 Factory Method  *****
 *************************************/
 

AVSValue __cdecl Create_ReduceBy2(AVSValue args, void*, IScriptEnvironment* env) 
{
  return new HorizontalReduceBy2(new VerticalReduceBy2(args[0].AsClip(), env),env);
}



