/* ============================================================================
 *  Sega Mark III / Master System Emulator for ESP32 (Work in Progress)
 * ============================================================================
 * 
 * [ Credits & Acknowledgments ]
 *  - Graphics/Audio Engine: FabGL by Fabrizio Di Vittorio.
 *  - Z80 CPU Core: Modified based on the work by Marat Fayzullin.
 *  - VDP Section: Modified based on the Sega VDP core originally derived from 
 *                 TMS9918A by David Latham and maintained by Marat Fayzullin.
 *  - FM Sound Unit: Modified based on "emu2413" by Mitsutaka Okazaki (MIT License).
 *  Special thanks to all authors for providing these excellent foundations.
 * 
 * [ Development Environment ]
 *  - Board Manager: ESP32 by Espressif Systems (v2.0.5 recommended)
 *  - Library: FabGL v1.0.9
 * 
 * [ Recommended Arduino IDE Settings ]
 *  - PSRAM: "Enabled"
 *  - Partition Scheme: "Default 4MB with SPIFFS" or similar
 *  - ESP32 Dev Module
 * 
 * [ Disclaimer ]
 *  IN NO EVENT SHALL FABRIZIO DI VITTORIO, MARAT FAYZULLIN, DAVID LATHAM, 
 *  MITSUTAKA OKAZAKI, OR THE DEVELOPER BE LIABLE FOR ANY DIRECT, INDIRECT, 
 *  INCIDENTAL, OR CONSEQUENTIAL DAMAGES ARISING IN ANY WAY OUT OF THE USE 
 *  OF THIS SOFTWARE. USE AT YOUR OWN RISK.
 * 
 * [ Important Notes ]
 *  - This project is under development and may contain bugs or redundant code.
 *  - Subject to the license policies of FabGL, Marat Fayzullin (fMSX License), 
 *    and Mitsutaka Okazaki (MIT License).
 * ============================================================================
 */

/* Japanese
 * ============================================================================
 *  Sega Mark III / Master System Emulator for ESP32 (Work in Progress)
 * ============================================================================
 * 
 * 【クレジットと謝辞】
 *  - グラフィック/オーディオエンジン: Fabrizio Di Vittorio氏による FabGL を使用しています。
 *  - Z80 CPUコア: Marat Fayzullin氏によるコードをベースに改変して使用しています。
 *  - VDPセクション: David Latham氏およびMarat Fayzullin氏のTMS9918Aコアをベースに、
 *                  セガVDP仕様へ改変・調整を加えた実装を使用しています。
 *  - FM音源ユニット: Mitsutaka Okazaki氏による「emu2413」をベースに改変して使用しています（MITライセンス）。
 *  素晴らしい基盤を公開されている諸氏に深く感謝の意を表します。
 * 
 * 【動作確認済み環境 / Required Libraries】
 *  - ボードマネージャ: ESP32 by Espressif Systems (v2.0.5 推奨)
 *  - 使用ライブラリ: FabGL v1.0.9
 * 
 * 【Arduino IDE 推奨設定】
 *  - PSRAM: "Enabled" 
 *  - Partition Scheme: "Default 4MB with SPIFFS" 等
 *  - ESP32 Dev Module
 * 
 * 【免責事項】
 *  本ソフトウェアの使用によって生じたいかなる損害（直接的・間接的を問わず）
 *  についても、Fabrizio Di Vittorio氏、Marat Fayzullin氏、David Latham氏、
 *  Mitsutaka Okazaki氏、および開発者本人は一切の責任を負いません。
 *  利用者の責任において使用してください。
 * 
 * 【注意事項】
 *  - 本プロジェクトは開発中であり、不具合や最適化不足なコードが含まれる場合があります。
 *  - 本ソフトウェア内の各コアのライセンスは、FabGL、Marat Fayzullin氏 (fMSX License)、
 *    および Mitsutaka Okazaki氏 (MIT License) の各ポリシーに従います。
 * ============================================================================
 */

#pragma GCC optimize("O3")

#include <ctype.h>
#include <string.h>
#include <stdlib.h>
#include <SPI.h>
#include <SD.h>
#include <fabgl.h>
#include <vector>
#include <WiFi.h>
#include "soc/rtc_wdt.h"
#include "esp_task_wdt.h"
#include "Z80.h"
#include "vdp.h"
#include "emu2413.h"

#define USE_SERIAL_DEBUG
//#define USE_SIGNAL_DEBUG

// TTGO VGA32
#define SD_SCLK 14
#define SD_MISO 2
#define SD_MOSI 12
#define SD_CS 13

// ORANGE ESPer
// #define SD_SCLK 18
// #define SD_MISO 19
// #define SD_MOSI 23 // or 12
// #define SD_CS 5

extern "C" {
  uint8_t readByte(void* context, uint16_t addr);
  void writeByte(void* context, uint16_t addr, uint8_t val);
  int readIO(void* context, int port);
  void writeIO(void* context, int port, int val);

  extern Z80 cpu;

  byte RdZ80(zword Addr);
  void WrZ80(zword Addr, byte Value);
  byte InZ80(zword Port);
  void OutZ80(zword Port, byte Value);
  void PatchZ80(Z80* R);
}

Z80 cpu;

byte IRAM_ATTR RdZ80(zword Addr) {
  return (byte)readByte(NULL, Addr);
}
void IRAM_ATTR WrZ80(zword Addr, byte Value) {
  writeByte(NULL, Addr, Value);
}
byte IRAM_ATTR InZ80(zword Port) {
  return (byte)readIO(NULL, Port);
}
void IRAM_ATTR OutZ80(zword Port, byte Value) {
  writeIO(NULL, Port, Value);
}
void PatchZ80(Z80* R) {}

void listROMs();
void detectMapper(uint32_t size);
void updateKeyboard();
void updateFabGLSound();

fabgl::VGAController DisplayController;
fabgl::Canvas* canvas;
fabgl::Bitmap* smsBitmap;
fabgl::SoundGenerator soundGenerator;
fabgl::NoiseWaveformGenerator noiseGen;
fabgl::PS2Controller PS2Controller;

//psg scc
int8_t scc_waveform[6][32] = { 0 };
uint16_t scc_f[] = { 0, 0, 0, 0, 0, 0 };
uint8_t scc_v[] = { 0, 0, 0, 0, 0, 0 };
uint8_t scc_enable = 0x07;
bool psg2scc = false;

static const int BUFFER_SIZE = 512;
const uint8_t SCC_VOL_TABLE[16] = {
  0, 2, 4, 7, 10, 15, 21, 26, 33, 39, 47, 54, 62, 68, 73, 75
};

uint8_t Crystal_Drop_SCC[32] = {
  0x80, 0xB0, 0xE0, 0xB0, 0x80, 0x50, 0x20, 0x50, 0x80, 0xA0, 0xC0, 0xE0, 0xFF, 0xE0, 0xC0, 0xA0,
  0x80, 0x50, 0x20, 0x50, 0x80, 0xB0, 0xE0, 0xB0, 0x80, 0x60, 0x40, 0x20, 0x00, 0x20, 0x40, 0x60
};

uint8_t Warm_Body_PhaseShift[32] = {
  0x80, 0x68, 0x50, 0x38, 0x20, 0x10, 0x08, 0x04, 0x02, 0x04, 0x08, 0x10, 0x20, 0x38, 0x50, 0x68,
  0x80, 0x98, 0xB0, 0xC8, 0xE0, 0xF0, 0xF8, 0xFC, 0xFE, 0xFC, 0xF8, 0xF0, 0xE0, 0xC8, 0xB0, 0x98
};

