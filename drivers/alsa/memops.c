/*
    Copyright (C) 2000 Paul Davis 

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

    $Id$
*/

#include <stdio.h>
#include <string.h>
#include <math.h>
#include <memory.h>
#include <stdlib.h>
#include <limits.h>

#include <jack/memops.h>

#define SAMPLE_MAX_24BIT  8388607.0f
#define SAMPLE_MAX_16BIT  32767.0f

/* Round float to int (can't go in memops.h otherwise it gets multiply
   defined) */
inline int f_round(float f) {
        f += (3<<22);
        return *((int*)&f) - 0x4b400000;
}

/* XXX we could use rint(), but for now we'll accept whatever default
   floating-point => int conversion the compiler provides.  
*/

void sample_move_d32u24_sS (char *dst, jack_default_audio_sample_t *src, unsigned long nsamples, unsigned long dst_skip, dither_state_t *state)

{
        long long y;

	while (nsamples--) {
		y = (long long)(*src * SAMPLE_MAX_24BIT) << 8;
		if (y > INT_MAX) {
			*((int *) dst) = INT_MAX;
		} else if (y < INT_MIN) {
			*((int *) dst) = INT_MIN;
		} else {
			*((int *) dst) = (int)y;
		}
		dst += dst_skip;
		src++;
	}
}	

void sample_move_dS_s32u24 (jack_default_audio_sample_t *dst, char *src, unsigned long nsamples, unsigned long src_skip)
{
	/* ALERT: signed sign-extension portability !!! */

	while (nsamples--) {
		*dst = (*((int *) src) >> 8) / SAMPLE_MAX_24BIT;
		dst++;
		src += src_skip;
	}
}	

void sample_move_dither_rect_d32u24_sS (char *dst, jack_default_audio_sample_t *src, unsigned long nsamples, unsigned long dst_skip, dither_state_t *state)

{
	/* ALERT: signed sign-extension portability !!! */
	jack_default_audio_sample_t  x;
	long long y;

	while (nsamples--) {
		x = *src * SAMPLE_MAX_16BIT;
		x -= (float)rand() / (float)RAND_MAX;
		y = (long long)f_round(x);
		y <<= 16;
		if (y > INT_MAX) {
			*((int *) dst) = INT_MAX;
		} else if (y < INT_MIN) {
			*((int *) dst) = INT_MIN;
		} else {
			*((int *) dst) = (int)y;
		}
		dst += dst_skip;
		src++;
	}
}	

void sample_move_dither_tri_d32u24_sS (char *dst,  jack_default_audio_sample_t *src, unsigned long nsamples, unsigned long dst_skip, dither_state_t *state)
	
{
	jack_default_audio_sample_t  x;
	float     r;
	float     rm1 = state->rm1;
	long long y;

	while (nsamples--) {
		x = *src * (float)SAMPLE_MAX_16BIT;
		r = 2.0f * (float)rand() / (float)RAND_MAX - 1.0f;
		x += r - rm1;
		rm1 = r;
		/* swh: This could be some inline asm on x86 */
		y = (long long)f_round(x);
		y <<= 16;

		if (y > INT_MAX) {
			*((int *) dst) = INT_MAX;
		} else if (y < INT_MIN) {
			*((int *) dst) = INT_MIN;
		} else {
			*((int *) dst) = (int)y;
		}

		dst += dst_skip;
		src++;
	}
	state->rm1 = rm1;
}

void sample_move_dither_shaped_d32u24_sS (char *dst,  jack_default_audio_sample_t *src, unsigned long nsamples, unsigned long dst_skip, dither_state_t *state)
	
{
	jack_default_audio_sample_t     x;
	jack_default_audio_sample_t     xe; /* the innput sample - filtered error */
	jack_default_audio_sample_t     xp; /* x' */
	float        r;
	float        rm1 = state->rm1;
	unsigned int idx = state->idx;
	long long    y;

	while (nsamples--) {
		x = *src * (float)SAMPLE_MAX_16BIT;
		r = 2.0f * (float)rand() / (float)RAND_MAX - 1.0f;
		/* Filter the error with Lipshitz's minimally audible FIR:
		   [2.033 -2.165 1.959 -1.590 0.6149] */
		xe = x
		     - state->e[idx] * 2.033f
		     + state->e[(idx - 1) & DITHER_BUF_MASK] * 2.165f
		     - state->e[(idx - 2) & DITHER_BUF_MASK] * 1.959f
		     + state->e[(idx - 3) & DITHER_BUF_MASK] * 1.590f
		     - state->e[(idx - 4) & DITHER_BUF_MASK] * 0.6149f;
		xp = xe + r - rm1;
		rm1 = r;

		/* This could be some inline asm on x86 */
		y = (long long)f_round(xp);

		/* Intrinsic z^-1 delay */
		idx = (idx + 1) & DITHER_BUF_MASK;
		state->e[idx] = y - xe;

		y <<= 16;

		if (y > INT_MAX) {
			*((int *) dst) = INT_MAX;
		} else if (y < INT_MIN) {
			*((int *) dst) = INT_MIN;
		} else {
			*((int *) dst) = (int)y;
		}
		dst += dst_skip;
		src++;
	}
	state->rm1 = rm1;
	state->idx = idx;
}

