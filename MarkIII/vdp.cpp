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
	0x00,  // 0:(Transparent)
	0x00,  // 1:(Black)
	0x08,  // 2:(Medium Green)
	0x0C,  // 3:(Light Green)
	0x20,  // 4:(Dark Blue)
	0x30,  // 5:(Light Blue)
	0x02,  // 6:(Dark Red)
	0x3C,  // 7:(Cyan)
	0x03,  // 8:(Medium Red)
	0x17,  // 9:(Light Red)
	0x0A,  // 10:(Dark Yellow)
	0x1F,  // 11:(Light Yellow)
	0x04,  // 12:(Dark Green)
	0x33,  // 13:(Magenta)
	0x2A,  // 14:(Gray)
	0x3F   // 15:(White)
};

VDP::VDP(uint8_t *vram_buf, uint8_t *cram_buf, uint8_t *cramRGB_buf, uint8_t *reg_buf) {
	vram = vram_buf;
	cram = cram_buf;
	cramRGB = cramRGB_buf;
	reg_ = reg_buf;
	active_raster = rasterbuffer0;
	visible_raster = rasterbuffer1;
	reset();
}

void VDP::reset() {
	memset(vram, 0, 16384);
	memset(cram, 0, 32);
	memset(cramRGB, 0, 32);
	memset(reg_, 0, 16);

	memset(sprite_line_count, 0, sizeof(sprite_line_count));
	memset(sprite_line_list, 0, sizeof(sprite_line_list));
	// ダブルバッファの初期化を復活
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
	linecnt_ = 0xFF;
	vint_pending_ = false;
	hint_pending_ = false;

	memcpy(cram, tms9918a_fixed_palette, 16);
	memcpy(cramRGB, tms9918a_fixed_palette, 16);
}

