/**
 * FreeRDP: A Remote Desktop Protocol client.
 * RemoteFX Codec Library - Encode
 *
 * Copyright 2011 Vic Lee
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "rfx_types.h"
#include "rfx_rlgr.h"
#include "rfx_differential.h"
#include "rfx_quantization.h"
#include "rfx_dwt.h"

#include "rfx_encode.h"

#define MINMAX(_v,_l,_h) ((_v) < (_l) ? (_l) : ((_v) > (_h) ? (_h) : (_v)))

static void rfx_encode_format_rgb(const uint8* rgb_data, int width, int height, int rowstride,
	RFX_PIXEL_FORMAT pixel_format, sint16* r_buf, sint16* g_buf, sint16* b_buf)
{
	int x, y;
	int x_exceed;
	int y_exceed;
	const uint8* src;
	sint16 r, g, b;
	sint16 *r_last, *g_last, *b_last;

	x_exceed = 64 - width;
	y_exceed = 64 - height;
	for (y = 0; y < height; y++)
	{
		src = rgb_data + y * rowstride;

		switch (pixel_format)
		{
			case RFX_PIXEL_FORMAT_BGRA:
				for (x = 0; x < width; x++)
				{
					*b_buf++ = (sint16) (*src++);
					*g_buf++ = (sint16) (*src++);
					*r_buf++ = (sint16) (*src++);
					src++;
				}
				break;
			case RFX_PIXEL_FORMAT_RGBA:
				for (x = 0; x < width; x++)
				{
					*r_buf++ = (sint16) (*src++);
					*g_buf++ = (sint16) (*src++);
					*b_buf++ = (sint16) (*src++);
					src++;
				}
				break;
			case RFX_PIXEL_FORMAT_BGR:
				for (x = 0; x < width; x++)
				{
					*b_buf++ = (sint16) (*src++);
					*g_buf++ = (sint16) (*src++);
					*r_buf++ = (sint16) (*src++);
				}
				break;
			case RFX_PIXEL_FORMAT_RGB:
				for (x = 0; x < width; x++)
				{
					*r_buf++ = (sint16) (*src++);
					*g_buf++ = (sint16) (*src++);
					*b_buf++ = (sint16) (*src++);
				}
				break;
			case RFX_PIXEL_FORMAT_BGR565_LE:
				for (x = 0; x < width; x++)
				{
					*b_buf++ = (sint16) (((*(src + 1)) & 0xF8) | ((*(src + 1)) >> 5));
					*g_buf++ = (sint16) ((((*(src + 1)) & 0x07) << 5) | (((*src) & 0xE0) >> 3));
					*r_buf++ = (sint16) ((((*src) & 0x1F) << 3) | (((*src) >> 2) & 0x07));
					src += 2;
				}
				break;
			case RFX_PIXEL_FORMAT_RGB565_LE:
				for (x = 0; x < width; x++)
				{
					*r_buf++ = (sint16) (((*(src + 1)) & 0xF8) | ((*(src + 1)) >> 5));
					*g_buf++ = (sint16) ((((*(src + 1)) & 0x07) << 5) | (((*src) & 0xE0) >> 3));
					*b_buf++ = (sint16) ((((*src) & 0x1F) << 3) | (((*src) >> 2) & 0x07));
					src += 2;
				}
				break;
			default:
				break;
		}
		/* Fill the horizontal region outside of 64x64 tile size with the right-most pixel for best quality */
		if (x_exceed > 0)
		{
			r = *(r_buf - 1);
			g = *(g_buf - 1);
			b = *(b_buf - 1);
			for (x = 0; x < x_exceed; x++)
			{
				*r_buf++ = r;
				*g_buf++ = g;
				*b_buf++ = b;
			}
		}
	}

	/* Fill the vertical region outside of 64x64 tile size with the last line. */
	if (y_exceed > 0)
	{
		r_last = r_buf - 64;
		g_last = g_buf - 64;
		b_last = b_buf - 64;
		while (y_exceed > 0)
		{
			memcpy(r_buf, r_last, 64 * sizeof(sint16));
			memcpy(g_buf, g_last, 64 * sizeof(sint16));
			memcpy(b_buf, b_last, 64 * sizeof(sint16));
			r_buf += 64;
			g_buf += 64;
			b_buf += 64;
			y_exceed--;
		}
	}
}