void sample_move_d16_sS (char *dst,  jack_default_audio_sample_t *src, unsigned long nsamples, unsigned long dst_skip, dither_state_t *state)
	
{
	jack_default_audio_sample_t val;

	/* ALERT: signed sign-extension portability !!! */

	/* XXX good to use x86 assembler here, since float->short
	   sucks that h/w.
	*/
	
	while (nsamples--) {
		val = *src;
		if (val > 1.0f) {
			*((short *)dst) = SHRT_MAX;
		} else if (val < -1.0f) {
			*((short *)dst) = SHRT_MIN;
		} else {
			*((short *) dst) = (short) (val * SAMPLE_MAX_16BIT);
		}
		dst += dst_skip;
		src++;
	}
}

void sample_move_dither_rect_d16_sS (char *dst,  jack_default_audio_sample_t *src, unsigned long nsamples, unsigned long dst_skip, dither_state_t *state)
	
{
	jack_default_audio_sample_t val;
	int      tmp;

	while (nsamples--) {
		val = *src * (float)SAMPLE_MAX_16BIT;
		val -= (float)rand() / (float)RAND_MAX;
		/* swh: This could be some inline asm on x86 */
		tmp = f_round(val);
		if (tmp > SHRT_MAX) {
			*((short *)dst) = SHRT_MAX;
		} else if (tmp < SHRT_MIN) {
			*((short *)dst) = SHRT_MIN;
		} else {
			*((short *) dst) = (short)tmp;
		}
		dst += dst_skip;
		src++;
	}
}

void sample_move_dither_tri_d16_sS (char *dst,  jack_default_audio_sample_t *src, unsigned long nsamples, unsigned long dst_skip, dither_state_t *state)
	
{
	jack_default_audio_sample_t x;
	float    r;
	float    rm1 = state->rm1;
	int      y;

	while (nsamples--) {
		x = *src * (float)SAMPLE_MAX_16BIT;
		r = 2.0f * (float)rand() / (float)RAND_MAX - 1.0f;
		x += r - rm1;
		rm1 = r;
		/* swh: This could be some inline asm on x86 */
		y = f_round(x);

		if (y > SHRT_MAX) {
			*((short *)dst) = SHRT_MAX;
		} else if (y < SHRT_MIN) {
			*((short *)dst) = SHRT_MIN;
		} else {
			*((short *) dst) = (short)y;
		}

		dst += dst_skip;
		src++;
	}
	state->rm1 = rm1;
}

void sample_move_dither_shaped_d16_sS (char *dst,  jack_default_audio_sample_t *src, unsigned long nsamples, unsigned long dst_skip, dither_state_t *state)
	
{
	jack_default_audio_sample_t     x;
	jack_default_audio_sample_t     xe; /* the innput sample - filtered error */
	jack_default_audio_sample_t     xp; /* x' */
	float        r;
	float        rm1 = state->rm1;
	unsigned int idx = state->idx;
	int          y;

	while (nsamples--) {
		x = *src * (float)SAMPLE_MAX_16BIT;
		r = 2.0f * (float)rand() / (float)RAND_MAX - 1.0f;
		/* Filter the error with Lipshitz's minimally audible FIR:
		   [2.033 -2.165 1.959 -1.590 0.6149] */
		xe = x
		     - state->e[idx] * 2.033f
		     + state->e[(idx - 1) & DITHER_BUF_MASK] * 2.165f
		     - state->e[(idx - 2) & DITHER_BUF_MASK] * 1.959f
		     + state->e[(idx - 3) & DITHER_BUF_MASK] * 1.590f
		     - state->e[(idx - 4) & DITHER_BUF_MASK] * 0.6149f;
		xp = xe + r - rm1;
		rm1 = r;

		/* This could be some inline asm on x86 */
		y = f_round(xp);

		/* Intrinsic z^-1 delay */
		idx = (idx + 1) & DITHER_BUF_MASK;
		state->e[idx] = y - xe;

		if (y > SHRT_MAX) {
			*((short *)dst) = SHRT_MAX;
		} else if (y < SHRT_MIN) {
			*((short *)dst) = SHRT_MIN;
		} else {
			*((short *) dst) = (short)y;
		}
		dst += dst_skip;
		src++;
	}
	state->rm1 = rm1;
	state->idx = idx;
}

