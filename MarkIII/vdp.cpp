// 【最適化】Arduinoのesp32コアは既定で -Os。renderBGのピクセル本体は
// 4つの8bit値に対する24回のシフト/マスク/OR で、-Os はまさにここで必要な
// スケジューリングとレジスタ割り付けの妥協を抑制してしまう。
// 8倍アンロールはテキストで展開済みなので -Os でも効いており、これは
// ループ構造ではなく「本体」の話。
// ※【注意】このファイルは IRAM_ATTR が23個あり、O3でコードが膨らむと
//   リンク時に IRAM 溢れ("region `iram0_0_seg' overflowed")になる可能性がある。
//   その場合はこの1行を消し、代わりに renderBG と renderSprites の定義に
//   __attribute__((optimize("O3"))) を個別に付けること。
#pragma GCC optimize("O3")

#include "vdp.h"
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <esp_attr.h>
#include <string.h>

extern bool spriteHorizontalUnlimited;
extern fabgl::VGAController DisplayController;

static const uint8_t tms9918a_fixed_palette[16] = {
	0x00,
	0x00,
	0x08,
	0x0C,
	0x20,
	0x30,
	0x02,
	0x3C,
	0x03,
	0x17,
	0x0A,
	0x1F,
	0x04,
	0x33,
	0x2A,
	0x3F
};

static void buildBgTables();

VDP::VDP(uint8_t *vram_buf, uint8_t *cram_buf, uint8_t *cramRGB_buf, uint8_t *reg_buf) {
	vram = vram_buf;
	cram = cram_buf;
	cramRGB = cramRGB_buf;
	reg_ = reg_buf;
	active_raster = rasterbuffer0;
	visible_raster = rasterbuffer1;
	buildBgTables();
	reset();
}

void VDP::reset() {
	memset(vram, 0, 16384);
	memset(cram, 0, 32);
	memset(cramRGB, 0, 32);
	memset(reg_, 0, 16);

	memset(sprite_line_count, 0, sizeof(sprite_line_count));
	memset(sprite_line_list, 0, sizeof(sprite_line_list));
	memset(rasterbuffer0, 0, sizeof(rasterbuffer0));
	memset(rasterbuffer1, 0, sizeof(rasterbuffer1));
	active_raster = rasterbuffer0;
	visible_raster = rasterbuffer1;

	vaddr_ = 0;
	vcode_ = 0;
	vlatch_ = 0;
	vcmd_ = false;
	rbuf_ = 0;
	status_ = 0;
	vscroll_ = 0;
	linecnt_ = 0xFF;
	vint_pending_ = false;
	hint_pending_ = false;

	memcpy(cram, tms9918a_fixed_palette, 16);
	memcpy(cramRGB, tms9918a_fixed_palette, 16);
}

void IRAM_ATTR VDP::writeData(uint8_t v) {
	vcmd_ = false;
	if (vcode_ == 3) setCRAM(vaddr_ & 0x1F, v);
	else             vram[vaddr_ & 0x3FFF] = v;
	vaddr_ = (vaddr_ + 1) & 0x3FFF;
	rbuf_ = v;
}

uint8_t IRAM_ATTR VDP::readData() {
	vcmd_ = false;
	uint8_t ret = rbuf_;
	rbuf_ = vram[vaddr_ & 0x3FFF];
	vaddr_ = (vaddr_ + 1) & 0x3FFF;
	return ret;
}

void IRAM_ATTR VDP::writeControl(uint8_t v) {
	if (!vcmd_) {
		vlatch_ = v;
		vaddr_ = (vaddr_ & 0x3F00) | v;
		vcmd_ = true;
		return;
	}
	vcmd_ = false;
	vcode_ = (v >> 6) & 0x03;
	vaddr_ = ((uint16_t)(v & 0x3F) << 8) | vlatch_;

	switch (vcode_) {
		case 0:
			rbuf_ = vram[vaddr_ & 0x3FFF];
			vaddr_ = (vaddr_ + 1) & 0x3FFF;
			break;
		case 2:
			{
				uint8_t rn = v & 0x0F;
				if (rn < 11) {
					reg_[rn] = vlatch_;
				}
			}
			break;
		default:
			break;
	}
}

