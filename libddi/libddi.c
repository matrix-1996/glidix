/*
	Glidix Display Device Interface (DDI)

	Copyright (c) 2014-2016, Madd Games.
	All rights reserved.
	
	Redistribution and use in source and binary forms, with or without
	modification, are permitted provided that the following conditions are met:
	
	* Redistributions of source code must retain the above copyright notice, this
	  list of conditions and the following disclaimer.
	
	* Redistributions in binary form must reproduce the above copyright notice,
	  this list of conditions and the following disclaimer in the documentation
	  and/or other materials provided with the distribution.
	
	THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
	AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
	IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
	DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
	FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
	DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
	SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
	CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
	OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
	OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <png.h>
#include <ft2build.h>
#include FT_FREETYPE_H

#include "libddi.h"

typedef uint8_t	DDIByteVector __attribute__ ((vector_size(4)));
typedef int DDIIntVector __attribute__ ((vector_size(16)));

typedef struct PenSegment_
{
	DDISurface *surface;
	struct PenSegment_ *next;
	int firstCharPos;			// position of the first character in the segment, within the original text
	size_t numChars;			// number of characters in segment
	int *widths;				// width of each character
	
	// where the segment was last drawn (initialized by ddiExecutePen).
	int drawX, drawY;
	
	// baseline position
	int baselineY;
	DDIColor background;
} PenSegment;

typedef struct PenLine_
{
	PenSegment *firstSegment;
	int currentWidth;
	int maxHeight;
	int alignment;
	int lineHeight;				// %
	int drawY;
	int baselineY;
	struct PenLine_ *next;
} PenLine;

struct DDIFont_
{
	/**
	 * FreeType library handle.
	 */
	FT_Library lib;

	/**
	 * Current font.
	 */
	FT_Face face;
	
	/**
	 * Font size.
	 */
	int size;
};

struct DDIPen_
{
	/**
	 * The format of the surface onto which we will draw (typically the screen format).
	 */
	DDIPixelFormat format;
	
	/**
	 * The font which we are currently using.
	 */
	DDIFont *font;
	
	/**
	 * The bounding box onto which to draw.
	 */
	int x, y, width, height;
	
	/**
	 * The current scroll position.
	 */
	int scrollX, scrollY;
	
	/**
	 * Nonzero if text should be word-wrapped automatically (default = 1).
	 */
	int wrap;
	
	/**
	 * Text alignment (default = DDI_ALIGN_LEFT).
	 */
	int align;
	
	/**
	 * Letter spacing and line height. Letter spacing is given in pixels (default = 0), while the line height
	 * is given as a percentage (default = 100).
	 */
	int letterSpacing;
	int lineHeight;
	
	/**
	 * Cursor position, in character units; -1 (default) means do not draw the cursor.
	 */
	int cursorPos;
	
	/**
	 * The current writer position (i.e. how many unicode characters were written to the pen).
	 */
	int writePos;
	
	/**
	 * Masking character (0 = none, 1 = default, everything else = codepoint for mask).
	 */
	long mask;
	
	/**
	 * Background and foreground colors.
	 */
	DDIColor background;
	DDIColor foreground;
	
	/**
	 * Current line being layed out.
	 */
	PenLine *currentLine;
	
	/**
	 * Head of the line list.
	 */
	PenLine *firstLine;
};

size_t ddiGetFormatDataSize(DDIPixelFormat *format, unsigned int width, unsigned int height)
{
	size_t scanlineLen = format->scanlineSpacing
		+ (format->bpp + format->pixelSpacing) * width;
	size_t size = scanlineLen * height;
	
	// make sure the size is a multiple of 4 bytes as that helps with certain operations
	if (size & 3)
	{
		size &= ~((size_t)3);
		size += 4;
	};
	
	return size;
};

static size_t ddiGetSurfaceDataSize(DDISurface *surface)
{
	size_t scanlineLen = surface->format.scanlineSpacing
		+ (surface->format.bpp + surface->format.pixelSpacing) * surface->width;
	size_t size = scanlineLen * surface->height;
	
	// make sure the size is a multiple of 4 bytes as that helps with certain operations
	if (size & 3)
	{
		size &= ~((size_t)3);
		size += 4;
	};
	
	return size;
};

DDISurface* ddiCreateSurface(DDIPixelFormat *format, unsigned int width, unsigned int height, char *data, unsigned int flags)
{
	DDISurface *surface = (DDISurface*) malloc(sizeof(DDISurface));
	memcpy(&surface->format, format, sizeof(DDIPixelFormat));
	
	surface->width = width;
	surface->height = height;
	surface->flags = flags;
	
	if ((flags & DDI_STATIC_FRAMEBUFFER) == 0)
	{
		surface->data = (uint8_t*) malloc(ddiGetSurfaceDataSize(surface));
		if (data != NULL) memcpy(surface->data, data, ddiGetSurfaceDataSize(surface));
	}
	else
	{
		surface->data = (uint8_t*) data;
	};
	
	return surface;
};

void ddiDeleteSurface(DDISurface *surface)
{
	if ((surface->flags & DDI_STATIC_FRAMEBUFFER) == 0)
	{
		free(surface->data);
	};
	
	free(surface);
};

