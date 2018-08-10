
#include "tiles_generic.h"
#include "math.h"

#define TABLE_SIZE  0x10000 // excessive?

struct vector_line {
	INT32 x;
	INT32 y;
	INT32 color;
	UINT8 intensity;
};

static struct vector_line *vector_table;
struct vector_line *vector_ptr; // pointer
static INT32 vector_cnt;
static UINT32 *pBitmap = NULL;
static UINT32 *pPalette = NULL;

static float vector_scaleX      = 1.00;
static float vector_scaleY      = 1.00;
static INT32 vector_offsetX     = 0;
static INT32 vector_offsetY     = 0;
static float vector_gamma_corr  = 1.2;
static float vector_intens      = 1.0;
static INT32 vector_antialias   = 1;
static INT32 vector_beam        = 0x00010000; // 16.16 beam width

#define CLAMP8(x) do { if (x > 0xff) x = 0xff; } while (0)

static UINT8 gammaLUT[256];

void vector_set_gamma(float gamma_corr)
{
	for (INT32 i = 0; i < 256; i++)	{
		INT32 gamma = pow((float)i / 255.0, 1.0 / vector_gamma_corr) * (float)((1 << 8) - 1) + 0.5;
		CLAMP8(gamma);
		gammaLUT[i] = gamma;
	}
}

static inline INT32 divop(INT32 parm1, INT32 parm2)
{
    if (parm2 & 0xfffff000)	{
		parm1 = (parm1 << 4) / (parm2 >> 12);
		if (parm1 > 0x00010000)
			return 0x00010000;
		else if (parm1 < -0x00010000)
			return -0x00010000;
		return parm1;
	}
	return 0x00010000;
}

void vector_set_offsets(INT32 x, INT32 y)
{
	vector_offsetX = x;
	vector_offsetY = y;
}

void vector_set_scale(INT32 x, INT32 y)
{
	if (x == 0 || x == -1)
		vector_scaleX = 1.00;
	if (y == 0 || y == -1)
		vector_scaleY = 1.00;

	vector_scaleX = (float)nScreenWidth / x;
	vector_scaleY = (float)nScreenHeight / y;
}

void vector_add_point(INT32 x, INT32 y, INT32 color, INT32 intensity)
{
	if (vector_cnt + 1 > (TABLE_SIZE - 2)) return;
	vector_ptr->x = (vector_antialias == 0) ? ((x + 0x8000) >> 16) : x;
	vector_ptr->y = (vector_antialias == 0) ? ((y + 0x8000) >> 16) : y;
	vector_ptr->color = color;

	intensity *= vector_intens; // intensity correction
	CLAMP8(intensity);
	vector_ptr->intensity = (vector_antialias == 0) ? gammaLUT[intensity] : intensity;

	vector_cnt++;
	vector_ptr++;
	vector_ptr->color = -1; // mark it as the last one to save some cycles later...
}

static inline void vector_draw_pixel(INT32 x, INT32 y, INT32 pixel)
{
	pixel = pPalette[pixel];
	INT32 coords = y * nScreenWidth + x;
	if (x >= 0 && x < nScreenWidth && y >= 0 && y < nScreenHeight)
	{
		UINT32 d = pBitmap[coords];

		if (d) { // if something is already there, mix it.
			INT32 r = ((d >> 16) & 0xff) + ((pixel >> 16) & 0xff);
			INT32 g = ((d >>  8) & 0xff) + ((pixel >>  8) & 0xff);
			INT32 b = (d & 0xff) + (pixel & 0xff);
			CLAMP8(r); CLAMP8(g); CLAMP8(b);

			pBitmap[coords] = (r << 16) | (g << 8) | b;
		}
		else
		{
			pBitmap[y * nScreenWidth + x] = pixel;
		}
	}
}