void rfx_encode_rgb_to_ycbcr(sint16* y_r_buf, sint16* cb_g_buf, sint16* cr_b_buf)
{
	sint16 y, cb, cr;
	sint16 r, g, b;
	int i;

	/**
	 * The encoded YCbCr coeffectients are represented as 11.5 fixed-point numbers:
	 *
	 * 1 sign bit + 10 integer bits + 5 fractional bits
	 *
	 * However only 7 integer bits will be actually used since the value range is [-128.0, 127.0].
	 * In other words, the encoded coeffectients is scaled by << 5 when intepreted as sint16.
	 * It will be scaled down to original during the quantization phase.
	 */
	for (i = 0; i < 4096; i++)
	{
		r = y_r_buf[i];
		g = cb_g_buf[i];
		b = cr_b_buf[i];
		/* 0.299 << 5 = 1001.10010001(b), 0.587 << 5 = 10010.11001000(b), 0.114 << 5 = 11.10100101(b) */
		y = ((r << 3) + (r) + (r >> 1) + (r >> 4) + (r >> 7)) +
			((g << 4) + (g << 1) + (g >> 1) + (g >> 2) + (g >> 5)) +
			((b << 1) + (b) + (b >> 1) + (b >> 3) + (b >> 6) + (b >> 7));
		y_r_buf[i] = MINMAX(y - 4096, -4096, 4095);
		/* 0.168935 << 5 = 101.01100111(b), 0.331665 << 5 = 1010.10011100(b), 0.50059 << 5 = 10000.00000100(b) */
		cb = 0 - ((r << 2) + (r) + (r >> 2) + (r >> 3) + (r >> 5)) -
			((g << 3) + (g << 1) + (g >> 1) + (g >> 4) + (g >> 5) + (g >> 6)) +
			((b << 4) + (b >> 6));
		cb_g_buf[i] = MINMAX(cb, -4096, 4095);
		/* 0.499813 << 5 = 1111.11111110(b), 0.418531 << 5 = 1101.01100100(b), 0.081282 << 5 = 10.10011001(b) */
		cr = ((r << 4) - (r >> 7)) -
			((g << 3) + (g << 2) + (g) + (g >> 2) + (g >> 3) + (g >> 6)) -
			((b << 1) + (b >> 1) + (b >> 4) + (b >> 5) + (b >> 7));
		cr_b_buf[i] = MINMAX(cr, -4096, 4095);
	}
}

static void rfx_encode_component(RFX_CONTEXT* context, const uint32* quantization_values,
	sint16* data, uint8* buffer, int buffer_size, int* size)
{
	PROFILER_ENTER(context->priv->prof_rfx_encode_component);

	PROFILER_ENTER(context->priv->prof_rfx_dwt_2d_encode);
		context->dwt_2d_encode(data, context->priv->dwt_buffer);
	PROFILER_EXIT(context->priv->prof_rfx_dwt_2d_encode);

	PROFILER_ENTER(context->priv->prof_rfx_quantization_encode);
		context->quantization_encode(data, quantization_values);
	PROFILER_EXIT(context->priv->prof_rfx_quantization_encode);

	PROFILER_ENTER(context->priv->prof_rfx_differential_encode);
		rfx_differential_encode(data + 4032, 64);
	PROFILER_EXIT(context->priv->prof_rfx_differential_encode);

	PROFILER_ENTER(context->priv->prof_rfx_rlgr_encode);
		*size = rfx_rlgr_encode(context->mode, data, 4096, buffer, buffer_size);
	PROFILER_EXIT(context->priv->prof_rfx_rlgr_encode);

	PROFILER_EXIT(context->priv->prof_rfx_encode_component);
}

void rfx_encode_rgb(RFX_CONTEXT* context, const uint8* rgb_data, int width, int height, int rowstride,
	const uint32* y_quants, const uint32* cb_quants, const uint32* cr_quants,
	STREAM* data_out, int* y_size, int* cb_size, int* cr_size)
{
	sint16* y_r_buffer = context->priv->y_r_buffer;
	sint16* cb_g_buffer = context->priv->cb_g_buffer;
	sint16* cr_b_buffer = context->priv->cr_b_buffer;

	PROFILER_ENTER(context->priv->prof_rfx_encode_rgb);

	PROFILER_ENTER(context->priv->prof_rfx_encode_format_rgb);
		rfx_encode_format_rgb(rgb_data, width, height, rowstride,
			context->pixel_format, y_r_buffer, cb_g_buffer, cr_b_buffer);
	PROFILER_EXIT(context->priv->prof_rfx_encode_format_rgb);

	PROFILER_ENTER(context->priv->prof_rfx_encode_rgb_to_ycbcr);
		context->encode_rgb_to_ycbcr(context->priv->y_r_buffer, context->priv->cb_g_buffer, context->priv->cr_b_buffer);
	PROFILER_EXIT(context->priv->prof_rfx_encode_rgb_to_ycbcr);

	/* Ensure the buffer is reasonably large enough */
	stream_check_size(data_out, 4096);
	rfx_encode_component(context, y_quants, context->priv->y_r_buffer,
		stream_get_tail(data_out), stream_get_left(data_out), y_size);
	stream_seek(data_out, *y_size);

	stream_check_size(data_out, 4096);
	rfx_encode_component(context, cb_quants, context->priv->cb_g_buffer,
		stream_get_tail(data_out), stream_get_left(data_out), cb_size);
	stream_seek(data_out, *cb_size);

	stream_check_size(data_out, 4096);
	rfx_encode_component(context, cr_quants, context->priv->cr_b_buffer,
		stream_get_tail(data_out), stream_get_left(data_out), cr_size);
	stream_seek(data_out, *cr_size);

	PROFILER_EXIT(context->priv->prof_rfx_encode_rgb);
}