uint8_t SCC_Sawtooth[32] = {
  0x00, 0x08, 0x10, 0x18, 0x20, 0x28, 0x30, 0x38, 0x40, 0x48, 0x50, 0x58, 0x60, 0x68, 0x70, 0x78,
  0x80, 0x88, 0x90, 0x98, 0xA0, 0xA8, 0xB0, 0xB8, 0xC0, 0xC8, 0xD0, 0xD8, 0xE0, 0xE8, 0xF0, 0xF8
};

uint8_t SCC_SyncOvertone[32] = {
  0x80, 0xC0, 0xFF, 0xC0, 0x80, 0x40, 0x00, 0x40, 0x80, 0xA0, 0xC0, 0xA0, 0x80, 0x60, 0x40, 0x60,
  0x80, 0x90, 0xA0, 0x90, 0x80, 0x70, 0x60, 0x70, 0x80, 0x88, 0x90, 0x88, 0x80, 0x78, 0x70, 0x78
};

void initSCCPresets() {
  // マークIII 用プリセット波形を SCC 波形バッファにロード
  for (int i = 0; i < 32; i++) {
    scc_waveform[0][i] = (int8_t)Crystal_Drop_SCC[i] - 128;
    scc_waveform[1][i] = (int8_t)SCC_Sawtooth[i] - 128;
    scc_waveform[2][i] = (int8_t)SCC_SyncOvertone[i] - 128;
    scc_waveform[3][i] = (int8_t)Warm_Body_PhaseShift[i] - 128;
    scc_waveform[4][i] = (int8_t)Warm_Body_PhaseShift[i] - 128;
  }
}

// for mark3
String selectedRom;
std::vector<String> romFiles;
uint8_t sms_joy_state_a = 0xFF;
uint8_t sms_joy_state_b = 0xFF;
uint8_t sms_memory_control = 0x00;
volatile uint8_t sms_io_port_3F = 0xFF;
const bool smsRegionExport = true;
bool ym2413_enabled = false;
volatile int current_scanline = 0;
String current_rom_path = "";
bool spriteHorizontalUnlimited = false;
uint8_t renderCounter = 0;

uint8_t* main_ram = nullptr;
uint8_t* game_rom_ptr = nullptr;
uint8_t* bank_ptrs[3];
uint32_t rom_size = 0;
uint8_t cart_ram[32 * 1024];
bool sram_enabled = false;
uint8_t sram_bank = 0;
enum SmsMapperType { SMS_MAPPER_SEGA,
                     SMS_MAPPER_CODEMASTERS };
SmsMapperType smsMapper = SMS_MAPPER_SEGA;
uint32_t bank_mask = 0;
bool sega_mapper_locked = false;
uint8_t* mem_read_map[64];

//VDP
volatile bool frameReady = false;
bool displayEnabled = true;

enum class VideoMode {
  TMS9918A,
  MODE4
};

VideoMode detectVideoModeFromHeader(const uint8_t* rom_data, uint32_t size);

bool hasSmsHeader(const uint8_t* rom_data, uint32_t size) {
  if (!rom_data || size < 0x7FF3) return false;
  return rom_data[0x7FF0] == 'T' && rom_data[0x7FF1] == 'M' && rom_data[0x7FF2] == 'R';
}

VideoMode determineVideoMode(const String& path, const uint8_t* rom_data = nullptr, uint32_t rom_size = 0) {
  if (rom_data && rom_size > 0) {
    if (hasSmsHeader(rom_data, rom_size)) {
      return VideoMode::MODE4;
    }
  }

  String ext = path.substring(path.lastIndexOf('.') + 1);
  ext.toUpperCase();
  if (ext == "SMS" || ext == "SMD" || ext == "GG" || ext == "SGX") {
    return VideoMode::MODE4;
  }
  if (ext == "SG" || ext == "SC" || ext == "SG-1000" || ext == "BIN" || ext == "ROM") {
    return VideoMode::TMS9918A;
  }

  return VideoMode::TMS9918A;
}

VideoMode currentVideoMode = VideoMode::MODE4;
VDP* smsVdp = nullptr;

static DRAM_ATTR uint8_t g_vdp_vram[16384];
static DRAM_ATTR uint8_t g_vdp_cram[32];
static DRAM_ATTR uint8_t g_vdp_cramRGB[32];
static DRAM_ATTR uint8_t g_vdp_reg[16];

// ── ステートセーブ本体構造体（PSRAMに配置） ──────────────────────────────────
struct SaveState {
    char     magic[4];          // "SMS1"
    bool     valid;             // 有効なデータが入っているか
    VDPState vdp;               // VDP全状態（VRAM含む） ~16.4KB
    Z80      cpu_state;         // Z80レジスタ/フラグ
    uint8_t  ram[0x2000];       // メインRAM 8KB
    uint8_t  sram_data[32768];  // カートリッジRAM 32KB
    uint8_t  bank_regs[3];      // バンク番号（slot 0/1/2）
    bool     sram_enabled;
    uint8_t  sram_bank;
    uint8_t  mapper_type;       // SmsMapperType をそのまま格納
    bool     mapper_locked;
    OPLL     fm_opll;           // YM2413内部状態の完全スナップショット
    uint8_t  fm_patch_idx[18];  // 各スロットのpatchポインタ復元用インデックス
    bool     ym2413_en;
    bool     psg2scc_en;
};
// 合計サイズ: ~57KB（PSRAM想定）
// ────────────────────────────────────────────────────────────────────────────
static SaveState* g_psram_state = nullptr;

class YM2413Generator : public fabgl::WaveformGenerator {
private:
  OPLL* opll = nullptr;
  int16_t sampleBuffer[BUFFER_SIZE];
  volatile int head = 0;
  volatile int tail = 0;
  int lastSample = 0;

  static const int32_t OPLL_CLOCK = 3579545;

  struct FMCommand {
    uint8_t adr;
    uint8_t val;
  };
  static const int CMD_QUEUE_SIZE = 128;
  FMCommand cmdQueue[CMD_QUEUE_SIZE];
  volatile int cmdHead = 0;
  volatile int cmdTail = 0;

public:
  YM2413Generator() {
    opll = OPLL_new(OPLL_CLOCK, 16384);
  }

  ~YM2413Generator() {
    if (opll) OPLL_delete(opll);
  }

  void writePort(uint8_t isDataPort, uint8_t val) {
    int nextHead = (cmdHead + 1) % CMD_QUEUE_SIZE;
    if (nextHead != cmdTail) {
      cmdQueue[cmdHead] = { isDataPort, val };
      cmdHead = nextHead;
    }
  }

  void fillBuffer() {
    if (!opll) return;

    while (cmdTail != cmdHead) {
      FMCommand cmd = cmdQueue[cmdTail];
      OPLL_writeIO(opll, cmd.adr, cmd.val);
      cmdTail = (cmdTail + 1) % CMD_QUEUE_SIZE;
    }

    int nextHead = (head + 1) % BUFFER_SIZE;
    while (nextHead != tail) {
      sampleBuffer[head] = (int16_t)(OPLL_calc(opll) >> 4);
      head = nextHead;
      nextHead = (head + 1) % BUFFER_SIZE;
    }
  }

  int getSample() override {
    if (head == tail) return lastSample;
    lastSample = sampleBuffer[tail];
    tail = (tail + 1) % BUFFER_SIZE;
    return lastSample;
  }

  void setFrequency(int value) override {}

  void resetOPLL() {
    if (opll) OPLL_reset(opll);
  }

  void getRegs(uint8_t* dest) {
    if (opll) {
      memcpy(dest, opll->reg, 0x40);
    }
  }

  void setRegs(const uint8_t* src) {
    if (!opll) return;

    cmdHead = 0;
    cmdTail = 0;

    for (int i = 0; i < 0x40; i++) {
      OPLL_writeIO(opll, 0, i);
      OPLL_writeIO(opll, 1, src[i]);
    }

    clearBuffer();
  }