static void ddiInsertWithMask(uint32_t *target, uint32_t mask, uint32_t val)
{
	if (mask == 0) return;
	while ((mask & 1) == 0)
	{
		mask >>= 1;
		val <<= 1;
	};
	
	(*target) |= val;
};

static void ddiColorToPixel(uint32_t *pixeldata, DDIPixelFormat *format, DDIColor *color)
{
	*pixeldata = 0;
	ddiInsertWithMask(pixeldata, format->redMask, color->red);
	ddiInsertWithMask(pixeldata, format->greenMask, color->green);
	ddiInsertWithMask(pixeldata, format->blueMask, color->blue);
	ddiInsertWithMask(pixeldata, format->alphaMask, color->alpha);
};

static void ddiCopy(void *dest, void *src, uint64_t count)
{
	if(!count){return;}
	uint64_t maskDest = (uint64_t) dest & 0xFUL;
	if (maskDest != ((uint64_t) src & 0xFUL))
	{
		memcpy(dest, src, count);
		return;
	};
	
	uint64_t skip = (16 - maskDest) & 15;
	if (skip >= count)
	{
		memcpy(dest, src, count);
		return;
	}
	else
	{
		memcpy(dest, src, skip);
		dest += skip;
		src += skip;
		count -= skip;
	};
	
	while(count >= 8){ *(uint64_t*)dest = *(uint64_t*)src; dest += 8; src += 8; count -= 8; };
	while(count >= 4){ *(uint32_t*)dest = *(uint32_t*)src; dest += 4; src += 4; count -= 4; };
	while(count >= 2){ *(uint16_t*)dest = *(uint16_t*)src; dest += 2; src += 2; count -= 2; };
	while(count >= 1){ *(uint8_t*)dest = *(uint8_t*)src; dest += 1; src += 1; count -= 1; };
};

static void ddiFill(void *dest, void *src, uint64_t unit, uint64_t count)
{
	while (count--)
	{
		ddiCopy(dest, src, unit);
		dest += unit;
	};
};

void ddiFillRect(DDISurface *surface, int x, int y, unsigned int width, unsigned int height, DDIColor *color)
{
	while (x < 0)
	{
		x++;
		if (width == 0) return;
		width--;
	};
	
	while (y < 0)
	{
		y++;
		if (height == 0) return;
		height--;
	};
	
	if ((x >= surface->width) || (y >= surface->height))
	{
		// do not need to do anything
		return;
	};
	
	if ((x+width) > surface->width)
	{
		width = surface->width - x;
	};
	
	if ((y+height) > surface->height)
	{
		height = surface->height - y;
	};
	
	uint32_t pixel;
	ddiColorToPixel(&pixel, &surface->format, color);
	
	size_t pixelSize = surface->format.bpp + surface->format.pixelSpacing;
	size_t scanlineSize = surface->format.scanlineSpacing + pixelSize * surface->width;
	
	char pixbuf[32];
	memcpy(pixbuf, &pixel, 4);
	
	size_t offset = scanlineSize * y + pixelSize * x;
	unsigned char *put = surface->data + offset;
	for (; height; height--)
	{
		ddiFill(put, pixbuf, pixelSize, width);
		put += scanlineSize;
	};
};

void ddiOverlay(DDISurface *src, int srcX, int srcY, DDISurface *dest, int destX, int destY,
	unsigned int width, unsigned int height)
{
	// cannot copy from negative source coordinates
	if ((srcX < 0) || (srcY < 0))
	{
		return;
	};
	
	// we can copy to negative target coordinates with some messing around
	while (destX < 0)
	{
		srcX++;
		destX++;
		if (width < 0) return;
		width--;
	};
	
	while (destY < 0)
	{
		srcY++;
		destY++;
		if (height < 0) return;
		height--;
	};
	
	// clip the rectangle to make sure it can fit on both surfaces
	if ((srcX >= src->width) || (srcY >= src->height))
	{
		// do not need to do anything
		return;
	};

	if ((destX+(int)width <= 0) || (destY+(int)height <= 0))
	{
		return;
	};
	
	if ((srcX+width) > src->width)
	{
		if (src->width <= srcX)
		{
			return;
		};
		
		width = src->width - srcX;
	};
	
	if ((srcY+height) > src->height)
	{
		if (src->height <= srcY)
		{
			return;
		};
		
		height = src->height - srcY;
	};
	
	if ((destX >= dest->width) || (destY >= dest->height))
	{
		// do not need to do anything
		return;
	};
	
	if ((destX+width) > dest->width)
	{
		if (dest->width <= destX)
		{
			return;
		};
		
		width = dest->width - destX;
	};
	
	if ((destY+height) > dest->height)
	{
		if (dest->height <= destY)
		{
			return;
		};
		
		height = dest->height - destY;
	};
	
	// make sure the formats are the same
	if (memcmp(&src->format, &dest->format, sizeof(DDIPixelFormat)) != 0)
	{
		return;
	};
	
	// calculate offsets
	size_t pixelSize = src->format.bpp + src->format.pixelSpacing;
	size_t srcScanlineSize = src->format.scanlineSpacing + pixelSize * src->width;
	size_t destScanlineSize = dest->format.scanlineSpacing + pixelSize * dest->width;
	
	unsigned char *scan = src->data + pixelSize * srcX + srcScanlineSize * srcY;
	unsigned char *put = dest->data + pixelSize * destX + destScanlineSize * destY;
	
	for (; height; height--)
	{
		ddiCopy(put, scan, pixelSize * width);
		//memcpy(put, scan, pixelSize * width);
		scan += srcScanlineSize;
		put += destScanlineSize;
	};
};