uint8_t IRAM_ATTR VDP::readControl() {
	uint8_t ret = status_;
	status_ = 0;
	vint_pending_ = false;
	hint_pending_ = false;
	return ret;
}

#define BG_SCRATCH_W (33 * 8)
static uint32_t s_nib_fwd[16], s_nib_rev[16];
static uint8_t  s_bgpx[BG_SCRATCH_W] __attribute__((aligned(4)));
static uint8_t  s_bgin[BG_SCRATCH_W] __attribute__((aligned(4)));

static void buildBgTables() {
	for (int n = 0; n < 16; n++) {
		uint32_t f = 0, r = 0;
		for (int k = 0; k < 4; k++) {
			if ((n >> (3 - k)) & 1) f |= 1u << (8 * k);
			if ((n >> k) & 1)       r |= 1u << (8 * k);
		}
		s_nib_fwd[n] = f;
		s_nib_rev[n] = r;
	}
}

int IRAM_ATTR VDP::renderLineScratch(int y) {
	if (!(reg_[1] & 0x40)) {
		uint8_t bg = cramRGB[16 + (reg_[7] & 0x0F)];
		memset(s_bgpx, bg, SCREEN_W);
		return 0;
	}

	int fine = renderBG(y, s_bgpx, s_bgin);
	uint8_t *px = s_bgpx + fine;
	renderSprites(y, px, s_bgin + fine);

	if (reg_[0] & 0x20) {
		memset(px, 0, 8);
	}
	return fine;
}

void IRAM_ATTR VDP::renderLine(int y, uint8_t *line_buf) {
	int fine = renderLineScratch(y);
	memcpy(line_buf, s_bgpx + fine, SCREEN_W);
}

int IRAM_ATTR VDP::renderBG(int y, uint8_t *px, uint8_t *bginfo) {
	const uint16_t nt = ntBase();
	const int scx = ((reg_[0] & 0x40) && y < 16) ? 0 : reg_[8];

	int bg0 = (256 - scx) & 255;
	int fine = bg0 & 7;
	int tcol0 = bg0 >> 3;

	const uint8_t *vram_ptr = vram;
	const uint32_t *F = s_nib_fwd;
	const uint32_t *R = s_nib_rev;
	int current_scy_base = vscroll_ % 224;
	bool v_lock = (reg_[0] & 0x80);
	const int lock_tc = fine ? 25 : 24;
	const bool small_nt = !(reg_[2] & 0x01);

	uint32_t *opx = (uint32_t *)px;
	uint32_t *obi = (uint32_t *)bginfo;

	for (int tc = 0; tc <= 32; tc++, opx += 2, obi += 2) {
		int col = (tcol0 + tc) & 31;
		int current_scy = (v_lock && tc >= lock_tc) ? 0 : current_scy_base;
		int sum_y = y + current_scy;
		int eff_y = (sum_y >= 224) ? sum_y - 224 : sum_y;
		int row = eff_y >> 3;
		if (small_nt) row &= ~0x10;
		int tile_y = eff_y & 7;
		uint16_t na = (nt + (row * 32 + col) * 2) & 0x3FFF;
		uint16_t n_val = *(const uint16_t *)&vram_ptr[na];
		uint16_t tile = n_val & 0x01FF;
		bool hflip = n_val & 0x0200;
		bool vflip = n_val & 0x0400;
		const uint8_t *pal = &cramRGB[(n_val & 0x0800) ? 16 : 0];
		uint32_t pri4 = (n_val & 0x1000) ? 0x10101010u : 0u;
		int ty = vflip ? (7 - tile_y) : tile_y;
		uint16_t ta = ((tile << 5) | (ty << 2)) & 0x3FFF;
		uint32_t b32 = *(const uint32_t *)&vram_ptr[ta];

		uint32_t h0 = (b32 >>  4) & 15, l0 =  b32        & 15;
		uint32_t h1 = (b32 >> 12) & 15, l1 = (b32 >>  8) & 15;
		uint32_t h2 = (b32 >> 20) & 15, l2 = (b32 >> 16) & 15;
		uint32_t h3 = (b32 >> 28),      l3 = (b32 >> 24) & 15;

		uint32_t c0, c1;
		if (!hflip) {
			c0 = F[h0] | (F[h1] << 1) | (F[h2] << 2) | (F[h3] << 3);
			c1 = F[l0] | (F[l1] << 1) | (F[l2] << 2) | (F[l3] << 3);
		} else {
			c0 = R[l0] | (R[l1] << 1) | (R[l2] << 2) | (R[l3] << 3);
			c1 = R[h0] | (R[h1] << 1) | (R[h2] << 2) | (R[h3] << 3);
		}

		obi[0] = c0 | pri4;
		obi[1] = c1 | pri4;
		opx[0] =  (uint32_t)pal[ c0        & 15]
		       | ((uint32_t)pal[(c0 >>  8) & 15] <<  8)
		       | ((uint32_t)pal[(c0 >> 16) & 15] << 16)
		       | ((uint32_t)pal[ c0 >> 24      ] << 24);
		opx[1] =  (uint32_t)pal[ c1        & 15]
		       | ((uint32_t)pal[(c1 >>  8) & 15] <<  8)
		       | ((uint32_t)pal[(c1 >> 16) & 15] << 16)
		       | ((uint32_t)pal[ c1 >> 24      ] << 24);
	}
	return fine;
}