  // OPLL内部状態を丸ごと取得（エンベロープ・位相含む完全スナップショット）
  // patch_idx: 各スロットのpatchポインタを配列インデックスに変換して保存
  void getFullState(OPLL* dest, uint8_t* patch_idx) {
    if (!opll) return;
    memcpy(dest, opll, sizeof(OPLL));
    for (int i = 0; i < 18; i++)
      patch_idx[i] = (uint8_t)(opll->slot[i].patch - opll->patch);
  }

  // OPLL内部状態を丸ごと復元。convポインタは現在のものを維持し、
  // slot[].patchは保存したインデックスから自分のpatch配列へ付け替える
  void setFullState(const OPLL* src, const uint8_t* patch_idx) {
    if (!opll) return;
    OPLL_RateConv* keepConv = opll->conv;
    memcpy(opll, src, sizeof(OPLL));
    opll->conv = keepConv;
    for (int i = 0; i < 18; i++)
      opll->slot[i].patch = &opll->patch[patch_idx[i] < 38 ? patch_idx[i] : 0];
    clearBuffer();
  }

  void clearBuffer() {
    cmdHead = 0;
    cmdTail = 0;
    head = 0;
    tail = 0;
    lastSample = 0;
    memset(sampleBuffer, 0, sizeof(sampleBuffer));
  }
};
YM2413Generator* fmGen = nullptr;

class SCCGenerator : public fabgl::WaveformGenerator {
private:
  int16_t sampleBuffer[BUFFER_SIZE];
  volatile int head = 0;
  volatile int tail = 0;
  int lastSample = 0;

  int8_t waveform[32];
  uint32_t phase = 0;
  uint32_t phaseInc = 0;
  uint8_t curVol = 0;

public:
  int getSample() override {
    if (head == tail) return lastSample;
    lastSample = sampleBuffer[tail];
    tail = (tail + 1) % BUFFER_SIZE;
    return lastSample;
  }

  void setFrequency(int value) override {}

  void updateParams(int8_t* newWave, uint32_t freq, uint8_t vol) {
    if (freq == 0) {
      phaseInc = 0;
    } else {
      ++freq;
      uint32_t freqHz = 111860 / (freq + 1);
      phaseInc = (uint32_t)(freqHz << 18);
    }
    curVol = vol;
    memcpy(waveform, newWave, 32);
  }

  void fillBuffer() {
    int nextHead = (head + 1) % BUFFER_SIZE;
    while (nextHead != tail) {
      sampleBuffer[head] = calculateNextSample();
      head = nextHead;
      nextHead = (head + 1) % BUFFER_SIZE;
    }
  }

  void clearBuffer() {
    head = 0;
    tail = 0;
    lastSample = 0;
    memset(sampleBuffer, 0, sizeof(sampleBuffer));
  }

private:
  inline int16_t calculateNextSample() {
    if (curVol == 0 || phaseInc == 0) return 0;

    int8_t sample = waveform[(phase >> 27) & 0x1F];
    phase += phaseInc;

    return (int16_t)((int)sample * curVol >> 7);
  }
};

SCCGenerator* scc_ch[6];

void IRAM_ATTR updateSCCSound() {
  for (int i = 0; i < 3; i++) {
    auto* ch = scc_ch[i];
    if ((scc_enable & (1 << i)) && scc_f[i] > 0) {
      int detuned_f = scc_f[i];
      if (i == 0) {
        detuned_f += 1;
      } else if (i == 1) {
        if (detuned_f > 1) detuned_f -= 1;
      } else {
        detuned_f += 2;
      }

      ch->updateParams((int8_t*)scc_waveform[i], detuned_f, SCC_VOL_TABLE[scc_v[i]]);
    } else {
      ch->updateParams((int8_t*)scc_waveform[0], 0, 0);
    }
  }
}

class SN76489Generator : public fabgl::WaveformGenerator {
private:
  const uint32_t snClock = 223721;
  uint32_t tickAccum = 0;
  uint32_t tickStep = 0;
  volatile uint32_t tonePeriod[3] = { 1, 1, 1 };
  volatile uint8_t volume[4] = { 15, 15, 15, 15 };
  volatile uint8_t noiseControl = 0;
  volatile uint8_t latchedChannel = 0;
  volatile uint8_t latchedType = 0;
  int32_t toneCounter[3] = { 0, 0, 0 };
  uint8_t toneOut[3] = { 0, 0, 0 };
  int32_t noiseCounter = 0;
  uint16_t lfsr = 0x4000;
  uint8_t noiseOut = 0;
  uint8_t lastTone2Out = 0;
  // int16_t volTable[16] = { 70, 68, 63, 58, 50, 44, 36, 31, 24, 20, 14, 9, 7, 4, 2, 0 };
  //int16_t volTable[16] = { 80, 78, 72, 66, 57, 50, 41, 35, 27, 23, 16, 10, 8, 5, 2, 0 };
  int16_t volTable[16] = { 90, 87, 81, 75, 64, 57, 46, 40, 31, 26, 18, 12, 9, 5, 3, 0 };
  int16_t sampleBuffer[BUFFER_SIZE];
  volatile int head = 0;
  volatile int tail = 0;

public:
  void writePort(uint8_t val) {
    if (val & 0x80) {
      latchedChannel = (val >> 5) & 0x03;
      latchedType = (val >> 4) & 0x01;

      if (latchedType == 1) {
        volume[latchedChannel] = val & 0x0F;
        if (latchedChannel < 3) {
          scc_v[latchedChannel] = 15 - volume[latchedChannel];
        }
      } else {
        if (latchedChannel < 3) {
          tonePeriod[latchedChannel] = (tonePeriod[latchedChannel] & 0x3F0) | (val & 0x0F);
          scc_f[latchedChannel] = (tonePeriod[latchedChannel] == 0) ? 0x400 : tonePeriod[latchedChannel];
        } else {
          noiseControl = val & 0x07;
          lfsr = 0x4000;
        }
      }
    } else {
      if (latchedType == 1) {
        volume[latchedChannel] = val & 0x0F;
        if (latchedChannel < 3) {
          scc_v[latchedChannel] = 15 - volume[latchedChannel];
        }
      } else {
        if (latchedChannel < 3) {
          tonePeriod[latchedChannel] = (tonePeriod[latchedChannel] & 0x00F) | ((val & 0x3F) << 4);
          scc_f[latchedChannel] = (tonePeriod[latchedChannel] == 0) ? 0x400 : tonePeriod[latchedChannel];
        } else {
          noiseControl = val & 0x07;
          lfsr = 0x4000;
        }
      }
    }
  }

  void fillBuffer() {
    if (tickStep == 0) {
      int rate = sampleRate();
      if (rate > 0) setSampleRate(rate);
      else return;
    }

    int nextHead = (head + 1) % BUFFER_SIZE;
    while (nextHead != tail) {
      sampleBuffer[head] = calculateNextSample();
      head = nextHead;
      nextHead = (head + 1) % BUFFER_SIZE;
    }
  }

  void setSampleRate(int rate) {
    this->fabgl::WaveformGenerator::setSampleRate(rate);
    if (rate > 0) {
      tickStep = (uint32_t)(((uint64_t)snClock << 16) / rate);
    } else {
      tickStep = (uint32_t)(((uint64_t)snClock << 16) / 16384);
    }
  }