#define	DDI_ERROR(msg)	if (error != NULL) *error = msg

DDISurface* ddiLoadPNG(const char *filename, const char **error)
{
	unsigned char header[8];
	
	FILE *fp = fopen(filename, "rb");
	if (fp == NULL)
	{
		DDI_ERROR("Could not open the PNG file");
		return NULL;
	};
	
	fread(header, 1, 8, fp);
	if (png_sig_cmp(header, 0, 8))
	{
		DDI_ERROR("Invalid header in PNG file");
		fclose(fp);
		return NULL;
	};
	
	png_structp png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
	if (!png_ptr)
	{
		DDI_ERROR("Could not allocate PNG read struct");
		fclose(fp);
		return NULL;
	};
	
	png_infop info_ptr = png_create_info_struct(png_ptr);
	if (!info_ptr)
	{
		DDI_ERROR("Failed to create PNG info struct");
		fclose(fp);
		png_destroy_read_struct(&png_ptr, NULL, NULL);
		return NULL;
	};
	
	if (setjmp(png_jmpbuf(png_ptr)))
	{
		DDI_ERROR("Error during png_init_io");
		fclose(fp);
		png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
		return NULL;
	};
	
	png_init_io(png_ptr, fp);
	png_set_sig_bytes(png_ptr, 8);
	png_read_info(png_ptr, info_ptr);
	
	unsigned int width = png_get_image_width(png_ptr, info_ptr);
	unsigned int height = png_get_image_height(png_ptr, info_ptr);
	png_byte colorType = png_get_color_type(png_ptr, info_ptr);
	
	png_read_update_info(png_ptr, info_ptr);
	
	if (setjmp(png_jmpbuf(png_ptr)))
	{
		DDI_ERROR("Failed to read PNG file");
		fclose(fp);
		png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
		return NULL;
	};
	
	png_bytep *rowPointers = (png_bytep*) malloc(sizeof(png_bytep) * height);
	int y;
	for (y=0; y<height; y++)
	{
		rowPointers[y] = (png_byte*) malloc(png_get_rowbytes(png_ptr, info_ptr));
	};
	
	png_read_image(png_ptr, rowPointers);
	png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
	fclose(fp);
	
	// OK, now translate that into a surface
	DDIPixelFormat format;
	if (colorType == PNG_COLOR_TYPE_RGB)
	{
		format.bpp = 3;
		format.alphaMask = 0;
	}
	else if (colorType == PNG_COLOR_TYPE_RGBA)
	{
		format.bpp = 4;
		format.alphaMask = 0xFF000000;
	}
	else
	{
		DDI_ERROR("Unsupported color type");
		return NULL;
	};
	
	format.redMask = 0x0000FF;
	format.greenMask = 0x00FF00;
	format.blueMask = 0xFF0000;
	
	format.pixelSpacing = 0;
	format.scanlineSpacing = 0;
	
	DDISurface *surface = ddiCreateSurface(&format, width, height, NULL, 0);
	unsigned char *put = surface->data;
	
	for (y=0; y<height; y++)
	{
		ddiCopy(put, rowPointers[y], format.bpp*width);
		put += format.bpp*width;
	};
	
	return surface;
};

static int ddiGetIndexForMask(uint32_t mask)
{
	if (mask == 0x0000FF) return 0;
	if (mask == 0x00FF00) return 1;
	if (mask == 0xFF0000) return 2;
	return 3;
};

DDISurface* ddiConvertSurface(DDIPixelFormat *format, DDISurface *surface, const char **error)
{
	// find the shuffle mask to use
	DDIByteVector convertMask;
	int indexIntoRed = ddiGetIndexForMask(format->redMask);
	int indexIntoGreen = ddiGetIndexForMask(format->greenMask);
	int indexIntoBlue = ddiGetIndexForMask(format->blueMask);
	int indexIntoAlpha = ddiGetIndexForMask(format->alphaMask);
	
	convertMask[indexIntoRed] = ddiGetIndexForMask(surface->format.redMask);
	convertMask[indexIntoGreen] = ddiGetIndexForMask(surface->format.greenMask);
	convertMask[indexIntoBlue] = ddiGetIndexForMask(surface->format.blueMask);
	convertMask[indexIntoAlpha] = ddiGetIndexForMask(surface->format.alphaMask);
	
	// create the new surface and prepare for conversion
	DDISurface *newsurf = ddiCreateSurface(format, surface->width, surface->height, NULL, 0);
	
	size_t targetPixelSize = format->bpp + format->pixelSpacing;
	
	size_t srcPixelSize = surface->format.bpp + surface->format.pixelSpacing;
	
	unsigned char *put = newsurf->data;
	unsigned char *scan = surface->data;
	
	int x, y;
	for (y=0; y<surface->height; y++)
	{
		for (x=0; x<surface->width; x++)
		{
			*((DDIByteVector*)put) = __builtin_shuffle(*((DDIByteVector*)scan), convertMask);
			put += targetPixelSize;
			scan += srcPixelSize;
		};
		
		put += format->scanlineSpacing;
		scan += surface->format.scanlineSpacing;
	};
	
	return newsurf;
};