void IRAM_ATTR VDP::renderSprites(int y, uint8_t *px, uint8_t *bginfo) {
	const uint16_t sat = satBase();
	const uint16_t spb = sprBase();
	const bool tall = (reg_[1] & 0x02) != 0;
	const int sph = tall ? 16 : 8;
	const bool xshift = (reg_[0] & 0x08) != 0;

	int count = 0;
	int jobs_sx[64];
	uint8_t jobs_tile[64];
	int jobs_line[64];
	int job_count = 0;

	const uint8_t *vram_ptr = vram;
	uint16_t sat_masked = sat & 0x3FFF;

	for (int s = 0; s < 64; s++) {
		int sy = vram_ptr[(sat_masked + s) & 0x3FFF];
		if (sy == 0xD0) break;

		int line = (uint8_t)(y - sy - 1);
		if (line >= sph) continue;

		if (!spriteHorizontalUnlimited && ++count > 8) {
			status_ |= 0x40;
			break;
		}

		uint16_t attr = (sat_masked + 128 + s * 2) & 0x3FFF;
		uint16_t attr_val = *(const uint16_t *)&vram_ptr[attr];
		int sx = (attr_val & 0xFF) - (xshift ? 8 : 0);
		uint8_t tile = (attr_val >> 8) & 0xFF;

		jobs_sx[job_count] = sx;
		jobs_tile[job_count] = tile;
		jobs_line[job_count] = line;
		job_count++;
	}

	const uint8_t *sprite_cram = &cramRGB[16];
	const uint32_t *F = s_nib_fwd;

	uint32_t occ[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
	bool coll = false;

	for (int i = 0; i < job_count; i++) {
		int sx = jobs_sx[i];
		uint8_t tile = jobs_tile[i];
		int local_y = jobs_line[i];

		if (tall) tile &= 0xFE;

		if (tall && local_y >= 8) {
			tile++;
			local_y -= 8;
		}

		uint16_t ta = (spb | (tile << 5) | (local_y << 2)) & 0x3FFF;

		uint32_t b32 = *(const uint32_t *)&vram_ptr[ta];
		uint32_t c0 = F[(b32 >> 4) & 15] | (F[(b32 >> 12) & 15] << 1) | (F[(b32 >> 20) & 15] << 2) | (F[b32 >> 28] << 3);
		uint32_t c1 = F[b32 & 15] | (F[(b32 >> 8) & 15] << 1) | (F[(b32 >> 16) & 15] << 2) | (F[(b32 >> 24) & 15] << 3);
		if (!(c0 | c1)) continue;

		for (int k = 0; k < 8; k++) {
			uint32_t idx = (k < 4 ? (c0 >> (8 * k)) : (c1 >> (8 * (k - 4)))) & 15;
			int x = sx + k;
			if (!idx || (unsigned)x >= SCREEN_W) continue;
			uint32_t bit = 0x80000000u >> (x & 31);
			uint32_t &w = occ[x >> 5];
			if (w & bit) { coll = true; continue; }
			w |= bit;
			uint8_t bgi = bginfo[x];
			if (!((bgi & 0x10) && (bgi & 0x0F) != 0)) px[x] = sprite_cram[idx];
		}
	}
	if (coll) status_ |= 0x20;
}

static uint8_t **s_direct_rows = nullptr;
static uint32_t  s_direct_sync = 0;

void VDP::setDirectOutput(uint8_t **rows, uint8_t syncBits) {
	s_direct_sync = (uint32_t)(syncBits & 0xC0) * 0x01010101u;
	s_direct_rows = rows;
}

void IRAM_ATTR VDP::renderSingleLine(int y) {
	if (y < 0 || y >= SCREEN_H) return;

	uint8_t **rows = s_direct_rows;
	if (rows != nullptr) {
		int fine = renderLineScratch(y);
		const uint32_t *s = (const uint32_t *)(s_bgpx + (fine & ~3));
		uint32_t *d = (uint32_t *)rows[y];
		const uint32_t sync = s_direct_sync;
		const int sh = (fine & 3) * 8;
		if (sh == 0) {
			for (int k = 0; k < SCREEN_W / 4; k++) {
				uint32_t w = (s[k] & 0x3F3F3F3Fu) | sync;
				d[k] = (w >> 16) | (w << 16);
			}
		} else {
			uint32_t a = s[0];
			for (int k = 0; k < SCREEN_W / 4; k++) {
				uint32_t b = s[k + 1];
				uint32_t w = (((a >> sh) | (b << (32 - sh))) & 0x3F3F3F3Fu) | sync;
				d[k] = (w >> 16) | (w << 16);
				a = b;
			}
		}
		return;
	}

	if (active_raster != nullptr) {
		uint8_t *line_buf = &active_raster[y * SCREEN_W];
		renderLine(y, line_buf);
	}
}

void IRAM_ATTR VDP::scanlineCheck(int y) {
	if (y >= 192) vscroll_ = reg_[9];

	if (y <= 192) {
		if (linecnt_ == 0) {
			linecnt_ = reg_[10];
			hint_pending_ = true;
		} else {
			linecnt_--;
		}
	} else {
		linecnt_ = reg_[10];
	}
	if (y == 192) {
		status_ |= 0x80;
		vint_pending_ = true;
	}
}

void IRAM_ATTR VDP::transferFrameToDisplay() {
}

inline uint32_t VDP::expand_bits_to_color(uint8_t nibble, uint8_t color) {
	uint32_t res = 0;
	if (nibble & 0x08) res |= (uint32_t)color;
	if (nibble & 0x04) res |= (uint32_t)color << 8;
	if (nibble & 0x02) res |= (uint32_t)color << 16;
	if (nibble & 0x01) res |= (uint32_t)color << 24;
	return res;
}

inline uint32_t VDP::expand_mag_bits_to_color(uint8_t two_bits, uint8_t color) {
	uint32_t res = 0;
	if (two_bits & 0x02) {
		res |= (uint32_t)color;
		res |= (uint32_t)color << 8;
	}
	if (two_bits & 0x01) {
		res |= (uint32_t)color << 16;
		res |= (uint32_t)color << 24;
	}
	return res;
}

IRAM_ATTR void VDP::tms9918a_render_slice(int y, uint8_t *sprat, uint16_t bits, unsigned int width) {
	int x = sprat[1];
	if (sprat[3] & 0x80) x -= 32;

	int mag = reg_[1] & 0x01;
	int real_width = mag ? (width << 1) : width;
	uint8_t foreground = cram[sprat[3] & 0x0F];

	if (width == 8) bits &= 0xFF00;
	uint8_t *out = &active_raster[256 * y];

	if (!mag) {
		for (int i = 0; i < (int)width; i += 4) {
			int target_x = x + i;
			uint8_t nibble = (uint8_t)((bits >> (12 - i)) & 0x0F);
			if (nibble == 0) continue;

			if (target_x >= 0 && target_x <= 252) {
				if (nibble == 0x0F) {
					out[target_x + 0] = foreground;
					out[target_x + 1] = foreground;
					out[target_x + 2] = foreground;
					out[target_x + 3] = foreground;
				} else {
					for (int b = 0; b < 4; b++) {
						if ((nibble >> (3 - b)) & 1) out[target_x + b] = foreground;
					}
				}
			} else {
				for (int b = 0; b < 4; b++) {
					if ((nibble >> (3 - b)) & 1) {
						int tx = target_x + b;
						if (tx >= 0 && tx < 256) out[tx] = foreground;
					}
				}
			}
		}
	} else {
		for (int i = 0; i < (int)width; i += 2) {
			int target_x = x + (i * 2);
			uint8_t two_bits = (uint8_t)((bits >> (14 - i)) & 0x03);

			if (two_bits == 0) continue;
			if (target_x >= 0 && target_x <= (256 - 4)) {

				if (two_bits == 0x03) {
					uint8_t *p = out + target_x;
					p[0] = foreground;
					p[1] = foreground;
					p[2] = foreground;
					p[3] = foreground;
				} else {
					for (int b = 0; b < 2; b++) {
						if ((two_bits >> (1 - b)) & 1) {
							int tx = target_x + (b * 2);
							out[tx] = foreground;
							out[tx + 1] = foreground;
						}
					}
				}
			} else {
				for (int b = 0; b < 2; b++) {
					if ((two_bits >> (1 - b)) & 1) {
						int tx = target_x + (b * 2);
						if (tx >= 0 && tx < 256) out[tx] = foreground;
						if (tx + 1 >= 0 && tx + 1 < 256) out[tx + 1] = foreground;
					}
				}
			}
		}
	}
}

static inline __attribute__((always_inline)) uint16_t tmsSpriteBits(const uint8_t *reg, int y, const uint8_t *sprat, const uint8_t *spdat) {
	int row = *sprat;
	if (row > 0xE0)
		row -= 0x100;
	row += 1;
	row = y - row;
	if (reg[1] & 0x01)
		row >>= 1;
	if ((reg[1] & 0x02) == 0)
		return (uint16_t)(spdat[row] << 8);
	return (uint16_t)((spdat[row] << 8) | spdat[row + 16]);
}

static inline __attribute__((always_inline)) uint32_t tmsSpriteMask(uint16_t bits, int mag) {
	uint32_t b = bits;
	if (!mag) return b << 16;
	b = (b | (b << 8)) & 0x00FF00FFu;
	b = (b | (b << 4)) & 0x0F0F0F0Fu;
	b = (b | (b << 2)) & 0x33333333u;
	b = (b | (b << 1)) & 0x55555555u;
	return b | (b << 1);
}

IRAM_ATTR void VDP::tms9918a_render_sprite(int y, uint8_t *sprat, uint8_t *spdat) {
	unsigned int width = (reg_[1] & 0x02) ? 16 : 8;
	tms9918a_render_slice(y, sprat, tmsSpriteBits(reg_, y, sprat, spdat), width);
}

void VDP::prepare_sprites() {
	memset(sprite_line_count, 0, 192);
	uint8_t *sprat = vram + ((reg_[5] & 0x7F) << 7);
	int spheight = reg_[1] & 0x02 ? 16 : 8;
	int mag = reg_[1] & 0x01;
	if (mag) spheight <<= 1;

	for (int i = 0; i < 32; i++) {
		int ypos = sprat[i * 4];
		if (ypos == 0xD0) break;
		if (ypos > 0xE0) ypos -= 256;
		ypos += 1;

		int start_y = (ypos < 0) ? 0 : ypos;
		int end_y = (ypos + spheight > 192) ? 192 : ypos + spheight;

		for (int y = start_y; y < end_y; y++) {
			if (sprite_line_count[y] < 32) {
				sprite_line_list[y][sprite_line_count[y]++] = i;
			}
		}
	}
}

IRAM_ATTR void VDP::tms9918a_sprite_line(int y) {
	uint8_t *sprat_base = vram + ((reg_[5] & 0x7F) << 7);
	uint8_t *spdat = vram + ((reg_[6] & 0x07) << 11);
	unsigned int spshft = 3;
	uint8_t patMask = (reg_[1] & 0x02) ? 0xFC : 0xFF;
	int count = sprite_line_count[y];

	if (limit_sprites && count > 4) {
		if (!(status_ & 0x40))
			status_ = (status_ & 0xA0) | 0x40 | (sprite_line_list[y][4] & 0x1F);
		count = 4;
	}

	if (count > 1 && !(status_ & 0x20)) {
		uint32_t occ[9] = { 0 };
		int mag = reg_[1] & 0x01;
		for (int n = 0; n < count; n++) {
			const uint8_t *sprat = sprat_base + sprite_line_list[y][n] * 4;
			uint32_t m = tmsSpriteMask(tmsSpriteBits(reg_, y, sprat, spdat + ((sprat[2] & patMask) << spshft)), mag);
			int x = sprat[1] - ((sprat[3] & 0x80) ? 32 : 0);
			if (x < 0) { m = (x <= -32) ? 0 : (m << -x); x = 0; }
			else if (x > 224) m &= ~(0xFFFFFFFFu >> (256 - x));
			uint32_t w = (uint32_t)x >> 5, s = (uint32_t)x & 31;
			uint32_t p0 = m >> s, p1 = s ? m << (32 - s) : 0;
			if ((p0 & occ[w]) | (p1 & occ[w + 1])) { status_ |= 0x20; break; }
			occ[w] |= p0; occ[w + 1] |= p1;
		}
	}

	for (int n = count - 1; n >= 0; n--) {
		uint8_t i = sprite_line_list[y][n];
		uint8_t *sprat = sprat_base + (i * 4);
		uint8_t foreground = sprat[3] & 0x0F;
		if (foreground == 0) continue;

		tms9918a_render_sprite(y, sprat, spdat + ((sprat[2] & patMask) << spshft));
	}
}

IRAM_ATTR void VDP::tms9918a_raster_sprites() {
	for (unsigned int i = 0; i < 192; i++)
		tms9918a_sprite_line(i);
}

void IRAM_ATTR VDP::tms9918a_rasterize_g1() {
	uint8_t *p_base = vram + ((reg_[2] & 0x0F) << 10);
	uint8_t *pattern = vram + ((reg_[4] & 0x07) << 11);
	uint8_t *colour = vram + (reg_[3] << 6);
	uint8_t *out = active_raster;
	const uint8_t *const cm = cram;

	for (int char_row = 0; char_row < 24; char_row++) {
		for (int scanline = 0; scanline < 8; scanline++) {
			uint8_t *p_line = p_base + char_row * 32;
			for (int x = 0; x < 32; x++) {
				uint8_t code = *p_line++;
				uint8_t b = pattern[(code << 3) + scanline];
				uint8_t c = colour[code >> 3];
				uint8_t c_arr[2] = { cm[c & 0x0F], cm[c >> 4] };

				*((uint32_t *)out) = c_arr[(b >> 7) & 1] | (c_arr[(b >> 6) & 1] << 8) | (c_arr[(b >> 5) & 1] << 16) | (c_arr[(b >> 4) & 1] << 24);
				*((uint32_t *)(out + 4)) = c_arr[(b >> 3) & 1] | (c_arr[(b >> 2) & 1] << 8) | (c_arr[(b >> 1) & 1] << 16) | (c_arr[b & 1] << 24);
				out += 8;
			}
		}
	}
	tms9918a_raster_sprites();
}

IRAM_ATTR void VDP::tms9918a_rasterize_g2() {
	uint8_t *p_base = vram + ((reg_[2] & 0x0F) << 10);
	uint8_t *pattern0 = vram + ((reg_[4] & 0x04) << 11);
	uint8_t *colour0 = vram + ((reg_[3] & 0x80) << 6);
	uint8_t *out = active_raster;
	const uint8_t *const cm = cram;

	for (int char_row = 0; char_row < 24; char_row++) {
		uint8_t *pattern = pattern0;
		uint8_t *colour = colour0;

		if (char_row >= 8 && char_row < 16) {
			if (reg_[4] & 0x01) pattern += 0x0800;
			if (reg_[3] & 0x20) colour += 0x0800;
		} else if (char_row >= 16) {
			if (reg_[4] & 0x02) pattern += 0x1000;
			if (reg_[3] & 0x40) colour += 0x1000;
		}

		for (int scanline = 0; scanline < 8; scanline++) {
			uint8_t *p_line = p_base + char_row * 32;
			for (int x = 0; x < 32; x++) {
				uint8_t code = *p_line++;
				uint8_t b = pattern[(code << 3) + scanline];
				uint8_t c = colour[(code << 3) + scanline];
				uint8_t c_arr[2] = { cm[c & 0x0F], cm[c >> 4] };

				*((uint32_t *)out) = c_arr[(b >> 7) & 1] | (c_arr[(b >> 6) & 1] << 8) | (c_arr[(b >> 5) & 1] << 16) | (c_arr[(b >> 4) & 1] << 24);
				*((uint32_t *)(out + 4)) = c_arr[(b >> 3) & 1] | (c_arr[(b >> 2) & 1] << 8) | (c_arr[(b >> 1) & 1] << 16) | (c_arr[b & 1] << 24);
				out += 8;
			}
		}
	}
	tms9918a_raster_sprites();
}

void IRAM_ATTR VDP::tms9918a_rasterize_mc() {
	uint8_t *p_base = vram + ((reg_[2] & 0x0F) << 10);
	uint8_t *pattern = vram + ((reg_[4] & 0x07) << 11);
	uint8_t *out = active_raster;
	const uint8_t *const cm = cram;

	for (int char_row = 0; char_row < 24; char_row++) {
		for (int scanline = 0; scanline < 8; scanline++) {
			uint8_t *p_line = p_base + char_row * 32;
			int block_y = (scanline >> 2) & 1;
			int pat_offset = (char_row & 3) << 1;

			for (int x = 0; x < 32; x++) {
				uint8_t code = *p_line++;
				uint8_t c = pattern[(code << 3) + pat_offset + block_y];
				uint8_t col0 = cm[c >> 4];
				uint8_t col1 = cm[c & 0x0F];

				uint32_t p1 = col0 | (col0 << 8) | (col0 << 16) | (col0 << 24);
				uint32_t p2 = col1 | (col1 << 8) | (col1 << 16) | (col1 << 24);
				*((uint32_t *)out) = p1;
				*((uint32_t *)(out + 4)) = p2;
				out += 8;
			}
		}
	}
	tms9918a_raster_sprites();
}

void IRAM_ATTR VDP::tms9918a_rasterize_text() {
	uint8_t *p_base = vram + ((reg_[2] & 0x0F) << 10);
	uint8_t *pattern = vram + ((reg_[4] & 0x07) << 11);
	uint8_t *out = active_raster;
	const uint8_t *const cm = cram;
	uint8_t bg = cm[reg_[7] & 0x0F];
	uint8_t fg = cm[reg_[7] >> 4];
	uint8_t c_arr[2] = { bg, fg };
	uint32_t bg_block = bg | (bg << 8) | (bg << 16) | (bg << 24);

	for (int char_row = 0; char_row < 24; char_row++) {
		for (int scanline = 0; scanline < 8; scanline++) {
			uint8_t *p_line = p_base + char_row * 40;

			*((uint32_t *)out) = bg_block;
			out += 4;
			*((uint32_t *)out) = bg_block;
			out += 4;

			for (int x = 0; x < 40; x++) {
				uint8_t code = *p_line++;
				uint8_t b = pattern[(code << 3) + scanline];
				*out++ = c_arr[(b >> 7) & 1];
				*out++ = c_arr[(b >> 6) & 1];
				*out++ = c_arr[(b >> 5) & 1];
				*out++ = c_arr[(b >> 4) & 1];
				*out++ = c_arr[(b >> 3) & 1];
				*out++ = c_arr[(b >> 2) & 1];
			}

			*((uint32_t *)out) = bg_block;
			out += 4;
			*((uint32_t *)out) = bg_block;
			out += 4;
		}
	}
}

void IRAM_ATTR VDP::tms9918a_rasterize() {
	prepare_sprites();
	unsigned int mode = (reg_[1] >> 2) & 0x06;
	mode |= (reg_[0] & 0x02) >> 1;

	if ((reg_[1] & 0x40) == 0)
		memset(active_raster, cram[0], 256 * 192);
	else {
		switch (mode) {
			case 0: tms9918a_rasterize_g1(); break;
			case 1: tms9918a_rasterize_g2(); break;
			case 2: tms9918a_rasterize_mc(); break;
			case 4: tms9918a_rasterize_text(); break;
			default: memset(active_raster, 0, sizeof(active_raster));
		}
	}
	if (reg_[0] & 0x20) {
		uint8_t bg = cram[reg_[7] & 0x0F];
		for (int y = 0; y < 192; y++) {
			memset(&active_raster[y * 256], bg, 8);
		}
	}
	status_ |= 0x80;
}

void IRAM_ATTR VDP::tms9918a_write(uint8_t addr, uint8_t val) {
	if ((addr & 1) == 0) {
		vram[vaddr_ & 0x3FFF] = val;
		vaddr_ = (vaddr_ + 1) & 0x3FFF;
		vcmd_ = false;
	} else {
		if (!vcmd_) {
			vlatch_ = val;
			vcmd_ = true;
			return;
		}
		vcmd_ = false;

		switch (val & 0xC0) {
			case 0x00:
				vaddr_ = (((uint16_t)(val & 0x3F)) << 8) | vlatch_;
				read_mode = 1;
				rbuf_ = vram[vaddr_ & 0x3FFF];
				vaddr_ = (vaddr_ + 1) & 0x3FFF;
				break;
			case 0x40:
				vaddr_ = (((uint16_t)(val & 0x3F)) << 8) | vlatch_;
				read_mode = 0;
				break;
			case 0x80:
				{
					uint8_t rn = val & 0x07;
					reg_[rn] = vlatch_;
				}
				break;
		}
	}
}

uint8_t IRAM_ATTR VDP::tms9918a_read(uint8_t addr) {
	uint8_t r;
	if ((addr & 1) == 0) {
		r = rbuf_;
		rbuf_ = vram[vaddr_ & 0x3FFF];
		vaddr_ = (vaddr_ + 1) & 0x3FFF;
	} else {
		r = status_;
		status_ = 0;
	}
	return r;
}

void VDP::tms9918a_reset() {
	memset(reg_, 0, 16);
	vaddr_ = 0;
	vcmd_ = false;
	vlatch_ = 0;
	read_mode = 0;
	status_ = 0;

	static const uint8_t tms9918a_fixed_palette[16] = {
		0x00, 0x00, 0x08, 0x0C, 0x20, 0x30, 0x24, 0x0D,
		0x28, 0x3C, 0x38, 0x3D, 0x04, 0x35, 0x2A, 0x3F
	};
	memcpy(cram, tms9918a_fixed_palette, 16);
}

int VDP::tms9918a_irq_pending() {
	if (reg_[1] & 0x20)
		return status_ & 0x80;
	return 0;
}

void IRAM_ATTR VDP::tms9918a_apply_cram() {
	uint8_t *p = active_raster;
	const uint8_t *cm = cram;
	for (int i = 0; i < 256 * 192; i++) {
		p[i] = cm[p[i] & 0x0F];
	}
}

void IRAM_ATTR VDP::flip_buffer() {
	visible_raster = active_raster;
	if (active_raster == rasterbuffer0) {
		active_raster = rasterbuffer1;
	} else {
		active_raster = rasterbuffer0;
	}
}

void VDP::saveState(VDPState& s) const {
	memcpy(s.vram,  vram,  16384);
	memcpy(s.reg,   reg_,  16);
	memcpy(s.cram,  cram,  32);
	s.vaddr        = vaddr_;
	s.vcode        = vcode_;
	s.vlatch       = vlatch_;
	s.vcmd         = vcmd_;
	s.rbuf         = rbuf_;
	s.linecnt      = linecnt_;
	s.status       = status_;
	s.vint_pending = vint_pending_;
	s.hint_pending = hint_pending_;
	s.read_mode    = read_mode;
}

void VDP::loadState(const VDPState& s) {
	memcpy(vram,   s.vram,  16384);
	memcpy(reg_,   s.reg,   16);
	memcpy(cram,   s.cram,  32);
	memcpy(cramRGB, s.cram, 32);
	vaddr_        = s.vaddr;
	vcode_        = s.vcode;
	vlatch_       = s.vlatch;
	vcmd_         = s.vcmd;
	rbuf_         = s.rbuf;
	linecnt_      = s.linecnt;
	status_       = s.status;
	vscroll_      = reg_[9];
	vint_pending_ = s.vint_pending;
	hint_pending_ = s.hint_pending;
	read_mode     = s.read_mode;
}