  void clearBuffer() {
    head = 0;
    tail = 0;
    lastSample = 0;
    memset(sampleBuffer, 0, sizeof(sampleBuffer));
  }

private:
  int16_t lastSample = 0;
  inline int calculateNextSample() {
    uint32_t curTP[3] = { tonePeriod[0], tonePeriod[1], tonePeriod[2] };
    uint8_t curVol[4] = { volume[0], volume[1], volume[2], volume[3] };
    uint8_t curNC = noiseControl;
    tickAccum += tickStep;
    uint32_t ticks = tickAccum >> 16;
    tickAccum &= 0xFFFF;

    while (ticks--) {
      for (int i = 0; i < 3; i++) {
        toneCounter[i]--;
        if (toneCounter[i] <= 0) {
          toneCounter[i] = (curTP[i] == 0) ? 0x400 : curTP[i];
          toneOut[i] ^= 1;
        }
      }

      bool triggerNoiseShift = false;
      int shiftRate = curNC & 0x03;

      if (shiftRate == 3) {
        if (toneOut[2] != lastTone2Out) {
          triggerNoiseShift = true;
          lastTone2Out = toneOut[2];
        }
      } else {
        noiseCounter--;
        if (noiseCounter <= 0) {
          noiseCounter = (1 << shiftRate);
          triggerNoiseShift = true;
        }
      }

      if (triggerNoiseShift) {
        int feedback;
        if (curNC & 0x04) {
          feedback = (lfsr & 1) ^ ((lfsr >> 1) & 1);
        } else {
          feedback = (lfsr & 1);
        }
        lfsr = (lfsr >> 1) | (feedback << 14);
        noiseOut = lfsr & 1;
      }
    }

    int32_t mixedSample = 0;
    if (toneOut[0]) mixedSample += volTable[curVol[0]];
    if (toneOut[1]) mixedSample += volTable[curVol[1]];
    if (toneOut[2]) mixedSample += volTable[curVol[2]];
    if (noiseOut) mixedSample += volTable[curVol[3]];

    return (int16_t)(mixedSample - 128);
  }

  int getSample() override {
    if (head == tail) {
      return lastSample;
    }
    lastSample = sampleBuffer[tail];
    tail = (tail + 1) % BUFFER_SIZE;
    return lastSample;
  }

  void setFrequency(int value) override {}
};
SN76489Generator psgGen;

void IRAM_ATTR update_memory_map() {
  uint8_t* b0_ptr = bank_ptrs[0];
  for (int i = 0; i < 16; i++) {
    mem_read_map[i] = b0_ptr + (i * 1024);
  }
  if (smsMapper == SMS_MAPPER_SEGA) {
    mem_read_map[0] = game_rom_ptr;
  }

  uint8_t* b1_ptr = bank_ptrs[1];
  for (int i = 0; i < 16; i++) {
    mem_read_map[16 + i] = b1_ptr + (i * 1024);
  }

  if (sram_enabled) {
    uint8_t* sram_ptr = &cart_ram[sram_bank * 0x4000];
    for (int i = 0; i < 16; i++) {
      mem_read_map[32 + i] = sram_ptr + (i * 1024);
    }
  } else {
    uint8_t* b2_ptr = bank_ptrs[2];
    for (int i = 0; i < 16; i++) {
      mem_read_map[32 + i] = b2_ptr + (i * 1024);
    }
  }

  for (int i = 0; i < 8; i++) {
    mem_read_map[48 + i] = &main_ram[i * 1024];
    mem_read_map[56 + i] = &main_ram[i * 1024];
  }
}

inline uint8_t IRAM_ATTR readByte(void* context, uint16_t addr) {
  return mem_read_map[addr >> 10][addr & 0x03FF];
}

inline void IRAM_ATTR writeByte(void* context, uint16_t addr, uint8_t val) {
  if (addr >= 0xC000) {
    main_ram[addr & 0x1FFF] = val;

    if (addr >= 0xFFFC) {
      sega_mapper_locked = true;
      bool changed = false;

      switch (addr) {
        case 0xFFFC:
          if (rom_size <= 0x80000) {
            bool next_sram_enabled = (val & 0x08) != 0;
            uint8_t next_sram_bank = (val & 0x04) >> 2;
            if (sram_enabled != next_sram_enabled || sram_bank != next_sram_bank) {
              sram_enabled = next_sram_enabled;
              sram_bank = next_sram_bank;
              changed = true;
            }
          }
          break;

        case 0xFFFD:
          bank_ptrs[0] = &game_rom_ptr[(val & bank_mask) * 0x4000];
          changed = true;
          break;

        case 0xFFFE:
          bank_ptrs[1] = &game_rom_ptr[(val & bank_mask) * 0x4000];
          changed = true;
          break;

        case 0xFFFF:
          bank_ptrs[2] = &game_rom_ptr[(val & bank_mask) * 0x4000];
          changed = true;
          break;
      }

      if (changed) update_memory_map();
    }
  } else {
    if (addr >= 0x8000 && sram_enabled) {
      cart_ram[(sram_bank * 0x4000) + (addr & 0x3FFF)] = val;
    } else if (!sega_mapper_locked && rom_size <= 0x80000) {
      bool changed = false;
      if (addr == 0x0000) {
        smsMapper = SMS_MAPPER_CODEMASTERS;
        bank_ptrs[0] = &game_rom_ptr[(val & bank_mask) * 0x4000];
        changed = true;
      } else if (addr == 0x4000) {
        smsMapper = SMS_MAPPER_CODEMASTERS;
        bank_ptrs[1] = &game_rom_ptr[(val & bank_mask) * 0x4000];
        changed = true;
      } else if (addr == 0x8000) {
        smsMapper = SMS_MAPPER_CODEMASTERS;
        bank_ptrs[2] = &game_rom_ptr[(val & bank_mask) * 0x4000];
        changed = true;
      }
      if (changed) update_memory_map();
    }
  }
}

void writeWord(void* ctx, int addr, int val) {
  writeByte(ctx, addr, val & 0xFF);
  writeByte(ctx, addr + 1, val >> 8);
}

inline int IRAM_ATTR readIO(void* context, int port) {
  uint8_t p = (uint8_t)port;

  if (likely((p & 0xFE) == 0xBE)) {
    if (likely(currentVideoMode == VideoMode::MODE4)) {
      return (p & 1) ? smsVdp->readControl() : smsVdp->readData();
    }
    return smsVdp->tms9918a_read(p & 0x01);
  }

  if (p == 0xDC) {
    uint8_t v = sms_joy_state_a & 0x3F;
    v |= (sms_joy_state_b & 0x01) << 6;
    v |= (sms_joy_state_b & 0x02) << 6;
    return v;
  }

  if (p == 0xDD) {
    uint8_t v = ((sms_joy_state_b >> 2) & 0x0F) | 0x30;
    if (smsRegionExport) {
      v |= (sms_io_port_3F & 0x02) ? ((sms_io_port_3F & 0x20) ? 0x40 : 0x00) : 0x40;
      v |= (sms_io_port_3F & 0x08) ? ((sms_io_port_3F & 0x80) ? 0x80 : 0x00) : 0x80;
    } else {
      v |= 0xC0;
    }
    return v;
  }

  if (p == 0x7E) {
    int ln = current_scanline;
    return (uint8_t)(ln <= 0xDA ? ln : ln - 6);
  }

  if (p == 0xF2) {
    return ym2413_enabled ? 0x01 : 0x00;
  }

  return 0xFF;
}

inline void IRAM_ATTR writeIO(void* context, int port, int val) {
  uint8_t p = (uint8_t)port;
  uint8_t v = (uint8_t)val;

  if (likely((p & 0xFE) == 0xBE)) {
    if (likely(currentVideoMode == VideoMode::MODE4)) {
      if (p & 1) smsVdp->writeControl(v);
      else smsVdp->writeData(v);
    } else {
      smsVdp->tms9918a_write(p & 0x01, v);
    }
    return;
  }

  if (p == 0x7F) {
    psgGen.writePort(v);
    return;
  }

  if (p == 0xF0) {
    fmGen->writePort(0, v);
    return;
  }
  if (p == 0xF1) {
    fmGen->writePort(1, v);
    return;
  }

  if (p == 0xF2) {
    ym2413_enabled = (v & 0x01);
    psg2scc = ym2413_enabled ^ 0x01;
    return;
  }

  if (p == 0x3E) {
    sms_memory_control = v;
    return;
  }
  if (p == 0x3F) {
    sms_io_port_3F = v;
    return;
  }
}