DDISurface* ddiLoadAndConvertPNG(DDIPixelFormat *format, const char *filename, const char **error)
{
	DDISurface *org = ddiLoadPNG(filename, error);
	if (org == NULL) return NULL;
	
	DDISurface *surface = ddiConvertSurface(format, org, error);
	ddiDeleteSurface(org);
	return surface;
};

/**
 * Don't question the use of the "register" keyword here. It causes the function to be approximately
 * 2 times faster, when compiling with GCC.
 */
void ddiBlit(DDISurface *src, int srcX, int srcY, DDISurface *dest, int destX, int destY,
	unsigned int width, unsigned int height)
{
	if (memcmp(&src->format, &dest->format, sizeof(DDIPixelFormat)) != 0)
	{
		return;
	};
	
	if (src->format.alphaMask == 0)
	{
		ddiOverlay(src, srcX, srcY, dest, destX, destY, width, height);
	};
	
	int alphaIndex = ddiGetIndexForMask(src->format.alphaMask);

	// cannot copy from negative source coordinates
	if ((srcX < 0) || (srcY < 0))
	{
		return;
	};
	
	// we can copy to negative target coordinates with some messing around
	while (destX < 0)
	{
		srcX++;
		destX++;
		if (width < 0) return;
		width--;
	};
	
	while (destY < 0)
	{
		srcY++;
		destY++;
		if (height < 0) return;
		height--;
	};
	
	// clip the rectangle to make sure it can fit on both surfaces
	if ((srcX >= src->width) || (srcY >= src->height))
	{
		// do not need to do anything
		return;
	};
	
	if ((destX+(int)width <= 0) || (destY+(int)height <= 0))
	{
		return;
	};
	
	if ((srcX+width) > src->width)
	{
		if (src->width <= srcX)
		{
			return;
		};
		
		width = src->width - srcX;
	};
	
	if ((srcY+height) > src->height)
	{
		if (src->height <= srcY)
		{
			return;
		};
		
		height = src->height - srcY;
	};
	
	if ((destX >= dest->width) || (destY >= dest->height))
	{
		// do not need to do anything
		return;
	};
	
	if ((destX+width) > dest->width)
	{
		if (dest->width <= destX)
		{
			return;
		};
		
		width = dest->width - destX;
	};
	
	if ((destY+height) > dest->height)
	{
		if (dest->height <= destY)
		{
			return;
		};
		
		height = dest->height - destY;
	};
	
	// calculate offsets
	register size_t pixelSize = src->format.bpp + src->format.pixelSpacing;
	register size_t srcScanlineSize = src->format.scanlineSpacing + pixelSize * src->width;
	register size_t destScanlineSize = dest->format.scanlineSpacing + pixelSize * dest->width;
	
	register uint8_t *scan = (uint8_t*) src->data + pixelSize * srcX + srcScanlineSize * srcY;
	register uint8_t *put = (uint8_t*) dest->data + pixelSize * destX + destScanlineSize * destY;
	
	for (; height; height--)
	{
		register size_t count = width;
		register uint8_t *scanStart = scan;
		register uint8_t *putStart = put;
		
		while (count--)
		{
			register int srcAlpha = (int) scan[alphaIndex];
			register int dstAlpha = (int) put[alphaIndex];
			register int outAlpha = srcAlpha + (int) dstAlpha * (255 - srcAlpha) / 255;

			if (outAlpha == 0)
			{
				*((uint32_t*)put) = 0;
			}
			else
			{
				DDIIntVector vdst = {put[0], put[1], put[2], put[3]};
				DDIIntVector vsrc = {scan[0], scan[1], scan[2], scan[3]};
				DDIIntVector result = (
					(vsrc * srcAlpha)/255
					+ (vdst * dstAlpha * (255-srcAlpha))/(255*255)
				)*255/outAlpha;
				
				put[0] = (uint8_t) result[0];
				put[1] = (uint8_t) result[1];
				put[2] = (uint8_t) result[2];
				put[3] = (uint8_t) result[3];
				put[alphaIndex] = outAlpha;
			};

			scan += pixelSize;
			put += pixelSize;
		};
		
		scan = scanStart + srcScanlineSize;
		put = putStart + destScanlineSize;
	};
};

static void ddiExpandBitmapRow(unsigned char *put, uint8_t row, const void *fill, int bpp, size_t pixelSize)
{
	static uint8_t mask[8] = {1, 2, 4, 8, 16, 32, 64, 128};
	int i;
	for (i=0; i<8; i++)
	{
		if (row & mask[i])
		{
			memcpy(put, fill, bpp);
		};
		
		put += pixelSize;
	};
};

