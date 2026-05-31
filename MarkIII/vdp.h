#pragma once
#include <Arduino.h>
#include <fabgl.h>

#define SCREEN_W 256
#define SCREEN_H 192

class VDP {
public:
    VDP(uint8_t* vram_buf, uint8_t* cram_buf, uint8_t* cramRGB_buf, uint8_t* reg_buf);
    void reset();

    void writeData(uint8_t v);
    uint8_t readData();
    void writeControl(uint8_t v);
    uint8_t readControl();

    void renderSingleLine(int y);
    void scanlineCheck(int y);
    
    void transferFrameToDisplay();

    // ── H-INT / V-INT ──
    bool     vint_pending_;
    bool     hint_pending_;
    bool     irqPending() const {
        return (vint_pending_ && (reg_[1] & 0x20))
            || (hint_pending_ && (reg_[0] & 0x10));
    }

    // ── レンダリング関数群 ──
    inline uint32_t expand_bits_to_color(uint8_t nibble, uint8_t color);
    inline uint32_t expand_mag_bits_to_color(uint8_t two_bits, uint8_t color);
    void tms9918a_render_slice(int y, uint8_t *sprat, uint16_t bits, unsigned int width);
    void tms9918a_render_sprite(int y, uint8_t *sprat, uint8_t *spdat);
    void prepare_sprites();
    void tms9918a_sprite_line(int y);
    void tms9918a_raster_sprites();
    void tms9918a_rasterize_g1();
    void tms9918a_rasterize_g2();
    void tms9918a_rasterize_mc();
    void tms9918a_rasterize_text();

    uint8_t tms9918a_read(uint8_t addr);
    void tms9918a_rasterize();
    void tms9918a_write(uint8_t addr, uint8_t val);
    void tms9918a_reset();
    int tms9918a_irq_pending();
    void tms9918a_apply_cram();
    void flip_buffer();
    
    // ── ダブルバッファ関係 ──
    uint8_t  rasterbuffer0[SCREEN_W * SCREEN_H];
    uint8_t  rasterbuffer1[SCREEN_W * SCREEN_H];
    uint8_t* active_raster;
    uint8_t* visible_raster;
    
    // ── メモリ類（DRAM_ATTR グローバルへのポインタ） ──
    uint8_t* vram;     // 16384 bytes – 外部 DRAM_ATTR バッファを指す
    uint8_t* cram;     // 32 bytes
    uint8_t* cramRGB;  // 32 bytes
    uint8_t* reg_;     // 16 bytes
    uint8_t  sprite_line_count[192];
    uint8_t  sprite_line_list[192][32];
    bool     limit_sprites = true;
    uint16_t memmask = 0x3FFF;
    uint8_t  read_mode;
    
    // 内部状態レジスタ
    uint16_t vaddr_;
    uint8_t  vcode_;
    uint8_t  vlatch_;
    bool     vcmd_;
    uint8_t  rbuf_;
    uint8_t  linecnt_;
    uint8_t  status_;

private:
    void renderLine(int y, uint8_t* line_buf);
    void renderBG(int y, uint8_t* px, uint8_t* bginfo);
    void renderSprites(int y, uint8_t* px, uint8_t* bginfo);

    // インラインヘルパー関数群
    uint16_t ntBase() const { return (reg_[2] & 0x0E) << 10; }
    uint16_t satBase() const { return (reg_[5] & 0x7E) << 7; }
    uint16_t sprBase() const { return (reg_[6] & 0x04) << 11; }
    
    void setCRAM(uint8_t addr, uint8_t val) {
        cram[addr & 0x1F] = val;
        cramRGB[addr & 0x1F] = val; 
    }
};