void clearStatus() {
  canvas->setBrushColor(Color::Black);
  canvas->fillRectangle(48, 192, 48 + 224, 192 + 8);
}

void flashStatus(const char* msg, int waitMs = 0) {
  clearStatus();
  canvas->setPenColor(Color::White);
  canvas->setBrushColor(Color::Black);
  canvas->drawText(48, 192, msg);

  if (waitMs > 0) {
    delay(waitMs);
    clearStatus();
  }
}

String getSaveFilePath(int slotNumber) {
  if (current_rom_path.length() == 0) {
    return "/ROM/SAVE/default_" + String(slotNumber) + ".sav";
  }

  int slashIndex = current_rom_path.lastIndexOf('/');
  String fileName = (slashIndex == -1) ? current_rom_path : current_rom_path.substring(slashIndex + 1);

  int dotIndex = fileName.lastIndexOf('.');
  if (dotIndex != -1) {
    fileName = fileName.substring(0, dotIndex);
  }

  String slotSuffix = "";
  slotSuffix = "_" + String(slotNumber);
  return "/ROM/SAVE/" + fileName + slotSuffix + ".sav";
}

void saveSRAMtoFile(int slotNumber) {
  String savePath = getSaveFilePath(slotNumber);

  File file = SD.open(savePath, FILE_WRITE);
  if (!file) {
    flashStatus("SRAM Save Failed!", 2000);
    return;
  }

  file.write(cart_ram, sizeof(cart_ram));
  file.close();

  flashStatus("SRAM Saved!", 1000);
}

void loadSRAMfromFile(int slotNumber) {
  String savePath = getSaveFilePath(slotNumber);

  if (!SD.exists(savePath)) {
    flashStatus("No SRAM File Found", 1500);
    memset(cart_ram, 0, sizeof(cart_ram));
    return;
  }

  File file = SD.open(savePath, FILE_READ);
  if (!file) {
    flashStatus("SRAM Load Failed!", 2000);
    return;
  }

  file.read(cart_ram, sizeof(cart_ram));
  file.close();

  flashStatus("SRAM Loaded!", 1000);
}

// ── ステートセーブ関数群 ──────────────────────────────────────────────────────

String getStateFilePath() {
  if (current_rom_path.length() == 0) return "/ROM/SAVE/default_st.sav";
  int slash = current_rom_path.lastIndexOf('/');
  String name = (slash == -1) ? current_rom_path : current_rom_path.substring(slash + 1);
  int dot = name.lastIndexOf('.');
  if (dot != -1) name = name.substring(0, dot);
  return "/ROM/SAVE/" + name + "_st.sav";
}

// 現在のエミュレータ状態をPSRAMバッファに保存
void saveStateToPSRAM() {
  if (!g_psram_state || !smsVdp) return;
  memcpy(g_psram_state->magic, "SMS2", 4);
  smsVdp->saveState(g_psram_state->vdp);
  memcpy(&g_psram_state->cpu_state, &cpu, sizeof(Z80));
  memcpy(g_psram_state->ram,       main_ram, 0x2000);
  memcpy(g_psram_state->sram_data, cart_ram, sizeof(cart_ram));
  for (int i = 0; i < 3; i++)
    g_psram_state->bank_regs[i] = (uint8_t)((bank_ptrs[i] - game_rom_ptr) / 0x4000);
  g_psram_state->sram_enabled  = sram_enabled;
  g_psram_state->sram_bank     = sram_bank;
  g_psram_state->mapper_type   = (uint8_t)smsMapper;
  g_psram_state->mapper_locked = sega_mapper_locked;
  if (fmGen) fmGen->getFullState(&g_psram_state->fm_opll, g_psram_state->fm_patch_idx);
  g_psram_state->ym2413_en  = ym2413_enabled;
  g_psram_state->psg2scc_en = psg2scc;
  g_psram_state->valid = true;
  flashStatus("STATE SAVED", 800);
}

// PSRAMバッファからエミュレータ状態を復元
void loadStateFromPSRAM() {
  if (!g_psram_state || !g_psram_state->valid || !smsVdp) {
    flashStatus("NO STATE", 1000);
    return;
  }
  smsVdp->loadState(g_psram_state->vdp);
  memcpy(&cpu,      &g_psram_state->cpu_state, sizeof(Z80));
  memcpy(main_ram,  g_psram_state->ram,        0x2000);
  memcpy(cart_ram,  g_psram_state->sram_data,  sizeof(cart_ram));
  for (int i = 0; i < 3; i++)
    bank_ptrs[i] = &game_rom_ptr[g_psram_state->bank_regs[i] * 0x4000];
  sram_enabled     = g_psram_state->sram_enabled;
  sram_bank        = g_psram_state->sram_bank;
  smsMapper        = (SmsMapperType)g_psram_state->mapper_type;
  sega_mapper_locked = g_psram_state->mapper_locked;
  ym2413_enabled   = g_psram_state->ym2413_en;
  psg2scc          = g_psram_state->psg2scc_en;
  if (fmGen) fmGen->setFullState(&g_psram_state->fm_opll, g_psram_state->fm_patch_idx);
  update_memory_map();
  flashStatus("STATE LOADED", 800);
}

// PSRAMバッファの内容をSDカードに書き出す
void saveStateToSD() {
  if (!g_psram_state || !g_psram_state->valid) {
    flashStatus("NO STATE IN PSRAM", 1500);
    return;
  }
  if (!SD.exists("/ROM/SAVE")) SD.mkdir("/ROM/SAVE");
  String path = getStateFilePath();
  File f = SD.open(path, FILE_WRITE);
  if (!f) { flashStatus("STATE SD WRITE FAIL", 2000); return; }
  f.write((const uint8_t*)g_psram_state, sizeof(SaveState));
  f.close();
  flashStatus("STATE -> SD OK", 1000);
}

// SDカードからPSRAMバッファに読み込み、そのまま復元する
void loadStateFromSD() {
  if (!g_psram_state) return;
  String path = getStateFilePath();
  if (!SD.exists(path)) { flashStatus("NO STATE FILE", 1500); return; }
  File f = SD.open(path, FILE_READ);
  if (!f) { flashStatus("STATE SD READ FAIL", 2000); return; }
  f.read((uint8_t*)g_psram_state, sizeof(SaveState));
  f.close();
  if (memcmp(g_psram_state->magic, "SMS2", 4) != 0) {
    g_psram_state->valid = false;
    flashStatus("STATE FILE CORRUPT", 2000);
    return;
  }
  flashStatus("SD -> PSRAM OK", 1000);  // 復元はF6で行う
}
// ────────────────────────────────────────────────────────────────────────────