void ddiExpandBitmap(DDISurface *surface, unsigned int x, unsigned int y, int type, const void *bitmap_, DDIColor *color)
{
	int charHeight;
	switch (type)
	{
	case DDI_BITMAP_8x8:
		charHeight = 8;
		break;
	case DDI_BITMAP_8x16:
		charHeight = 16;
		break;
	default:
		return;
	};
	
	uint32_t pixel;
	ddiColorToPixel(&pixel, &surface->format, color);
	
	if ((x+8) > surface->width)
	{
		return;
	};
	
	if ((y+8) > surface->height)
	{
		return;
	};
	
	const uint8_t *bitmap = (const uint8_t*) bitmap_;
	size_t pixelSize = surface->format.bpp + surface->format.pixelSpacing;
	size_t scanlineSize = surface->format.scanlineSpacing + pixelSize * surface->width;
	
	size_t offset = scanlineSize * y + pixelSize * x;
	unsigned char *put = surface->data + offset;
	
	int by;
	for (by=0; by<charHeight; by++)
	{
		ddiExpandBitmapRow(put, bitmap[by], &pixel, surface->format.bpp, pixelSize);
		put += scanlineSize;
	};
};

long ddiReadUTF8(const char **strptr)
{
	if (**strptr == 0)
	{
		return 0;
	};
	
	if ((**strptr & 0x80) == 0)
	{
		long ret = (long) (uint8_t) **strptr;
		(*strptr)++;
		return ret;
	};
	
	long result = 0;
	uint8_t c = (uint8_t) **strptr;
	(*strptr)++;
	int len = 0;
		
	while (c & 0x80)
	{
		c <<= 1;
		len++;
	};
		
	result = (long) (c >> len);
	
	while (--len)
	{
		c = **strptr;
		(*strptr)++;
		result <<= 6;
		result |= (c & 0x3F);	
	};
	
	return result;
};

DDIFont* ddiLoadFont(const char *family, int size, int style, const char **error)
{
	DDIFont *font = (DDIFont*) malloc(sizeof(DDIFont));
	if (font == NULL)
	{
		DDI_ERROR("Out of memory");
		return NULL;
	};
	
	font->size = size;
	if (strlen(family) > 64)
	{
		DDI_ERROR("Font name too long");
		free(font);
		return NULL;
	};
	
	char fontfile[256];
	int type = style & (DDI_STYLE_BOLD | DDI_STYLE_ITALIC);
	
	if (type == 0)
	{
		// regular
		sprintf(fontfile, "/usr/share/fonts/regular/%s.ttf", family);
	}
	else if (type == DDI_STYLE_BOLD)
	{
		sprintf(fontfile, "/usr/share/fonts/bold/%s Bold.ttf", family);
	}
	else if (type == DDI_STYLE_ITALIC)
	{
		sprintf(fontfile, "/usr/share/fonts/italic/%s Italic.ttf", family);
	}
	else
	{
		// bold italic
		sprintf(fontfile, "/usr/share/fonts/bi/%s BoldItalic.ttf", family);
	};
	
	FT_Error fterr = FT_Init_FreeType(&font->lib);
	if (fterr != 0)
	{
		DDI_ERROR("FreeType initialization failed");
		free(font);
		return NULL;
	};
	
	// load the new font
	fterr = FT_New_Face(font->lib, fontfile, 0, &font->face);
	if (fterr != 0)
	{
		DDI_ERROR("Failed to load the font");
		free(font);
		return NULL;
	};
	
	fterr = FT_Set_Char_Size(font->face, 0, size*64, 0, 0);
	if (fterr != 0)
	{
		DDI_ERROR("Failed to set font size");
		FT_Done_Face(font->face);
		FT_Done_FreeType(font->lib);
		free(font);
		return NULL;
	};
	
	return font;
};

DDIPen* ddiCreatePen(DDIPixelFormat *format, DDIFont *font, int x, int y, int width, int height, int scrollX, int scrollY, const char **error)
{
	DDIPen *pen = (DDIPen*) malloc(sizeof(DDIPen));
	memcpy(&pen->format, format, sizeof(DDIPixelFormat));
	pen->font = font;
	pen->x = x;
	pen->y = y;
	pen->width = width;
	pen->height = height;
	pen->wrap = 1;
	pen->align = DDI_ALIGN_LEFT;
	pen->letterSpacing = 0;
	pen->lineHeight = 100;
	pen->cursorPos = -1;
	pen->writePos = 0;
	pen->mask = 0;
	
	DDIColor foreground = {0, 0, 0, 255};
	DDIColor background = {0, 0, 0, 0};
	
	memcpy(&pen->foreground, &foreground, sizeof(DDIColor));
	memcpy(&pen->background, &background, sizeof(DDIColor));

	pen->currentLine = (PenLine*) malloc(sizeof(PenLine));
	pen->currentLine->firstSegment = NULL;
	pen->currentLine->maxHeight = font->size;
	pen->currentLine->currentWidth = 0;
	pen->currentLine->baselineY = 0;
	pen->currentLine->next = NULL;
	pen->firstLine = pen->currentLine;
	
	return pen;
};