void sample_move_dS_s16 (jack_default_audio_sample_t *dst, char *src, unsigned long nsamples, unsigned long src_skip) 
	
{
	/* ALERT: signed sign-extension portability !!! */
	while (nsamples--) {
		*dst = (*((short *) src)) / SAMPLE_MAX_16BIT;
		dst++;
		src += src_skip;
	}
}	

void sample_merge_d16_sS (char *dst,  jack_default_audio_sample_t *src, unsigned long nsamples, unsigned long dst_skip, dither_state_t *state)
{
	short val;

	/* ALERT: signed sign-extension portability !!! */
	
	while (nsamples--) {
		val = (short) (*src * SAMPLE_MAX_16BIT);
		
		if (val > SHRT_MAX - *((short *) dst)) {
			*((short *)dst) = SHRT_MAX;
		} else if (val < SHRT_MIN - *((short *) dst)) {
			*((short *)dst) = SHRT_MIN;
		} else {
			*((short *) dst) += val;
		}
		dst += dst_skip;
		src++;
	}
}	

void sample_merge_d32u24_sS (char *dst, jack_default_audio_sample_t *src, unsigned long nsamples, unsigned long dst_skip, dither_state_t *state)

{
	/* ALERT: signed sign-extension portability !!! */

	while (nsamples--) {
		*((int *) dst) += (((int) (*src * SAMPLE_MAX_24BIT)) << 8);
		dst += dst_skip;
		src++;
	}
}	

void memset_interleave (char *dst, char val, unsigned long bytes, 
			unsigned long unit_bytes, 
			unsigned long skip_bytes) 
{
	switch (unit_bytes) {
	case 1:
		while (bytes--) {
			*dst = val;
			dst += skip_bytes;
		}
		break;
	case 2:
		while (bytes) {
			*((short *) dst) = (short) val;
			dst += skip_bytes;
			bytes -= 2;
		}
		break;
	case 4:		    
		while (bytes) {
			*((int *) dst) = (int) val;
			dst += skip_bytes;
			bytes -= 4;
		}
		break;
	}
}

/* COPY FUNCTIONS: used to move data from an input channel to an
   output channel. Note that we assume that the skip distance
   is the same for both channels. This is completely fine
   unless the input and output were on different audio interfaces that
   were interleaved differently. We don't try to handle that.
*/

void 
memcpy_fake (char *dst, char *src, unsigned long src_bytes, unsigned long foo, unsigned long bar)
{
	memcpy (dst, src, src_bytes);
}

void 
merge_memcpy_d16_s16 (char *dst, char *src, unsigned long src_bytes,
		      unsigned long dst_skip_bytes, unsigned long src_skip_bytes)
{
	while (src_bytes) {
		*((short *) dst) += *((short *) src);
		dst += 2;
		src += 2;
		src_bytes -= 2;
	}
}

void 
merge_memcpy_d32_s32 (char *dst, char *src, unsigned long src_bytes,
		      unsigned long dst_skip_bytes, unsigned long src_skip_bytes)

{
	while (src_bytes) {
		*((int *) dst) += *((int *) src);
		dst += 4;
		src += 4;
		src_bytes -= 4;
	}
}

void 
merge_memcpy_interleave_d16_s16 (char *dst, char *src, unsigned long src_bytes, 
				 unsigned long dst_skip_bytes, unsigned long src_skip_bytes)

{
	while (src_bytes) {
		*((short *) dst) += *((short *) src);
		dst += dst_skip_bytes;
		src += src_skip_bytes;
		src_bytes -= 2;
	}
}

void 
merge_memcpy_interleave_d32_s32 (char *dst, char *src, unsigned long src_bytes,
				 unsigned long dst_skip_bytes, unsigned long src_skip_bytes)
{
	while (src_bytes) {
		*((int *) dst) += *((int *) src);
		dst += dst_skip_bytes;
		src += src_skip_bytes;
		src_bytes -= 4;
	}
}

void 
memcpy_interleave_d16_s16 (char *dst, char *src, unsigned long src_bytes,
			   unsigned long dst_skip_bytes, unsigned long src_skip_bytes)
{
	while (src_bytes) {
		*((short *) dst) = *((short *) src);
		dst += dst_skip_bytes;
		src += src_skip_bytes;
		src_bytes -= 2;
	}
}

void 
memcpy_interleave_d32_s32 (char *dst, char *src, unsigned long src_bytes,
			   unsigned long dst_skip_bytes, unsigned long src_skip_bytes)

{
	while (src_bytes) {
		*((int *) dst) = *((int *) src);
		dst += dst_skip_bytes;
		src += src_skip_bytes;
		src_bytes -= 4;
	}
}