uint8_t kbdJoyState = 0x3F;
char msgBuf[32];
void updateKeyboard() {
  static bool isBreak = false;
  static bool isE0 = false;
  static bool isShift = false;
  static bool f5_held = false;
  static bool f6_held = false;
  static bool f7_held = false;
  static bool f8_held = false;
  static bool f9_held = false;
  static bool f10_held = false;
  static bool f11_held = false;
  static bool f12_held = false;

  auto kb = PS2Controller.keyboard();
  if (!kb) return;
  int count = kb->scancodeAvailable();
  if (count <= 0) return;

  while (count--) {
    int sc = kb->getNextScancode(0);
    if (sc == -1) break;

    if (sc == 0xF0) {
      isBreak = true;
      continue;
    }
    if (sc == 0xE0) {
      isE0 = true;
      continue;
    }
    if (sc == 0x12 || sc == 0x59) {
      isShift = !isBreak;
    }

    if (sc == 0x03) {  // F5
      if (!isBreak && !f5_held) {
        f5_held = true;
        if (isShift) {
          saveStateToSD();        // SHIFT+F5: PSRAM→SD書き出し
        } else {
          loadStateFromSD();      // F5: SDからロードして復元
        }
      } else if (isBreak) f5_held = false;
    } else if (sc == 0x0B) {  // F6
      if (!isBreak && !f6_held) {
        f6_held = true;
        if (isShift) {
          saveStateToPSRAM();     // SHIFT+F6: PSRAMに保存
        } else {
          loadStateFromPSRAM();   // F6: PSRAMから復元
        }
      } else if (isBreak) f6_held = false;
    } else if (sc == 0x83) {  // F7
      if (!isBreak && !f7_held) {
        f7_held = true;
        if (!isShift) {
          if (renderCounter == 0) {
            renderCounter = 1;
          } else {
            renderCounter <<= 1;
            if (renderCounter > 16) renderCounter = 0;
          }
          sprintf(msgBuf, "DISPLAY TIMING %d", renderCounter);
          flashStatus(msgBuf, 1000);
        } else {
          // SHIFT+F7: VDPレジスタのデバッグ表示（スクロールロック調査用）
          if (smsVdp) {
            sprintf(msgBuf, "R0=%02X R1=%02X R8=%02X R9=%02X",
                    smsVdp->reg_[0], smsVdp->reg_[1], smsVdp->reg_[8], smsVdp->reg_[9]);
            flashStatus(msgBuf, 3000);
          }
        }
      } else if (isBreak) f7_held = false;
    } else if (sc == 0x0A) {  // F8
      if (!isBreak && !f8_held) {
        f8_held = true;
        if (!isShift) {
          if (!psg2scc) {
            psg2scc = true;
            scc_enable = 0x07;
            flashStatus("SCC ENABLED", 1000);
          } else {
            psg2scc = false;
            scc_enable = 0;
            flashStatus("SCC DISABLED", 1000);
          }
        } else {
          if (spriteHorizontalUnlimited == false) {
            spriteHorizontalUnlimited = true;
            flashStatus("NO LIMIT SPRITES", 1000);
          } else {
            spriteHorizontalUnlimited = false;
            flashStatus("LIMIT SPRITES", 1000);
          }
        }
      } else if (isBreak) f8_held = false;
    } else if (sc == 0x01) {
      if (!isBreak && !f9_held) {
        f9_held = true;
        if (!isShift) {
          loadSRAMfromFile(2);
          update_memory_map();
        } else {
          saveSRAMtoFile(2);
        }
      } else if (isBreak) f9_held = false;
    } else if (sc == 0x09) {
      if (!isBreak && !f10_held) {
        f10_held = true;
        if (!isShift) {
          loadSRAMfromFile(1);
          update_memory_map();
        } else {
          saveSRAMtoFile(1);
        }
      } else if (isBreak) f10_held = false;
    } else if (sc == 0x78) {
      if (!isBreak && !f11_held) {
        f11_held = true;
        if (!isShift) {
          loadSRAMfromFile(0);
          update_memory_map();
        } else {
          saveSRAMtoFile(0);
        }
      } else if (isBreak) f11_held = false;
    } else if (sc == 0x07) {
      if (!isBreak && !f12_held) {
        f12_held = true;
        if (isShift) {
          ESP.restart();
        } else {
          IntZ80(&cpu, INT_NMI);
        }
      } else if (isBreak) f12_held = false;
    }

    if (isE0) {
      switch (sc) {
        case 0x75:
          if (isBreak) kbdJoyState |= (1 << 0);
          else kbdJoyState &= ~(1 << 0);
          break;
        case 0x72:  // DOWN
          if (isBreak) kbdJoyState |= (1 << 1);
          else kbdJoyState &= ~(1 << 1);
          break;
        case 0x6B:  // LEFT
          if (isBreak) kbdJoyState |= (1 << 2);
          else kbdJoyState &= ~(1 << 2);
          break;
        case 0x74:  // RIGHT
          if (isBreak) kbdJoyState |= (1 << 3);
          else kbdJoyState &= ~(1 << 3);
          break;
        default: break;
      }
    } else {
      switch (sc) {
        case 0x1A:  // Z
          if (isBreak) kbdJoyState |= (1 << 4);
          else kbdJoyState &= ~(1 << 4);
          break;
        case 0x22:  // X
          if (isBreak) kbdJoyState |= (1 << 5);
          else kbdJoyState &= ~(1 << 5);
          break;
        default: break;
      }
    }

    isBreak = false;
    isE0 = false;
  }
}

void IRAM_ATTR updateController() {
  sms_joy_state_a = sms_joy_state_b = 0x3F;

  static bool btn3_held_a = false;
  static bool btn3_held_b = false;

  while (Serial1.available() > 0) {
    uint8_t joy_bits = Serial1.read();
    if (!(joy_bits & 0x70)) ESP.restart();

    if (joy_bits & 0x80) {
      sms_joy_state_b = joy_bits & 0x3F;
      if (!(joy_bits & 0x40)) {
        if (!btn3_held_b) {
          btn3_held_b = true;
          IntZ80(&cpu, INT_NMI);
        }
      } else {
        btn3_held_b = false;
      }

    } else {
      sms_joy_state_a = joy_bits & 0x3F;

      if (!(joy_bits & 0x40)) {
        if (!btn3_held_a) {
          btn3_held_a = true;
          IntZ80(&cpu, INT_NMI);
        }
      } else {
        btn3_held_a = false;
      }
    }
  }

  if ((sms_joy_state_a & 0x3F) == 0x3F) {
    sms_joy_state_a = kbdJoyState & 0x3F;
  }
}

void listROMs() {
  romFiles.clear();

  if (!SD.exists("/ROM")) {
    SD.mkdir("/ROM");
    return;
  }

  File romDir = SD.open("/ROM");
  if (!romDir) return;

  while (File file = romDir.openNextFile()) {
    if (!file.isDirectory()) {
      String name = file.name();
      String ext = name.substring(name.lastIndexOf('.') + 1);
      ext.toUpperCase();

      if (ext == "SMS" || ext == "SG") {
        romFiles.push_back(name);
      }
    }
    file.close();
  }
}