void ddiSetPenCursor(DDIPen *pen, int cursorPos)
{
	pen->cursorPos = cursorPos;
};

void ddiDeletePen(DDIPen *pen)
{
	PenLine *line = pen->firstLine;
	PenSegment *seg;
	
	while (line != NULL)
	{
		seg = line->firstSegment;
		while (seg != NULL)
		{
			PenSegment *nextSeg = seg->next;
			ddiDeleteSurface(seg->surface);
			free(seg->widths);
			free(seg);
			seg = nextSeg;
		};
		
		PenLine *nextLine = line->next;
		free(line);
		line = nextLine;
	};
	
	free(pen);
};

void ddiSetPenWrap(DDIPen *pen, int wrap)
{
	pen->wrap = wrap;
};

void ddiSetPenAlignment(DDIPen *pen, int alignment)
{
	pen->align = alignment;
};

void ddiSetPenSpacing(DDIPen *pen, int letterSpacing, int lineHeight)
{
	pen->letterSpacing = letterSpacing;
	pen->lineHeight = lineHeight;
};

/**
 * Calculate the size of the surface that will fit the text segment. If the text needs to be wrapped, 'nextEl' will point
 * to the location at which the rendering is to be stopped, and the rest of the text is to be rendered on another line.
 * If a newline character is in the text, the same happens. Otherwise, 'nextEl' is set to NULL.
 * If 'nextEl' is set, the character it points to must be IGNORED.
 */ 
static int calculateSegmentSize(DDIPen *pen, const char *text, int *width, int *height, int *offsetX, int *offsetY, const char **nextEl)
{
	FT_Error error;
	int penX=0, penY=0;
	int minX=0, maxX=0, minY=0, maxY=0;
	
	const char *lastWordEnd = NULL;
	int lastWordWidth = 0;
	int lastWordHeight = 0;
	int lastWordOffX = 0;
	int lastWordOffY = 0;
	
	while (1)
	{
		const char *chptr = text;
		long point = ddiReadUTF8(&text);
		if (point == 0)
		{
			break;
		};
		
		if (pen->mask == 1)
		{
			point = '*';
		}
		else if (pen->mask != 0)
		{
			point = pen->mask;
		};

		if (point == ' ')
		{
			lastWordEnd = chptr;
			lastWordWidth = maxX - minX;
			lastWordHeight = maxY - minY;
			lastWordOffX = 0;
			lastWordOffY = 0;
			
			if (minX < 0) lastWordOffX = -minX;
			if (minY < 0) lastWordOffY = -minY;
		};
		
		if (point == '\n')
		{
			// break the line here
			*nextEl = chptr;
			*width = maxX - minX;
			*height = maxY - minY;
			*offsetX = 0;
			*offsetY = 0;
			
			if (minX < 0) *offsetX = -minX;
			if (minY < 0) *offsetY = -minY;
			return 0;
		};
		
		if (point == '\t')
		{
			penX = penX/DDI_TAB_LEN*DDI_TAB_LEN + DDI_TAB_LEN;
			if (penX > maxX) maxX = penX;
		}
		else
		{
			FT_UInt glyph = FT_Get_Char_Index(pen->font->face, point);
			error = FT_Load_Glyph(pen->font->face, glyph, FT_LOAD_DEFAULT);
			if (error != 0)
			{
				return -1;
			};
	
			error = FT_Render_Glyph(pen->font->face->glyph, FT_RENDER_MODE_NORMAL);
			if (error != 0)
			{
				return -1;
			};
		
			FT_Bitmap *bitmap = &pen->font->face->glyph->bitmap;
		
			int left = penX + pen->font->face->glyph->bitmap_left;
			int top = penY - pen->font->face->glyph->bitmap_top;
			if (left < minX) minX = left;
			if (top < minY) minY = top;

			penX += (pen->font->face->glyph->advance.x >> 6) + pen->letterSpacing;
			penY += pen->font->face->glyph->advance.y >> 6;

			int right = left + (pen->font->face->glyph->advance.x >> 6) + pen->letterSpacing;
			int bottom = top + bitmap->rows;
			if (right > maxX) maxX = right;
			if (bottom > maxY) maxY = bottom;
		};
		
		if ((pen->currentLine->currentWidth + (maxX-minX)) > pen->width)
		{
			if (pen->wrap)
			{
				// we must wrap around at the last word that fit in
				// TODO: mfw nothing was written yet
				*nextEl = lastWordEnd;
				*width = lastWordWidth;
				*height = lastWordHeight;
				*offsetX = lastWordOffX;
				*offsetY = lastWordOffY;
				return 0;
			};
		};
	};
	
	if (minX < 0)
	{
		*offsetX = -minX;
	}
	else
	{
		*offsetX = 0;
	};
	
	if (minY < 0)
	{
		*offsetY = -minY;
	}
	else
	{
		*offsetY = 0;
	};
	
	*width = maxX - minX;
	*height = maxY - minY;
	*nextEl = NULL;
	
	return 0;
};

