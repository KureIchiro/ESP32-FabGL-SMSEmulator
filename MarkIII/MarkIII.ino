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
#include "soc/rtc_wdt.h"
#include "esp_task_wdt.h"
#include "Z80.h"
#include "vdp.h"
#include "emu2413.h"

#define SD_SCLK 14
#define SD_MISO 2
#define SD_MOSI 12
#define SD_CS 13

#define SD_ROOT "/SMS"

extern "C" {
  uint8_t readByte(void* context, uint16_t addr);
  void writeByte(void* context, uint16_t addr, uint8_t val);
  int readIO(void* context, int port);
  void writeIO(void* context, int port, int val);

  extern Z80 cpu;

  void WrZ80Slow(zword Addr, byte Value);
  byte InZ80(zword Port);
  void OutZ80(zword Port, byte Value);
  void PatchZ80(Z80* R);
}

Z80 cpu;

void IRAM_ATTR WrZ80Slow(zword Addr, byte Value) {
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
static volatile bool nmiRequest = false;
void updateFabGLSound();

fabgl::VGAController DisplayController;
fabgl::Canvas* canvas;
fabgl::Bitmap* smsBitmap;
fabgl::SoundGenerator soundGenerator;
fabgl::NoiseWaveformGenerator noiseGen;
fabgl::PS2Controller PS2Controller;

int8_t scc_waveform[6][32] = { 0 };
uint16_t scc_f[] = { 0, 0, 0, 0, 0, 0 };
uint8_t scc_v[] = { 0, 0, 0, 0, 0, 0 };
uint8_t scc_enable = 0x07;
bool psg2scc = false;

static const int BUFFER_SIZE = 512;
const uint8_t SCC_VOL_TABLE[16] = {
  0, 2, 4, 7, 10, 15, 21, 26, 33, 39, 47, 54, 62, 68, 73, 75
};

uint8_t SCC_Ys2_Lead[32] = {
  0x00, 0x02, 0x06, 0x0A, 0x0F, 0x13, 0x18, 0x1D, 0x23, 0x2A, 0x34, 0x41, 0x53, 0x6C, 0x7C, 0x5A,
  0x00, 0xA6, 0x84, 0x94, 0xAD, 0xBF, 0xCC, 0xD6, 0xDD, 0xE3, 0xE8, 0xED, 0xF1, 0xF6, 0xFA, 0xFE
};

uint8_t SCC_Ys2_Back[32] = {
  0x29, 0x6D, 0x7C, 0x79, 0x7B, 0x79, 0x7C, 0x6D, 0x29, 0xE5, 0xD5, 0xD8, 0xD7, 0xD8, 0xD7, 0xD7,
  0xD7, 0xD7, 0xD7, 0xD7, 0xD7, 0xD7, 0xD7, 0xD7, 0xD7, 0xD7, 0xD7, 0xD8, 0xD7, 0xD8, 0xD5, 0xE5
};

uint8_t SCC_Ys2_Bass[32] = {
  0x00, 0x0B, 0x15, 0x20, 0x2B, 0x36, 0x42, 0x4D, 0x59, 0x66, 0x72, 0x7B, 0x7C, 0x71, 0x58, 0x30,
  0x00, 0xD0, 0xA8, 0x8F, 0x84, 0x85, 0x8E, 0x9A, 0xA7, 0xB3, 0xBE, 0xCA, 0xD5, 0xE0, 0xEB, 0xF5
};

void initSCCPresets() {
  for (int i = 0; i < 32; i++) {
    scc_waveform[0][i] = (int8_t)SCC_Ys2_Lead[i] - 128;
    scc_waveform[1][i] = (int8_t)SCC_Ys2_Back[i] - 128;
    scc_waveform[2][i] = (int8_t)SCC_Ys2_Bass[i] - 128;
  }
}

String selectedRom;
std::vector<String> romFiles;
String current_dir = SD_ROOT "/ROM";
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

extern "C" { uint8_t* main_ram = nullptr; }
uint8_t* game_rom_ptr = nullptr;
uint8_t* bank_ptrs[3];
uint32_t rom_size = 0;
uint8_t cart_ram[32 * 1024];
volatile uint32_t sramChanges = 0;
bool sram_enabled = false;
uint8_t sram_bank = 0;
enum SmsMapperType { SMS_MAPPER_SEGA,
                     SMS_MAPPER_CODEMASTERS };
SmsMapperType smsMapper = SMS_MAPPER_SEGA;
uint32_t bank_mask = 0;
bool sega_mapper_locked = false;
extern "C" { uint8_t* mem_read_map[64]; }
std::vector<String> cachedRomFiles;
String cachedDir = "";

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
static uint8_t **mk3_rows = nullptr;
static bool mk3_direct = false;

static DRAM_ATTR uint8_t g_vdp_vram[16384];
static DRAM_ATTR uint8_t g_vdp_cram[32];
static DRAM_ATTR uint8_t g_vdp_cramRGB[32];
static DRAM_ATTR uint8_t g_vdp_reg[16];

struct SaveState {
    char     magic[4];
    bool     valid;
    VDPState vdp;
    Z80      cpu_state;
    uint8_t  ram[0x2000];
    uint8_t  sram_data[32768];
    uint8_t  bank_regs[3];
    bool     sram_enabled;
    uint8_t  sram_bank;
    uint8_t  mapper_type;
    bool     mapper_locked;
    OPLL     fm_opll;
    uint8_t  fm_patch_idx[18];
    bool     ym2413_en;
    bool     psg2scc_en;
};
static SaveState* g_psram_state = nullptr;

class YM2413Generator : public fabgl::WaveformGenerator {
private:
  OPLL* opll = nullptr;
  int16_t sampleBuffer[BUFFER_SIZE];
  volatile int head = 0;
  volatile int tail = 0;
  int lastSample = 0;

  static const int32_t OPLL_CLOCK = 3579545;

  static const int LP_SHIFT = 2;
  int32_t lpState = 0;

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

  volatile bool touched_ = false;
  bool touched() const { return touched_; }

  void writePort(uint8_t isDataPort, uint8_t val) {
    touched_ = true;
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
      int32_t raw = OPLL_calc(opll) >> 4;
      lpState += (raw - lpState) >> LP_SHIFT;
      sampleBuffer[head] = (int16_t)lpState;
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
    touched_ = false;
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

    touched_ = true;
    clearBuffer();
  }

  void getFullState(OPLL* dest, uint8_t* patch_idx) {
    if (!opll) return;
    memcpy(dest, opll, sizeof(OPLL));
    for (int i = 0; i < 18; i++)
      patch_idx[i] = (uint8_t)(opll->slot[i].patch - opll->patch);
  }

  void setFullState(const OPLL* src, const uint8_t* patch_idx) {
    if (!opll) return;
    OPLL_RateConv* keepConv = opll->conv;
    memcpy(opll, src, sizeof(OPLL));
    opll->conv = keepConv;
    for (int i = 0; i < 18; i++)
      opll->slot[i].patch = &opll->patch[patch_idx[i] < 38 ? patch_idx[i] : 0];
    touched_ = true;
    clearBuffer();
  }

  void clearBuffer() {
    cmdHead = 0;
    cmdTail = 0;
    head = 0;
    tail = 0;
    lastSample = 0;
    lpState = 0;
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
      ch->updateParams((int8_t*)scc_waveform[i], scc_f[i], SCC_VOL_TABLE[scc_v[i]]);
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
  int16_t volTable[16] = { 90, 87, 81, 75, 64, 57, 46, 40, 31, 26, 18, 12, 9, 5, 3, 0 };
  int16_t sampleBuffer[BUFFER_SIZE];
  volatile int head = 0;
  volatile int tail = 0;

  static const int LP_SHIFT = 2;
  int32_t lpState = 0;

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
    lpState = 0;
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
    if (!psg2scc) {
    if (toneOut[0]) mixedSample += volTable[curVol[0]];
    if (toneOut[1]) mixedSample += volTable[curVol[1]];
    if (toneOut[2]) mixedSample += volTable[curVol[2]];
    }
    if (noiseOut) mixedSample += volTable[curVol[3]];

    int32_t raw = mixedSample - 128;
    lpState += (raw - lpState) >> LP_SHIFT;
    return (int16_t)lpState;
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
      uint8_t &d = cart_ram[(sram_bank * 0x4000) + (addr & 0x3FFF)];
      if (d != val) { d = val; sramChanges++; }
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

  if (likely((p & 0xC0) == 0x80)) {
    if (likely(currentVideoMode == VideoMode::MODE4)) {
      return (p & 1) ? smsVdp->readControl() : smsVdp->readData();
    }
    return smsVdp->tms9918a_read(p & 0x01);
  }

  if ((p & 0xC1) == 0x40) {
    int ln = current_scanline;
    return (uint8_t)(ln <= 0xDA ? ln : ln - 6);
  }

  if (p == 0xF2) {
    return ym2413_enabled ? 0x01 : 0x00;
  }

  if ((p & 0xC1) == 0xC0) {
    uint8_t v = sms_joy_state_a & 0x3F;
    v |= (sms_joy_state_b & 0x01) << 6;
    v |= (sms_joy_state_b & 0x02) << 6;
    return v;
  }

  if ((p & 0xC1) == 0xC1) {
    uint8_t v = ((sms_joy_state_b >> 2) & 0x0F) | 0x30;
    if (smsRegionExport) {
      v |= (sms_io_port_3F & 0x02) ? ((sms_io_port_3F & 0x20) ? 0x40 : 0x00) : 0x40;
      v |= (sms_io_port_3F & 0x08) ? ((sms_io_port_3F & 0x80) ? 0x80 : 0x00) : 0x80;
    } else {
      v |= 0xC0;
    }
    return v;
  }

  return 0xFF;
}

inline void IRAM_ATTR writeIO(void* context, int port, int val) {
  uint8_t p = (uint8_t)port;
  uint8_t v = (uint8_t)val;

  if (likely((p & 0xC0) == 0x80)) {
    if (likely(currentVideoMode == VideoMode::MODE4)) {
      if (p & 1) smsVdp->writeControl(v);
      else smsVdp->writeData(v);
    } else {
      smsVdp->tms9918a_write(p & 0x01, v);
    }
    return;
  }

  if ((p & 0xC0) == 0x40) {
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

  if ((p & 0xC1) == 0x00) {
    sms_memory_control = v;
    return;
  }
  if ((p & 0xC1) == 0x01) {
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
    return SD_ROOT "/ROM/SAVE/default_" + String(slotNumber) + ".sav";
  }

  int slashIndex = current_rom_path.lastIndexOf('/');
  String fileName = (slashIndex == -1) ? current_rom_path : current_rom_path.substring(slashIndex + 1);

  int dotIndex = fileName.lastIndexOf('.');
  if (dotIndex != -1) {
    fileName = fileName.substring(0, dotIndex);
  }

  String slotSuffix = "";
  slotSuffix = "_" + String(slotNumber);
  return SD_ROOT "/ROM/SAVE/" + fileName + slotSuffix + ".sav";
}

bool writeSramFile(int slotNumber) {
  if (!SD.exists(SD_ROOT "/ROM/SAVE")) { SD.mkdir(SD_ROOT); SD.mkdir(SD_ROOT "/ROM"); SD.mkdir(SD_ROOT "/ROM/SAVE"); }
  File file = SD.open(getSaveFilePath(slotNumber), FILE_WRITE);
  if (!file) return false;
  size_t n = file.write(cart_ram, sizeof(cart_ram));
  file.close();
  return n == sizeof(cart_ram);
}

bool readSramFile(int slotNumber) {
  String savePath = getSaveFilePath(slotNumber);
  if (!SD.exists(savePath)) return false;
  File file = SD.open(savePath, FILE_READ);
  if (!file) return false;
  file.read(cart_ram, sizeof(cart_ram));
  file.close();
  return true;
}

void saveSRAMtoFile(int slotNumber) {
  flashStatus(writeSramFile(slotNumber) ? "SRAM Saved!" : "SRAM Save Failed!", 1000);
}

void loadSRAMfromFile(int slotNumber) {
  String savePath = getSaveFilePath(slotNumber);
  if (!SD.exists(savePath)) {
    flashStatus("No SRAM File Found", 1500);
    return;
  }
  flashStatus(readSramFile(slotNumber) ? "SRAM Loaded!" : "SRAM Load Failed!", 1000);
}

void autoSaveSram() {
  static uint32_t seen = 0, changedAt = 0;
  static bool pending = false;
  uint32_t n = sramChanges;
  if (n != seen) { seen = n; changedAt = millis(); pending = true; return; }
  if (pending && (uint32_t)(millis() - changedAt) >= 2000) {
    pending = false;
    if (!writeSramFile(0)) flashStatus("SRAM Save Failed!", 1000);
  }
}

String getStateFilePath() {
  if (current_rom_path.length() == 0) return SD_ROOT "/ROM/SAVE/default_st.sav";
  int slash = current_rom_path.lastIndexOf('/');
  String name = (slash == -1) ? current_rom_path : current_rom_path.substring(slash + 1);
  int dot = name.lastIndexOf('.');
  if (dot != -1) name = name.substring(0, dot);
  return SD_ROOT "/ROM/SAVE/" + name + "_st.sav";
}

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

void saveStateToSD() {
  if (!g_psram_state || !g_psram_state->valid) {
    flashStatus("NO STATE IN PSRAM", 1500);
    return;
  }
  if (!SD.exists(SD_ROOT "/ROM/SAVE")) { SD.mkdir(SD_ROOT); SD.mkdir(SD_ROOT "/ROM"); SD.mkdir(SD_ROOT "/ROM/SAVE"); }
  String path = getStateFilePath();
  File f = SD.open(path, FILE_WRITE);
  if (!f) { flashStatus("STATE SD WRITE FAIL", 2000); return; }
  f.write((const uint8_t*)g_psram_state, sizeof(SaveState));
  f.close();
  flashStatus("STATE -> SD OK", 1000);
}

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
  flashStatus("SD -> PSRAM OK", 1000);
}

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

    if (sc == 0x03) {
      if (!isBreak && !f5_held) {
        f5_held = true;
        if (isShift) {
          saveStateToSD();
        } else {
          loadStateFromSD();
        }
      } else if (isBreak) f5_held = false;
    } else if (sc == 0x0B) {
      if (!isBreak && !f6_held) {
        f6_held = true;
        if (isShift) {
          saveStateToPSRAM();
        } else {
          loadStateFromPSRAM();
        }
      } else if (isBreak) f6_held = false;
    } else if (sc == 0x83) {
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
          if (smsVdp) {
            sprintf(msgBuf, "R0=%02X R1=%02X R8=%02X R9=%02X",
                    smsVdp->reg_[0], smsVdp->reg_[1], smsVdp->reg_[8], smsVdp->reg_[9]);
            flashStatus(msgBuf, 3000);
          }
        }
      } else if (isBreak) f7_held = false;
    } else if (sc == 0x0A) {
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
          nmiRequest = true;
        }
      } else if (isBreak) f12_held = false;
    }

    if (isE0) {
      switch (sc) {
        case 0x75:
          if (isBreak) kbdJoyState |= (1 << 0);
          else kbdJoyState &= ~(1 << 0);
          break;
        case 0x72:
          if (isBreak) kbdJoyState |= (1 << 1);
          else kbdJoyState &= ~(1 << 1);
          break;
        case 0x6B:
          if (isBreak) kbdJoyState |= (1 << 2);
          else kbdJoyState &= ~(1 << 2);
          break;
        case 0x74:
          if (isBreak) kbdJoyState |= (1 << 3);
          else kbdJoyState &= ~(1 << 3);
          break;
        default: break;
      }
    } else {
      switch (sc) {
        case 0x1A:
          if (isBreak) kbdJoyState |= (1 << 4);
          else kbdJoyState &= ~(1 << 4);
          break;
        case 0x22:
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
          nmiRequest = true;
        }
      } else {
        btn3_held_b = false;
      }

    } else {
      sms_joy_state_a = joy_bits & 0x3F;

      if (!(joy_bits & 0x40)) {
        if (!btn3_held_a) {
          btn3_held_a = true;
          nmiRequest = true;
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
  if (cachedDir == current_dir && !cachedRomFiles.empty()) {
    romFiles = cachedRomFiles;
    return;
  }

  romFiles.clear();

  const String EXT_SMS = "SMS";
  const String EXT_SG = "SG";

  if (!SD.exists(current_dir.c_str())) {
    if (current_dir == SD_ROOT "/ROM") { SD.mkdir(SD_ROOT); SD.mkdir(SD_ROOT "/ROM"); return; }
    else { current_dir = SD_ROOT "/ROM"; }
  }

  if (current_dir != SD_ROOT "/ROM" && current_dir != SD_ROOT "/ROM/") {
    romFiles.push_back("..");
  }

  File dir = SD.open(current_dir.c_str());
  if (!dir || !dir.isDirectory()) return;

  while (File file = dir.openNextFile()) {
    String name = file.name();

    if (file.isDirectory()) {
      romFiles.push_back(name + "/");
    } else {
      int dotIdx = name.lastIndexOf('.');
      if (dotIdx != -1) {
        String ext = name.substring(dotIdx + 1);
        ext.toUpperCase();
        if (ext == EXT_SMS || ext == EXT_SG) {
          romFiles.push_back(name);
        }
      }
    }
    file.close();
  }
  dir.close();

  cachedRomFiles = romFiles;
  cachedDir = current_dir;
}

void selectRomMenu() {
  current_dir = SD_ROOT "/ROM";
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
  auto penFor = [&](int idx, bool sel) {
    const String &s = romFiles[idx];
    if (sel) canvas->setPenColor(RGB888(255, 255, 85));
    else if (s == ".." || s.endsWith("/")) canvas->setPenColor(RGB888(85, 255, 255));
    else canvas->setPenColor(RGB888(255, 255, 255));
  };
  int cursor = 0;
  int prevCursor = 0;
  int topIndex = 0;
  int prevTopIndex = -1;
  const int maxRows = 18;
  bool selected = false;
  bool needRedrawAll = true;
  bool needClear = true;

  while (!selected) {
    if (cursor < topIndex) topIndex = cursor;
    if (cursor >= topIndex + maxRows) topIndex = cursor - maxRows + 1;

    if (topIndex != prevTopIndex) {
      needRedrawAll = true;
      prevTopIndex = topIndex;
    }

    if (needRedrawAll) {
      if (needClear) {
        canvas->setBrushColor(RGB888(0, 0, 85));
        canvas->clear();
        canvas->setPenColor(RGB888(255, 255, 255));
        canvas->drawText(40, 5, "--- SEGA MARK3 ROM SELECTOR ---");
        needClear = false;
      }

      for (int i = 0; i < maxRows; ++i) {
        int idx = topIndex + i;
        int y = 25 + (i * 9);
        canvas->setBrushColor(RGB888(0, 0, 85));
        canvas->fillRectangle(10, y, 310, y + 8);
        if (idx >= (int)romFiles.size()) continue;
        penFor(idx, idx == cursor);
        canvas->drawText(20, y, romFiles[idx].c_str());
      }
      needRedrawAll = false;
    } else {
      int oldLocalIdx = prevCursor - topIndex;
      if (oldLocalIdx >= 0 && oldLocalIdx < maxRows) {
        int oldY = 25 + (oldLocalIdx * 9);
        canvas->setBrushColor(RGB888(0, 0, 85));
        canvas->fillRectangle(10, oldY, 310, oldY + 8);
        penFor(prevCursor, false);
        canvas->drawText(20, oldY, romFiles[prevCursor].c_str());
      }

      int newLocalIdx = cursor - topIndex;
      if (newLocalIdx >= 0 && newLocalIdx < maxRows) {
        int newY = 25 + (newLocalIdx * 9);
        canvas->setBrushColor(RGB888(0, 0, 85));
        canvas->fillRectangle(10, newY, 310, newY + 8);
        penFor(cursor, true);
        canvas->drawText(20, newY, romFiles[cursor].c_str());
      }
    }

    if (needRedrawAll || prevCursor != cursor) {
      canvas->swapBuffers();
    }
    prevCursor = cursor;
    bool keyPressed = false;
    bool doSelect = false;

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
          doSelect = true;
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
        if (joy_bits & 0x80) sms_joy_state_b = joy_bits & 0x3F;
        else sms_joy_state_a = joy_bits & 0x3F;
      }

      uint8_t ctrlPressed = ~sms_joy_state_a & 0x3F;
      if (ctrlPressed & (1 << 0)) {
        if (cursor > 0) cursor--;
      } else if (ctrlPressed & (1 << 1)) {
        if (cursor < (int)romFiles.size() - 1) cursor++;
      } else if (ctrlPressed & ((1 << 4) | (1 << 5))) {
        doSelect = true;
      }
    }

    if (doSelect) {
      String chosen = romFiles[cursor];

      if (chosen == "..") {
        int lastSlash = current_dir.lastIndexOf('/');
        if (lastSlash > 0) {
          current_dir = current_dir.substring(0, lastSlash);
        } else {
          current_dir = SD_ROOT "/ROM";
        }
        listROMs();
        cursor = 0; prevCursor = 0; topIndex = 0; prevTopIndex = -1; needRedrawAll = true; needClear = true;
      } else if (chosen.endsWith("/")) {
        String folderName = chosen.substring(0, chosen.length() - 1);
        if (current_dir.endsWith("/")) current_dir += folderName;
        else current_dir += "/" + folderName;
        listROMs();
        cursor = 0; prevCursor = 0; topIndex = 0; prevTopIndex = -1; needRedrawAll = true; needClear = true;
      } else {
        if (current_dir.endsWith("/")) selectedRom = current_dir + chosen;
        else selectedRom = current_dir + "/" + chosen;
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
    delay(80);
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
#ifdef USE_SERIAL_DEBUG2
int64_t cpuStatMinUs = INT64_MAX, cpuStatMaxUs = 0, cpuStatSumUs = 0, cpuStatLastPrintUs = 0;
uint32_t cpuStatCount = 0;
int64_t loopStatMinUs = INT64_MAX, loopStatMaxUs = 0, loopStatSumUs = 0, loopStatLastPrintUs = 0;
uint32_t loopStatCount = 0;

void updateLoadStats(int64_t* minUs, int64_t* maxUs, int64_t* sumUs, uint32_t* count, int64_t* lastPrintUs, const char* label, int64_t elapsedUs) {
  if (elapsedUs < *minUs) *minUs = elapsedUs;
  if (elapsedUs > *maxUs) *maxUs = elapsedUs;
  *sumUs += elapsedUs;
  (*count)++;

  int64_t now = esp_timer_get_time();
  if (now - *lastPrintUs >= 1000000) {
    int64_t avgUs = (*count > 0) ? (*sumUs / *count) : 0;
    Serial.printf("%s L%lld M%lld A%lld\n", label, (long long)*minUs, (long long)*maxUs, (long long)avgUs);

    *minUs = INT64_MAX;
    *maxUs = 0;
    *sumUs = 0;
    *count = 0;
    *lastPrintUs = now;
  }
}
#endif

void IRAM_ATTR cpuTask(void* pvParameters) {
  const int64_t frameTimeUs = 16684;
  int64_t nextFrameTime = esp_timer_get_time();
  static uint8_t renderTiming = 0;
  bool renderFrame = false;

#ifdef USE_SERIAL_DEBUG2
  Serial.printf("cpuTask started. (core=%d)\n", xPortGetCoreID());
#endif

  while (1) {
#if defined(USE_SIGNAL_DEBUG) || defined(USE_SERIAL_DEBUG2)
    int64_t cpuProfStart = esp_timer_get_time();
#endif
#ifdef USE_SIGNAL_DEBUG
    digitalWrite(14, HIGH);
#endif
    nextFrameTime += frameTimeUs;
    renderFrame = 1;

    for (int i = 0; i < 262; ++i) {
      current_scanline = i;

      ExecZ80(&cpu, CPU_CYCLE_NOWAIT);
      if (nmiRequest) { nmiRequest = false; IntZ80(&cpu, INT_NMI); }

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

#ifdef USE_SIGNAL_DEBUG
    digitalWrite(14, LOW);
#endif
#ifdef USE_SERIAL_DEBUG2
    updateLoadStats(&cpuStatMinUs, &cpuStatMaxUs, &cpuStatSumUs, &cpuStatCount, &cpuStatLastPrintUs, "CPU", esp_timer_get_time() - cpuProfStart);
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
  for (int i = 0; i < 3; i++) {
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

  soundGenerator.setVolume(64);
  soundGenerator.play(true);
  psgGen.enable(true);
  initSCCPresets();

  SPI.begin(SD_SCLK, SD_MISO, SD_MOSI, SD_CS);

  if (SD.begin(SD_CS, SPI, 40000000)) {
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

  if (smsVdp && currentVideoMode == VideoMode::MODE4) {
    DisplayController.processPrimitives();
    mk3_rows = (uint8_t**)heap_caps_malloc(sizeof(uint8_t*) * SCREEN_H,
                                           MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL);
    bool ok = (mk3_rows != nullptr);
    for (int y = 0; ok && y < SCREEN_H; y++) {
      uint8_t *row = DisplayController.getScanline(y);
      if (!row || ((uintptr_t)row & 3)) {
        Serial.printf("VIEWPORT row %d is NULL or unaligned (%p) -> drawBitmap path\n", y, row);
        ok = false;
        break;
      }
      mk3_rows[y] = row + 32;
    }
    if (ok) {
      uint8_t sync = DisplayController.createRawPixel(fabgl::RGB222(0, 0, 0));
      smsVdp->setDirectOutput(mk3_rows, sync);
      mk3_direct = true;
#if defined(USE_SERIAL_DEBUG) || defined(USE_SERIAL_DEBUG2)
      Serial.printf("DIRECT VIEWPORT: row0=%p row1=%p stride=%d rawBlack=%02X\n",
                    mk3_rows[0], mk3_rows[1], (int)(mk3_rows[1] - mk3_rows[0]), sync);
#endif
    }
  }

  memset(cart_ram, 0, sizeof(cart_ram));
  if (readSramFile(0)) Serial.println("SRAM (slot 0) loaded");

  memset(&cpu, 0, sizeof(Z80));
  ResetZ80(&cpu);
  cpu.IPeriod = 224;
  cpu.SPtr.W = 0xDFFF;

  printMemoryStats();

#if !defined(USE_SERIAL_DEBUG) && !defined(USE_SERIAL_DEBUG2)
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
        scc_ch[i]->fillBuffer();
      }
    }
    if (ym2413_enabled || fmGen->touched()) {
      fmGen->fillBuffer();
    }
  }
}

#ifdef USE_SERIAL_DEBUG
static int64_t c1_snd = 0, c1_blit = 0, c1_inp = 0;
static int64_t c1_winStart = 0;
static int c1_iter = 0, c1_frames = 0;
#endif

void loop() {
#ifdef USE_SERIAL_DEBUG
  int64_t t0 = esp_timer_get_time();
  if (c1_winStart == 0) c1_winStart = t0;
#endif

  updateAllSound();

#ifdef USE_SERIAL_DEBUG
  int64_t t1 = esp_timer_get_time();
  c1_snd += t1 - t0;
  c1_iter++;
#endif

  if (frameReady) {
    frameReady = false;
#if defined(USE_SIGNAL_DEBUG) || defined(USE_SERIAL_DEBUG2)
    int64_t loopProfStart = esp_timer_get_time();
#endif
#ifdef USE_SIGNAL_DEBUG
    digitalWrite(2, HIGH);
#endif
    if (mk3_direct) {
      DisplayController.processPrimitives();
    } else {
      smsVdp->flip_buffer();
      smsBitmap->data = smsVdp->visible_raster;
      canvas->drawBitmap(32, 0, smsBitmap);
      DisplayController.processPrimitives();
    }
#ifdef USE_SERIAL_DEBUG
    int64_t t2 = esp_timer_get_time();
    c1_blit += t2 - t1;
    c1_frames++;
#endif
    updateController();
    updateKeyboard();
    autoSaveSram();
#ifdef USE_SERIAL_DEBUG
    c1_inp += esp_timer_get_time() - t2;
#endif
#ifdef USE_SIGNAL_DEBUG
    digitalWrite(2, LOW);
#endif
#ifdef USE_SERIAL_DEBUG2
    updateLoadStats(&loopStatMinUs, &loopStatMaxUs, &loopStatSumUs, &loopStatCount, &loopStatLastPrintUs, "LOOP", esp_timer_get_time() - loopProfStart);
#endif
  } else {
    vTaskDelay(1);
  }

#ifdef USE_SERIAL_DEBUG
  {
    int64_t now = esp_timer_get_time();
    if (now - c1_winStart >= 1000000) {
      int64_t busy = c1_snd + c1_blit + c1_inp;
      Serial.printf("C1/s: snd=%6lld blit=%6lld inp=%5lld busy=%6lld (%2lld%%) frames=%2d iter=%4d | blit/frame=%5lld\n",
                    (long long)c1_snd, (long long)c1_blit, (long long)c1_inp,
                    (long long)busy, (long long)(busy / 10000),
                    c1_frames, c1_iter,
                    (long long)(c1_frames ? c1_blit / c1_frames : 0));
      c1_snd = c1_blit = c1_inp = 0;
      c1_iter = c1_frames = 0;
      c1_winStart = now;
    }
  }
#endif
}