void selectRomMenu() {
  listROMs();
  if (romFiles.empty()) {
    canvas->setBrushColor(RGB888(0, 0, 85));
    canvas->clear();
    canvas->setPenColor(RGB888(255, 255, 255));
    canvas->drawText(64, 90, "NO .SMS OR .SG FILES FOUND IN /ROM");
    canvas->swapBuffers();
    Serial.println("No ROM files found in /ROM/");
    return;
  }

  auto kbd = PS2Controller.keyboard();
  int cursor = 0;
  int prevCursor = 0;
  int topIndex = 0;
  int prevTopIndex = -1;
  const int maxRows = 18;
  bool selected = false;
  bool needRedrawAll = true;

  while (!selected) {
    if (cursor < topIndex) topIndex = cursor;
    if (cursor >= topIndex + maxRows) topIndex = cursor - maxRows + 1;

    if (topIndex != prevTopIndex) {
      needRedrawAll = true;
      prevTopIndex = topIndex;
    }

    if (needRedrawAll) {
      canvas->setBrushColor(RGB888(0, 0, 85));
      canvas->clear();

      canvas->setPenColor(RGB888(255, 255, 255));
      canvas->drawText(40, 5, "--- SEGA MARK3 ROM SELECTOR ---");

      for (int i = 0; i < maxRows && (topIndex + i) < romFiles.size(); ++i) {
        int idx = topIndex + i;
        int y = 25 + (i * 9);

        if (idx == cursor) {
          canvas->setBrushColor(RGB888(170, 0, 0));
          canvas->setPenColor(RGB888(255, 255, 255));
          canvas->fillRectangle(10, y, 310, y + 8);
        } else {
          canvas->setPenColor(RGB888(170, 170, 170));
        }
        canvas->drawText(20, y, romFiles[idx].c_str());
      }
      needRedrawAll = false;
    } else {
      int oldLocalIdx = prevCursor - topIndex;
      if (oldLocalIdx >= 0 && oldLocalIdx < maxRows) {
        int oldY = 25 + (oldLocalIdx * 9);
        canvas->setBrushColor(RGB888(0, 0, 85));
        canvas->fillRectangle(10, oldY, 310, oldY + 8);
        canvas->setPenColor(RGB888(170, 170, 170));
        canvas->drawText(20, oldY, romFiles[prevCursor].c_str());
      }

      int newLocalIdx = cursor - topIndex;
      if (newLocalIdx >= 0 && newLocalIdx < maxRows) {
        int newY = 25 + (newLocalIdx * 9);
        canvas->setBrushColor(RGB888(170, 0, 0));
        canvas->setPenColor(RGB888(255, 255, 255));
        canvas->fillRectangle(10, newY, 310, newY + 8);
        canvas->drawText(20, newY, romFiles[cursor].c_str());
      }
    }

    canvas->swapBuffers();
    prevCursor = cursor;
    bool keyPressed = false;

    int sc;
    static bool isBreak = false;
    while (kbd && (sc = kbd->getNextScancode(0)) != -1) {
      if (sc == 0xF0) {
        isBreak = true;
        continue;
      }
      if (sc == 0xE0) { continue; }

      if (!isBreak) {
        if (sc == 0x75) {
          if (cursor > 0) cursor--;
          keyPressed = true;
        } else if (sc == 0x72) {
          if (cursor < (int)romFiles.size() - 1) cursor++;
          keyPressed = true;
        } else if (sc == 0x1A || sc == 0x22 || sc == 0x5A) {
          selectedRom = "/ROM/" + romFiles[cursor];
          selected = true;
          keyPressed = true;
        }
      }
      isBreak = false;
      if (keyPressed) break;
    }

    if (!keyPressed && !selected) {
      sms_joy_state_a = 0x3F;
      while (Serial1.available() > 0) {
        uint8_t joy_bits = Serial1.read();
        if (!(joy_bits & 0x40)) { IntZ80(&cpu, INT_NMI); }
        if (joy_bits & 0x80) sms_joy_state_b = joy_bits & 0x3F;
        else sms_joy_state_a = joy_bits & 0x3F;
      }

      uint8_t ctrlPressed = ~sms_joy_state_a & 0x3F;
      if (ctrlPressed & (1 << 0)) {
        if (cursor > 0) cursor--;
      } else if (ctrlPressed & (1 << 1)) {
        if (cursor < (int)romFiles.size() - 1) cursor++;
      } else if (ctrlPressed & ((1 << 4) | (1 << 5))) {
        selectedRom = "/ROM/" + romFiles[cursor];
        selected = true;
      }
    }

    if (selected) {
      File f = SD.open(selectedRom.c_str());
      if (f) {
        Serial.println("========================================");
        Serial.printf("Loading ROM: %s\n", romFiles[cursor].c_str());
        Serial.printf("File Size:   %d bytes\n", f.size());
        Serial.println("========================================");
        f.close();
      }
      current_rom_path = selectedRom;
    }

    delay(125);
  }

  if (kbd) {
    while (kbd->getNextScancode(0) != -1) delay(100);
  }
  kbdJoyState = 0x3F;

  canvas->setBrushColor(RGB888(0, 0, 0));
  canvas->clear();
  canvas->swapBuffers();
  canvas->clear();
}

void initMemorySMS() {
  main_ram = (uint8_t*)heap_caps_malloc(0x2000, MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL);
  memset(main_ram, 0, 0x2000);

  bank_ptrs[0] = &game_rom_ptr[0x0000];
  bank_ptrs[1] = &game_rom_ptr[0x4000];
  bank_ptrs[2] = &game_rom_ptr[0x8000];
}

#define CPU_CYCLE_NOWAIT 228
#define CPU_CYCLE_4MHZ 255
void IRAM_ATTR cpuTask(void* pvParameters) {
  const int64_t frameTimeUs = 16684 * 2;  // NTSC 59.94FPS
  int64_t nextFrameTime = esp_timer_get_time();
  static uint8_t renderTiming = 0;
  bool renderFrame = false;

  while (1) {
#ifdef USE_SIGNAL_DEBUG
    digitalWrite(14, HIGH);
#endif
    nextFrameTime += frameTimeUs;
    renderFrame = (renderTiming++ & renderCounter) == 0;

    for (int i = 0; i < 262; ++i) {
      current_scanline = i;

      ExecZ80(&cpu, CPU_CYCLE_NOWAIT);

      if (currentVideoMode == VideoMode::MODE4) {
        smsVdp->scanlineCheck(i);
      } else {
        if (i == 192) {
          smsVdp->status_ |= 0x80;
        }
      }
      if (i < 192) {
        if (currentVideoMode == VideoMode::MODE4) {
          if (renderFrame) {
            smsVdp->renderSingleLine(i);
          }
        }
      }

      if (i == 192) {
        if (currentVideoMode == VideoMode::TMS9918A) {
          if (renderFrame) {
            smsVdp->tms9918a_rasterize();
          }
        }
        if (renderFrame) {
          frameReady = true;
        }
      }

      if ((currentVideoMode == VideoMode::MODE4 ? smsVdp->irqPending() : smsVdp->tms9918a_irq_pending())
          && (cpu.IFF & IFF_1)) {
        IntZ80(&cpu, 0x38);
      }
    }
    for (int i = 0; i < 262; ++i) {
      current_scanline = i;

      ExecZ80(&cpu, CPU_CYCLE_NOWAIT);

      if (currentVideoMode == VideoMode::MODE4) {
        smsVdp->scanlineCheck(i);
      } else {
        if (i == 192) {
          smsVdp->status_ |= 0x80;
        }
      }

      if (i < 192) {
        if (currentVideoMode == VideoMode::MODE4) {
          if (!renderFrame) {
            smsVdp->renderSingleLine(i);
          }
        }
      }

      if (i == 192) {
        if (currentVideoMode == VideoMode::TMS9918A) {
          if (!renderFrame) {
            smsVdp->tms9918a_rasterize();
          }
        }
        if (!renderFrame) {
          frameReady = true;
        }
      }

      if ((currentVideoMode == VideoMode::MODE4 ? smsVdp->irqPending() : smsVdp->tms9918a_irq_pending())
          && (cpu.IFF & IFF_1)) {
        IntZ80(&cpu, 0x38);
      }
    }
#ifdef USE_SIGNAL_DEBUG
    digitalWrite(14, LOW);
#endif
    int64_t now = esp_timer_get_time();
    int64_t waitTime = nextFrameTime - now;

    if (waitTime > 0) {
      if (waitTime >= 1000) {
        vTaskDelay(pdMS_TO_TICKS(waitTime / 1000));
      }
      while (esp_timer_get_time() < nextFrameTime) {
        asm volatile("nop");
      }
    }
  }
}