void ddiWritePen(DDIPen *pen, const char *text)
{
	if (*text == 0) return;
	const char *nextEl;
	int offsetX, offsetY, width, height;
	
	while (1)
	{
		if (calculateSegmentSize(pen, text, &width, &height, &offsetX, &offsetY, &nextEl) != 0)
		{
			break;
		};
		
		int *widths = NULL;
		size_t numChars = 0;
		
		// create a segment object for this
		size_t textSize;
		if (nextEl == NULL)
		{
			textSize = strlen(text);
		}
		else
		{
			textSize = nextEl - text;
		};
		
		const char *textEnd = &text[textSize];
		
		DDISurface *surface = ddiCreateSurface(&pen->format, width, height, NULL, 0);
		DDIColor fillColor;
		memcpy(&fillColor, &pen->foreground, sizeof(DDIColor));
		fillColor.alpha = 0;
		ddiFillRect(surface, 0, 0, width, height, &fillColor);
		
		if (height > pen->currentLine->maxHeight)
		{
			pen->currentLine->maxHeight = height;
		};
		
		int penX = 0, penY = 0;
		int firstCharPos = pen->writePos;
		while (text != textEnd)
		{
			long point = ddiReadUTF8(&text);
			if (pen->mask == 1)
			{
				point = '*';
			}
			else if (pen->mask != 0)
			{
				point = pen->mask;
			};

			if (point == '\t')
			{
				int oldPenX = penX;
				penX = penX/DDI_TAB_LEN*DDI_TAB_LEN + DDI_TAB_LEN;

				pen->writePos++;
				widths = (int*) realloc(widths, sizeof(int)*(numChars+1));
				widths[numChars++] = penX - oldPenX;
			}
			else
			{
				FT_UInt glyph = FT_Get_Char_Index(pen->font->face, point);
				FT_Error error = FT_Load_Glyph(pen->font->face, glyph, FT_LOAD_DEFAULT);
				if (error != 0) break;
				error = FT_Render_Glyph(pen->font->face->glyph, FT_RENDER_MODE_NORMAL);
				if (error != 0) break;
			
				FT_Bitmap *bitmap = &pen->font->face->glyph->bitmap;
			
				if (pen->writePos == pen->cursorPos)
				{
					fillColor.alpha = 255;
					ddiFillRect(surface, penX+offsetX, 0, 1, surface->height, &fillColor);
				};
			
				pen->writePos++;
			
				int x, y;
				for (x=0; x<bitmap->width; x++)
				{
					for (y=0; y<bitmap->rows; y++)
					{
						if (bitmap->buffer[y * bitmap->pitch + x] != 0)
						{
							fillColor.alpha = bitmap->buffer[y * bitmap->pitch + x];
							ddiFillRect(surface, penX+offsetX+x+pen->font->face->glyph->bitmap_left,
								penY+offsetY+y-pen->font->face->glyph->bitmap_top, 1, 1, &fillColor);
						};
					};
				};
			
				widths = (int*) realloc(widths, sizeof(int)*(numChars+1));
				widths[numChars++] = (pen->font->face->glyph->advance.x >> 6) + pen->letterSpacing;
			
				penX += (pen->font->face->glyph->advance.x >> 6) + pen->letterSpacing;
				penY += pen->font->face->glyph->advance.y >> 6;
			};
		};
		
		pen->currentLine->currentWidth += width;
		
		//DDISurface *finalSurface = ddiCreateSurface(&surface->format, width, height, NULL, 0);
		//fillColor.alpha = 0;
		//ddiFillRect(finalSurface, 0, 0, width, height, &fillColor);
		//ddiBlit(surface, 0, 0, finalSurface, 0, 0, width, height);
		//ddiDeleteSurface(surface);
		
		PenSegment *seg = (PenSegment*) malloc(sizeof(PenSegment));
		seg->surface = surface;
		seg->next = NULL;
		seg->numChars = numChars;
		seg->widths = widths;
		seg->firstCharPos = firstCharPos;
		seg->baselineY = offsetY;
		memcpy(&seg->background, &pen->background, sizeof(DDIColor));
		
		if ((surface->height-offsetY) > pen->currentLine->baselineY)
		{
			pen->currentLine->baselineY = surface->height - offsetY;
		};
		
		// add the segment to the end of the list for the current line
		if (pen->currentLine->firstSegment == NULL)
		{
			pen->currentLine->firstSegment = seg;
		}
		else
		{
			PenSegment *last = pen->currentLine->firstSegment;
			while (last->next != NULL) last = last->next;
			last->next = seg;
		};
		
		pen->currentLine->alignment = pen->align;
		pen->currentLine->lineHeight = pen->lineHeight;
		
		if (nextEl == NULL)
		{
			// we are done
			break;
		}
		else
		{
			widths = (int*) realloc(widths, sizeof(int)*(numChars+1));
			widths[numChars++] = 0;
			pen->writePos++;
			seg->widths = widths;
			seg->numChars = numChars;
			
			text = nextEl+1;
			PenLine *line = (PenLine*) malloc(sizeof(PenLine));
			line->firstSegment = NULL;
			line->maxHeight = pen->font->size;
			line->currentWidth = 0;
			line->baselineY = 0;
			line->next = NULL;
			pen->currentLine->next = line;
			pen->currentLine = line;
		};
	};
};