void IRAM_ATTR VDP::writeData(uint8_t v) {
	vcmd_ = false;
	switch (vcode_) {
		case 0:
		case 1:
			vram[vaddr_ & 0x3FFF] = v;
			vaddr_ = (vaddr_ + 1) & 0x3FFF;
			rbuf_ = vram[vaddr_];
			break;
		case 3:
			setCRAM(vaddr_ & 0x1F, v);
			vaddr_ = (vaddr_ + 1) & 0x3FFF;
			break;
		default:
			break;
	}
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

void IRAM_ATTR VDP::renderLine(int y, uint8_t *line_buf) {
	uint8_t bg_color_idx = 16 + (reg_[7] & 0x0F);

	if (!(reg_[1] & 0x40)) {
		uint8_t bg = cramRGB[bg_color_idx];
		memset(line_buf, bg, SCREEN_W);
		return;
	}

	uint8_t bginfo[SCREEN_W];
	memset(bginfo, 0, SCREEN_W);

	renderBG(y, line_buf, bginfo);
	renderSprites(y, line_buf, bginfo);

	if (reg_[0] & 0x20) {
		//uint8_t bg = cramRGB[bg_color_idx];
		//memset(line_buf, bg, 8);
		memset(line_buf, 0, 8);
	}
}

void IRAM_ATTR VDP::renderBG(int y, uint8_t *px, uint8_t *bginfo) {
	const uint16_t nt = ntBase();
	const int scx = ((reg_[0] & 0x40) && y < 16) ? 0 : reg_[8];

	int bg0 = (256 - scx) & 255;
	int fine = bg0 & 7;
	int tcol0 = bg0 >> 3;

	const uint8_t *vram_ptr = vram;
	int current_scy_base = reg_[9] % 224;
	bool v_lock = (reg_[0] & 0x80);

	for (int tc = 0; tc <= 32; tc++) {
		int start_sx = tc * 8 - fine;
		if (start_sx <= -8 || start_sx >= SCREEN_W) continue;
		int col = (tcol0 + tc) & 31;
		int current_scy = (v_lock && tc >= 24) ? 0 : current_scy_base;
		int sum_y = y + current_scy;
		int eff_y = (sum_y >= 224) ? sum_y - 224 : sum_y;
		int row = eff_y >> 3;
		if (!(reg_[2] & 0x01)) row &= ~0x10;
		int tile_row = row * 32;
		int tile_y = eff_y & 7;
		uint16_t na = (nt + (tile_row + col) * 2) & 0x3FFF;
		uint16_t n_val = *(const uint16_t *)&vram_ptr[na];
		uint16_t tile = n_val & 0x01FF;
		bool hflip = n_val & 0x0200;
		bool vflip = n_val & 0x0400;
		uint8_t pal_b = (n_val & 0x0800) ? 16 : 0;
		uint8_t pri_bit = (n_val & 0x1000) ? 0x10 : 0x00;
		int ty = vflip ? (7 - tile_y) : tile_y;
		uint16_t ta = ((tile << 5) | (ty << 2)) & 0x3FFF;
		uint32_t b32 = *(const uint32_t *)&vram_ptr[ta];
		uint8_t b0 = b32 & 0xFF;
		uint8_t b1 = (b32 >> 8) & 0xFF;
		uint8_t b2 = (b32 >> 16) & 0xFF;
		uint8_t b3 = (b32 >> 24) & 0xFF;

		if (start_sx < 0 || start_sx > SCREEN_W - 8) {
			if (!hflip) {
#define BG_EDGE_NO_FLIP(pp) \
	do { \
		int sx = start_sx + pp; \
		if (sx >= 0 && sx < SCREEN_W) { \
			uint8_t idx = ((b0 >> 7) & 1) | (((b1 >> 7) & 1) << 1) | (((b2 >> 7) & 1) << 2) | (((b3 >> 7) & 1) << 3); \
			px[sx] = cramRGB[pal_b + idx]; \
			bginfo[sx] = idx | pri_bit; \
		} \
		b0 <<= 1; \
		b1 <<= 1; \
		b2 <<= 1; \
		b3 <<= 1; \
	} while (0)
				BG_EDGE_NO_FLIP(0);
				BG_EDGE_NO_FLIP(1);
				BG_EDGE_NO_FLIP(2);
				BG_EDGE_NO_FLIP(3);
				BG_EDGE_NO_FLIP(4);
				BG_EDGE_NO_FLIP(5);
				BG_EDGE_NO_FLIP(6);
				BG_EDGE_NO_FLIP(7);
#undef BG_EDGE_NO_FLIP
			} else {
#define BG_EDGE_FLIP(pp) \
	do { \
		int sx = start_sx + pp; \
		if (sx >= 0 && sx < SCREEN_W) { \
			uint8_t idx = (b0 & 1) | ((b1 & 1) << 1) | ((b2 & 1) << 2) | ((b3 & 1) << 3); \
			px[sx] = cramRGB[pal_b + idx]; \
			bginfo[sx] = idx | pri_bit; \
		} \
		b0 >>= 1; \
		b1 >>= 1; \
		b2 >>= 1; \
		b3 >>= 1; \
	} while (0)
				BG_EDGE_FLIP(0);
				BG_EDGE_FLIP(1);
				BG_EDGE_FLIP(2);
				BG_EDGE_FLIP(3);
				BG_EDGE_FLIP(4);
				BG_EDGE_FLIP(5);
				BG_EDGE_FLIP(6);
				BG_EDGE_FLIP(7);
#undef BG_EDGE_FLIP
			}
		} else {
			if (!hflip) {
#define BG_MID_NO_FLIP(pp) \
	do { \
		int sx = start_sx + pp; \
		uint8_t idx = ((b0 >> 7) & 1) | (((b1 >> 7) & 1) << 1) | (((b2 >> 7) & 1) << 2) | (((b3 >> 7) & 1) << 3); \
		b0 <<= 1; \
		b1 <<= 1; \
		b2 <<= 1; \
		b3 <<= 1; \
		px[sx] = cramRGB[pal_b + idx]; \
		bginfo[sx] = idx | pri_bit; \
	} while (0)
				BG_MID_NO_FLIP(0);
				BG_MID_NO_FLIP(1);
				BG_MID_NO_FLIP(2);
				BG_MID_NO_FLIP(3);
				BG_MID_NO_FLIP(4);
				BG_MID_NO_FLIP(5);
				BG_MID_NO_FLIP(6);
				BG_MID_NO_FLIP(7);
#undef BG_MID_NO_FLIP
			} else {
#define BG_MID_FLIP(pp) \
	do { \
		int sx = start_sx + pp; \
		uint8_t idx = (b0 & 1) | ((b1 & 1) << 1) | ((b2 & 1) << 2) | ((b3 & 1) << 3); \
		b0 >>= 1; \
		b1 >>= 1; \
		b2 >>= 1; \
		b3 >>= 1; \
		px[sx] = cramRGB[pal_b + idx]; \
		bginfo[sx] = idx | pri_bit; \
	} while (0)
				BG_MID_FLIP(0);
				BG_MID_FLIP(1);
				BG_MID_FLIP(2);
				BG_MID_FLIP(3);
				BG_MID_FLIP(4);
				BG_MID_FLIP(5);
				BG_MID_FLIP(6);
				BG_MID_FLIP(7);
#undef BG_MID_FLIP
			}
		}
	}
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
	int jobs_sprite_y[64];
	int job_count = 0;

	const uint8_t *vram_ptr = vram;
	uint16_t sat_masked = sat & 0x3FFF;

	for (int s = 0; s < 64; s++) {
		int sy = vram_ptr[(sat_masked + s) & 0x3FFF];
		if (sy == 0xD0) break;

		int sprite_y = sy + 1;
		if (y < sprite_y || y >= sprite_y + sph) continue;

		if (!spriteHorizontalUnlimited && ++count > 8) {
			status_ |= 0x40;
			break;
		}

		uint16_t attr = (sat_masked + 128 + s * 2) & 0x3FFF;
		// 【最適化】スプライト属性も16ビットで一括読み込み
		uint16_t attr_val = *(const uint16_t *)&vram_ptr[attr];
		int sx = (attr_val & 0xFF) - (xshift ? 8 : 0);
		uint8_t tile = (attr_val >> 8) & 0xFF;

		jobs_sx[job_count] = sx;
		jobs_tile[job_count] = tile;
		jobs_sprite_y[job_count] = sprite_y;
		job_count++;
	}

	const uint8_t *sprite_cram = &cramRGB[16];

	for (int i = job_count - 1; i >= 0; i--) {
		int sx = jobs_sx[i];
		uint8_t tile = jobs_tile[i];
		int sprite_y = jobs_sprite_y[i];

		if (tall) tile &= 0xFE;

		int local_y = y - sprite_y;
		if (tall && local_y >= 8) {
			tile++;
			local_y -= 8;
		}

		uint16_t ta = (spb | (tile << 5) | (local_y << 2)) & 0x3FFF;

		// 【最適化】スプライトのタイルデータも32ビット一括読み込み
		uint32_t b32 = *(const uint32_t *)&vram_ptr[ta];
		uint8_t b0 = b32 & 0xFF;
		uint8_t b1 = (b32 >> 8) & 0xFF;
		uint8_t b2 = (b32 >> 16) & 0xFF;
		uint8_t b3 = (b32 >> 24) & 0xFF;

		if (sx >= 0 && sx <= SCREEN_W - 8) {
#define SPR_MID(pp) \
	do { \
		int screen_x = sx + pp; \
		uint8_t idx = ((b0 >> 7) & 1) | (((b1 >> 7) & 1) << 1) | (((b2 >> 7) & 1) << 2) | (((b3 >> 7) & 1) << 3); \
		b0 <<= 1; \
		b1 <<= 1; \
		b2 <<= 1; \
		b3 <<= 1; \
		if (idx != 0) { \
			uint8_t bgi = bginfo[screen_x]; \
			if (!((bgi & 0x10) && (bgi & 0x0F) != 0)) { \
				px[screen_x] = sprite_cram[idx]; \
			} \
		} \
	} while (0)
			SPR_MID(0);
			SPR_MID(1);
			SPR_MID(2);
			SPR_MID(3);
			SPR_MID(4);
			SPR_MID(5);
			SPR_MID(6);
			SPR_MID(7);
#undef SPR_MID
		} else {
#define SPR_EDGE(pp) \
	do { \
		int screen_x = sx + pp; \
		if (screen_x >= 0 && screen_x < SCREEN_W) { \
			uint8_t idx = ((b0 >> 7) & 1) | (((b1 >> 7) & 1) << 1) | (((b2 >> 7) & 1) << 2) | (((b3 >> 7) & 1) << 3); \
			if (idx != 0) { \
				uint8_t bgi = bginfo[screen_x]; \
				if (!((bgi & 0x10) && (bgi & 0x0F) != 0)) { \
					px[screen_x] = sprite_cram[idx]; \
				} \
			} \
		} \
		b0 <<= 1; \
		b1 <<= 1; \
		b2 <<= 1; \
		b3 <<= 1; \
	} while (0)
			SPR_EDGE(0);
			SPR_EDGE(1);
			SPR_EDGE(2);
			SPR_EDGE(3);
			SPR_EDGE(4);
			SPR_EDGE(5);
			SPR_EDGE(6);
			SPR_EDGE(7);
#undef SPR_EDGE
		}
	}
}

void IRAM_ATTR VDP::renderSingleLine(int y) {
	if (y < 0 || y >= SCREEN_H) return;

	if (active_raster != nullptr) {
		uint8_t *line_buf = &active_raster[y * SCREEN_W];
		renderLine(y, line_buf);
	}
}

void IRAM_ATTR VDP::scanlineCheck(int y) {

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

/*
 *	Draw a horizontal slice of a sprite into the render buffer. If it
 *	needs magnifying then do the magnification as we render. We use a
 *	simple line buffer to detect collisions
 */
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
				// 画面端
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

IRAM_ATTR void VDP::tms9918a_render_sprite(int y, uint8_t *sprat, uint8_t *spdat) {
	int row = *sprat;
	uint16_t bits;
	unsigned int width = 8;
	unsigned int mag = reg_[1] & 0x01;

	if (row > 0xE0)
		row -= 0x100;
	row += 1;
	row = y - row;
	if (mag)
		row >>= 1;

	if ((reg_[1] & 0x02) == 0) {
		spdat += row;
		width = 8;
		bits = *spdat << 24;
	} else {
		spdat += row;
		bits = *spdat << 8;
		bits |= spdat[16];
		width = 16;
	}
	tms9918a_render_slice(y, sprat, bits, width);
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
	int count = sprite_line_count[y];

	if (limit_sprites && count > 4) {
		uint8_t fifth_idx = sprite_line_list[y][4];
		status_ |= 0x40 | (fifth_idx & 0x1F);
		count = 4;
	}

	for (int n = count - 1; n >= 0; n--) {
		uint8_t i = sprite_line_list[y][n];
		uint8_t *sprat = sprat_base + (i * 4);
		uint8_t foreground = sprat[3] & 0x0F;
		if (foreground == 0) continue;

		tms9918a_render_sprite(y, sprat, spdat + (sprat[2] << spshft));
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

			// Left Border (8px)
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

			// Right Border (8px)
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
		memset(active_raster, cram[0], 256 * 192);  // 画面OFF時: 黒でクリア
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
			memset(&active_raster[y * 256], bg, 8); // 左端の8ピクセルを背景色で上書き
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
			case 0x00:  // Read set up
				vaddr_ = (((uint16_t)(val & 0x3F)) << 8) | vlatch_;
				read_mode = 1;
				rbuf_ = vram[vaddr_ & 0x3FFF];
				vaddr_ = (vaddr_ + 1) & 0x3FFF;
				break;
			case 0x40:  // Write set up
				vaddr_ = (((uint16_t)(val & 0x3F)) << 8) | vlatch_;
				read_mode = 0;
				break;
			case 0x80:  // Reg write
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