static void lineSimple(INT32 x0, INT32 y0, INT32 x1, INT32 y1, INT32 color, INT32 intensity)
{
	color = color * 256 + intensity;
	UINT32 p = pPalette[color];
	if (p == 0) return; // safe to assume we can't draw black??
	INT32 straight = 0;

	if (x0 == x1 || y0 == y1) straight = 1;

	if (vector_antialias == 0) {
		// http://rosettacode.org/wiki/Bitmap/Bresenham%27s_line_algorithm

		INT32 dx = abs(x1 - x0);
		INT32 dy = abs(y1 - y0);
		INT32 sx = x0 < x1 ? 1 : -1;
		INT32 sy = y0 < y1 ? 1 : -1;
		INT32 err = (dx>dy ? dx : -dy)/2, e2;

		while (1)
		{
			vector_draw_pixel(x0, y0, color);

			if (x0 == x1 && y0 == y1) break;

			e2 = err;

			if (e2 >-dx) { err -= dy; x0 += sx; }
			if (e2 < dy) { err += dx; y0 += sy; }
		}
	} else { // anti-aliased
		INT32 dx = abs(x1 - x0);
		INT32 dy = abs(y1 - y0);
		INT32 width = vector_beam;
		INT32 sx, sy, a1, yy, xx;

		if (dx >= dy) {
			sx = x0 <= x1 ? 1 : -1;
			sy = divop(y1 - y0, dx);
			if (sy < 0)
				dy--;
			x0 >>= 16;
			xx = x1 >> 16;
			y0 -= width >> 1;

			while (1) {
				dx = width;
				dy = y0 >> 16;
				vector_draw_pixel(x0, dy++, (color & 0xff00) + gammaLUT[0xff - (0xff & (y0 >> 8))]);
				dx -= 0x10000 - (0xffff & y0);
				a1 = gammaLUT[(dx >> 8) & 0xff];
				dx >>= 16;
				while (dx--)
					vector_draw_pixel(x0, dy++, color);
				vector_draw_pixel(x0, dy, (color & 0xff00) + a1);
				if (x0 == xx) break;
				x0 += sx;
				y0 += sy;
			}
		} else {
			sy = y0 <= y1 ? 1 : -1;
			sx = divop(x1 - x0, dy);
			if (sx < 0)
				dx--;
			y0 >>= 16;
			yy = y1 >> 16;
			x0 -= width >> 1;

			while (1) {
				dy = width;
				dx = x0 >> 16;
				vector_draw_pixel(dx++, y0, (color & 0xff00) + gammaLUT[0xff - (0xff & (x0 >> 8))]);
				dy -= 0x10000 - (0xffff & x0);
				a1 = gammaLUT[(dy >> 8) & 0xff];
				dy >>= 16;
				while (dy--)
					vector_draw_pixel(dx++, y0, color);
				vector_draw_pixel(dx, y0, (color & 0xff00) + a1);
				if (y0 == yy) break;
				y0 += sy;
				x0 += sx;
			}
		}
	}
}

void draw_vector(UINT32 *palette)
{
	struct vector_line *ptr = &vector_table[0];

	INT32 prev_x = 0, prev_y = 0;

	memset (pBitmap, 0, nScreenWidth * nScreenHeight * sizeof(INT32));
	pBurnDrvPalette = pPalette = palette;

	for (INT32 i = 0; i < vector_cnt && i < TABLE_SIZE; i++, ptr++)
	{
		if (ptr->color == -1) break;

		INT32 curr_y = (ptr->y + vector_offsetY) * vector_scaleY;
		INT32 curr_x = (ptr->x + vector_offsetX) * vector_scaleX;

		if (ptr->intensity != 0) { // intensity 0 means turn off the beam...
			lineSimple(curr_x, curr_y, prev_x, prev_y, ptr->color, ptr->intensity);
		}

		prev_x = curr_x;
		prev_y = curr_y;
	}

	// copy to the screen, only draw pixels that aren't black
	// should be safe for any bit depth with putpix
	{
		memset (pBurnDraw, 0, nScreenWidth * nScreenHeight * nBurnBpp);

		for (INT32 i = 0; i < nScreenWidth * nScreenHeight; i++)
		{
			UINT32 p = pBitmap[i];
			
			if (p) {
				PutPix(pBurnDraw + i * nBurnBpp, BurnHighCol((p >> 16) & 0xff, (p >> 8) & 0xff, p & 0xff, 0));
			}
		}
	}
}

void vector_reset()
{
	vector_cnt = 0;
	vector_ptr = &vector_table[0];
	vector_ptr->color = -1;
}

void vector_init()
{
	GenericTilesInit();
	
	pBitmap = (UINT32*)BurnMalloc(nScreenWidth * nScreenHeight * sizeof(INT32));

	vector_table = (struct vector_line*)BurnMalloc(TABLE_SIZE * sizeof(vector_line));

	memset (vector_table, 0, TABLE_SIZE * sizeof(vector_line));

	vector_set_scale(-1, -1); // default 1x
	vector_set_offsets(0, 0);
	vector_set_gamma(vector_gamma_corr);

	vector_reset();
}

void vector_exit()
{
	GenericTilesExit();
	
	if (pBitmap) {
		BurnFree (pBitmap);
	}

	pPalette = NULL;

	BurnFree (vector_table);
	vector_ptr = NULL;
}

INT32 vector_scan(INT32 nAction)
{
	struct BurnArea ba;

	if (nAction & ACB_VOLATILE) {
		memset(&ba, 0, sizeof(ba));

		ba.Data   = (UINT8*)vector_table;
		ba.nLen   = TABLE_SIZE * sizeof(vector_line);
		ba.szName = "Vector Table";
		BurnAcb(&ba);

		SCAN_VAR(vector_cnt);
	}

	if (nAction & ACB_WRITE) {
		vector_ptr = &vector_table[vector_cnt];
	}

	return 0;
}