void ddiExecutePen(DDIPen *pen, DDISurface *surface)
{
	PenLine *line;
	PenSegment *seg;
	
	int drawY = pen->y;
	int lastDrawY = drawY;
	int lastDrawX = pen->x;
	int lastHeight = 12;
	for (line=pen->firstLine; line!=NULL; line=line->next)
	{
		line->drawY = drawY;

		int drawX;
		if (line->alignment == DDI_ALIGN_LEFT)
		{
			drawX = pen->x;
		}
		else if (line->alignment == DDI_ALIGN_CENTER)
		{
			drawX = pen->x + (pen->width / 2) - (line->currentWidth / 2);
		}
		else
		{
			drawX = pen->x + pen->width - line->currentWidth;
		};

		lastDrawY = drawY;
		int baselineY = drawY + line->maxHeight - line->baselineY;
		for (seg=line->firstSegment; seg!=NULL; seg=seg->next)
		{
			int plotY = baselineY - seg->baselineY;
			lastDrawY = plotY;
			seg->drawX = drawX;
			seg->drawY = plotY;
			
			if (seg->background.alpha != 0)
			{
				ddiFillRect(surface, drawX, drawY, seg->surface->width, line->maxHeight, &seg->background);
			};
			
			ddiBlit(seg->surface, 0, 0, surface, drawX, plotY, seg->surface->width, seg->surface->height);
			drawX += seg->surface->width;
			lastHeight = seg->surface->height;
		};
		
		lastDrawX = drawX;
		drawY += line->maxHeight * line->lineHeight / 100;
	};
	
	if (pen->writePos == pen->cursorPos)
	{
		DDIColor color = {0, 0, 0, 0xFF};
		ddiFillRect(surface, lastDrawX, lastDrawY, 1, lastHeight, &color);
	};
};

void ddiSetPenBackground(DDIPen *pen, DDIColor *bg)
{
	memcpy(&pen->background, bg, sizeof(DDIColor));
};

void ddiSetPenColor(DDIPen *pen, DDIColor *fg)
{
	memcpy(&pen->foreground, fg, sizeof(DDIColor));
};

void ddiGetPenSize(DDIPen *pen, int *widthOut, int *heightOut)
{
	PenLine *line;
	
	int height = 0;
	int width = 0;
	for (line=pen->firstLine; line!=NULL; line=line->next)
	{
		if (line->currentWidth > width) width = line->currentWidth;
		height += line->maxHeight * line->lineHeight / 100;
	};
	
	*widthOut = width;
	*heightOut = height;
};

void ddiSetPenPosition(DDIPen *pen, int x, int y)
{
	pen->x = x;
	pen->y = y;
};

int ddiPenCoordsToPos(DDIPen *pen, int x, int y)
{
	if ((x < 0) || (y < 0))
	{
		return -1;
	};
	
	PenLine *line;
	PenSegment *seg;

	for (line=pen->firstLine; line!=NULL; line=line->next)
	{
		if ((y < line->drawY) || (y >= (line->drawY+line->maxHeight)))
		{
			if (line->next != NULL)
			{
				continue;
			};
		};
		
		if (line->firstSegment != NULL)
		{
			if (x < line->firstSegment->drawX)
			{
				return line->firstSegment->firstCharPos;
			};
		};
		
		int bestBet = pen->writePos;
		for (seg=line->firstSegment; seg!=NULL; seg=seg->next)
		{
			int endX = seg->drawX + seg->surface->width;
			bestBet = seg->firstCharPos + (int)seg->numChars;
			
			if ((x >= seg->drawX) && (x < endX))
			{
				// it's in this segment!
				int offX = x - seg->drawX;
				
				int i;
				for (i=0; seg->widths[i]<offX; i++)
				{
					if ((size_t)i == seg->numChars)
					{
						fprintf(stderr, "DDI WARNING: Assertion failed in ddiPenCoordsToPen!");
						return -1;
					};
					
					offX -= seg->widths[i];
				};
				
				if (seg->widths[i]/2 <= offX)
				{
					i++;
				};
				
				return seg->firstCharPos + i;
			};
		};
		
		return bestBet;
	};
	
	return 0;
};

void ddiPenSetMask(DDIPen *pen, long mask)
{
	pen->mask = mask;
};

DDISurface* ddiRenderText(DDIPixelFormat *format, DDIFont *font, const char *text, const char **error)
{
	DDIPen *pen = ddiCreatePen(format, font, 0, 0, 100, 100, 0, 0, error);
	if (pen == NULL) return NULL;
	
	ddiSetPenWrap(pen, 0);
	ddiWritePen(pen, text);
	
	int width, height;
	ddiGetPenSize(pen, &width, &height);

	DDI_ERROR("Failed to create surface");
	DDISurface *surface = ddiCreateSurface(format, width, height, NULL, 0);
	if (surface != NULL)
	{
		static DDIColor transparent = {0, 0, 0, 0};
		ddiFillRect(surface, 0, 0, width, height, &transparent);
		ddiExecutePen(pen, surface);
	};
	ddiDeletePen(pen);
	
	return surface;
};