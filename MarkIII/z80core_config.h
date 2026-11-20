/* z80core_config.h - Mark III: the memory map of MarkIII.ino for z80core
   Reads (opcodes and data) use the 1 KB page table mem_read_map (the Sega mapper keeps the first 1 KB fixed).
   Writes to the RAM (C000h-FFFBh) go straight to main_ram; the others (mapper registers FFFCh-FFFFh, the cartridge
   RAM, the Codemasters mapper) call WrZ80Slow. */
#ifndef Z80CORE_CONFIG_H
#define Z80CORE_CONFIG_H
#include <stdint.h>

extern uint8_t *mem_read_map[];
extern uint8_t *main_ram;
void WrZ80Slow(uint16_t addr, uint8_t value);
uint8_t InZ80(uint16_t port);
void OutZ80(uint16_t port, uint8_t value);

#define Z80CORE_RD(a) (mem_read_map[(a) >> 10][(a) & 0x3FF])
#define Z80CORE_WR(a, v) do { if ((uint16_t)((a) - 0xC000) < 0x3FFC) main_ram[(a) & 0x1FFF] = (v); \
  else { Z80CORE_SYNC_OUT(); WrZ80Slow((a), (v)); Z80CORE_SYNC_IN(); } } while (0)
#define Z80CORE_IN(p) ({ Z80CORE_SYNC_OUT(); const uint8_t vi_ = InZ80(p); Z80CORE_SYNC_IN(); vi_; })
#define Z80CORE_OUT(p, v) do { Z80CORE_SYNC_OUT(); OutZ80((p), (v)); Z80CORE_SYNC_IN(); } while (0)

#endif