void printMemoryStats() {
  size_t free_dram = heap_caps_get_free_size(MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL);
  size_t max_dram_block = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL);
  size_t free_iram = heap_caps_get_free_size(MALLOC_CAP_EXEC | MALLOC_CAP_INTERNAL);
  size_t max_iram_block = heap_caps_get_largest_free_block(MALLOC_CAP_EXEC | MALLOC_CAP_INTERNAL);
  size_t free_psram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);

  Serial.println("\n=== [ESP32 Memory Status at the end of setup()] ===");
  Serial.printf(" 内部高速RAM (DRAM) 空き容量 : %6d バイト (約 %5.1f KB)\n", free_dram, free_dram / 1024.0);
  Serial.printf(" 内部高速RAM (DRAM) 最大連続 : %6d バイト (連続して確保可能な最大サイズ)\n", max_dram_block);
  Serial.printf(" IRAM (Instruction) 空き容量 : %6d バイト (約 %5.1f KB)\n", free_iram, free_iram / 1024.0);
  Serial.printf(" IRAM (Instruction) 最大連続 : %6d バイト\n", max_iram_block);

  if (free_psram > 0) {
    Serial.printf(" 外部 PSRAM         空き容量 : %6d バイト (約 %5.1f KB)\n", free_psram, free_psram / 1024.0);
  } else {
    Serial.println(" 外部 PSRAM         空き容量 : 0 バイト (未搭載、または無効設定)");
  }
  Serial.println("===================================================\n");
}

void setup() {
  Serial.begin(115200);
  Serial1.begin(115200, SERIAL_8N1, 39, -1);
  WiFi.mode(WIFI_OFF);
  setCpuFrequencyMhz(240);

  disableCore0WDT();
  disableCore1WDT();
  rtc_wdt_protect_off();
  rtc_wdt_disable();
  esp_task_wdt_delete(NULL);
  delay(100);

  PS2Controller.begin(PS2Preset::KeyboardPort0_MousePort1, fabgl::KbdMode::NoVirtualKeys);
  PS2Controller.keyboard()->begin(GPIO_NUM_33, GPIO_NUM_32, false, false);
  sms_joy_state_a = 0xFF;
  sms_joy_state_b = 0xFF;

  main_ram = (uint8_t*)heap_caps_malloc(0x2000, MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL);
  memset(main_ram, 0, 0x2000);

  g_psram_state = (SaveState*)ps_malloc(sizeof(SaveState));
  if (g_psram_state) {
    memset(g_psram_state, 0, sizeof(SaveState));
    Serial.printf("SaveState allocated: %d bytes\n", sizeof(SaveState));
  } else {
    Serial.println("SaveState PSRAM alloc FAILED!");
  }

  DisplayController.begin();
  DisplayController.setResolution(VGA_320x200_70Hz, -1, -1, false);
  canvas = new fabgl::Canvas(&DisplayController);

  canvas->setBrushColor(fabgl::Color::Black);
  canvas->clear();
  canvas->swapBuffers();
  canvas->clear();

  soundGenerator.attach(&psgGen);
  for (int i = 0; i < 5; i++) {
    scc_ch[i] = new SCCGenerator();
    soundGenerator.attach(scc_ch[i]);
    scc_ch[i]->enable(true);
  }

  Serial.println("Allocating FM Generator on PSRAM...");
  fmGen = new YM2413Generator();
  Serial.println("FM Generator allocated successfully!");

  soundGenerator.attach(fmGen);
  fmGen->enable(true);
  fmGen->resetOPLL();
  ym2413_enabled = false;

  soundGenerator.setVolume(100);
  soundGenerator.play(true);
  psgGen.enable(true);
  initSCCPresets();

  SPI.begin(SD_SCLK, SD_MISO, SD_MOSI, SD_CS);

  if (SD.begin(SD_CS, SPI)) {
    selectRomMenu();

    if (selectedRom != "") {
      File romFile = SD.open(selectedRom.c_str());
      if (romFile) {
        uint32_t original_size = romFile.size();

        uint32_t header_offset = 0;
        if ((original_size % 0x4000) == 512) {
          header_offset = 512;
          original_size -= 512;
        }

        rom_size = 1;
        while (rom_size < original_size) rom_size <<= 1;
        if (rom_size < 0x8000) rom_size = 0x8000;

        bank_mask = (rom_size / 0x4000) - 1;

        if (game_rom_ptr) free(game_rom_ptr);
        game_rom_ptr = (uint8_t*)ps_malloc(rom_size);

        if (game_rom_ptr) {
          memset(game_rom_ptr, 0xFF, rom_size);
          romFile.seek(header_offset);
          {
            size_t totalRead = 0;
            while (totalRead < original_size) {
              size_t chunk = min((size_t)4096, (size_t)(original_size - totalRead));
              size_t n = romFile.read(game_rom_ptr + totalRead, chunk);
              if (n == 0) break;
              totalRead += n;
            }
            Serial.printf("ROM bytes read: %u / %u\n", (unsigned)totalRead, (unsigned)original_size);
          }

          sega_mapper_locked = false;
          sram_enabled = false;
          smsMapper = SMS_MAPPER_SEGA;

          currentVideoMode = determineVideoMode(selectedRom, game_rom_ptr, rom_size);
          Serial.printf("Selected ROM: %s -> video mode %s\n",
                        selectedRom.c_str(),
                        currentVideoMode == VideoMode::MODE4 ? "MODE4" : "TMS9918A");

          Serial.printf("ROM Loaded to PSRAM: %d bytes\n", original_size);

          uint32_t num_banks = rom_size / 0x4000;
          bank_ptrs[0] = &game_rom_ptr[0 * 0x4000];
          bank_ptrs[1] = &game_rom_ptr[(1 % num_banks) * 0x4000];
          bank_ptrs[2] = &game_rom_ptr[(2 % num_banks) * 0x4000];

          Serial.println("Memory mapping initialized.");

          smsVdp = new VDP(g_vdp_vram, g_vdp_cram, g_vdp_cramRGB, g_vdp_reg);
          smsVdp->reset();
          smsBitmap = new fabgl::Bitmap(256, 192, smsVdp->visible_raster, fabgl::PixelFormat::Native);
        }
      } else {
        Serial.println("Failed to allocate PSRAM for ROM!");
      }
      romFile.close();
    }
  }

  memset(&cpu, 0, sizeof(Z80));
  ResetZ80(&cpu);
  cpu.IPeriod = 224;
  cpu.SPtr.W = 0xDFFF;

  printMemoryStats();

#ifndef USE_SERIAL_DEBUG
  Serial.println("Sega Mark III / SMS Booting...");
  Serial.end();
#else
  Serial.println("Sega Mark III / SMS Booting with Debug Serial...");
#endif

#ifdef USE_SIGNAL_DEBUG
  SPI.end();
  SD.end();
  pinMode(14, OUTPUT);
  pinMode(2, OUTPUT);
#endif
  update_memory_map();
  xTaskCreatePinnedToCore(cpuTask, "cpuTask", 8192, NULL, 24, NULL, 0);
}

void updateAllSound() {
  psgGen.fillBuffer();

  if (psg2scc) {
    updateSCCSound();
    for (int i = 0; i < 3; i++) {
      if (scc_ch[i]) {
        scc_ch[i]->fillBuffer();
      }
    }
  } else {
    for (int i = 0; i < 5; i++) {
      if (scc_ch[i]) {
        scc_ch[i]->updateParams((int8_t*)scc_waveform[0], 0, 0);
        scc_ch[i]->fillBuffer();  // 明示的に無音でバッファを埋める
      }
    }
    fmGen->fillBuffer();
  }
}

void loop() {
  updateAllSound();
  if (frameReady) {
    frameReady = false;
#ifdef USE_SIGNAL_DEBUG
    digitalWrite(2, HIGH);
#endif
    smsVdp->flip_buffer();
    smsBitmap->data = smsVdp->visible_raster;
    canvas->drawBitmap(32, 0, smsBitmap);
    DisplayController.processPrimitives();
    updateController();
    updateKeyboard();
#ifdef USE_SIGNAL_DEBUG
    digitalWrite(2, LOW);
#endif
  }
}