// SPDX-License-Identifier: GPL-3.0-only

#include "installer.h"
#include "../../version.h"

extern uint8_t kernelelf[];
extern int32_t kernelelf_size;

extern uint8_t debuggerbin[];
extern int32_t debuggerbin_size;

void ascii_art() {
    printf("\n\n");
    printf("         _ _     _     _                            \n");
    printf(" _ __ __| | | __| |___| |__ _  _ __ _ ___ _ _  __ _ \n");
    printf("| '_ (_-<_  _/ _` / -_) '_ \\ || / _` |___| ' \\/ _` |\n");
    printf("| .__/__/ |_|\\__,_\\___|_.__/\\_,_\\__, |   |_||_\\__, |\n");
    printf("|_|                             |___/         |___/ \n");
    printf("                                                    \n");
}

static void patch_505_v119(uint64_t kernbase) {

    *(uint8_t *)(kernbase + 0x1EA53D) = 0xEB;

    memcpy((void *)(kernbase + 0x11730), "\x48\xC7\xC0\x01\x00\x00\x00\xC3", 8);

    memcpy((void *)(kernbase + 0x117B0), "\x48\xC7\xC0\x01\x00\x00\x00\xC3", 8);

    memcpy((void *)(kernbase + 0x117C0), "\x48\xC7\xC0\x01\x00\x00\x00\xC3", 8);

    *(uint8_t *)(kernbase + 0x7673E0) = 0xC3;

    memcpy((void *)(kernbase + 0x13F03F), "\x31\xC0\x90\x90\x90", 5);

    memcpy((void *)(kernbase + 0x1A3C08), "\x90\x90\x90\x90\x90\x90", 6);

    *(uint8_t *)(kernbase + 0x30D9AA) = 0xEB;

    memcpy((void *)(kernbase + 0x30DE08), "\xE9\xD0\x00\x00\x00", 5);

    *(uint16_t *)(kernbase + 0x194875) = 0x9090;

    *(uint8_t *)(kernbase + 0xFCD48) = 0x07;

    *(uint8_t *)(kernbase + 0xFCD56) = 0x07;

    memcpy((void *)(kernbase + 0x1EA767), "\x90\x90", 2);

    memcpy((void *)(kernbase + 0x1EA682), "\x90\x90", 2);
}

static void patch_671_v119(uint64_t kernbase) {

    *(uint8_t *)(kernbase + 0x3C15BD) = 0xEB;

    memcpy((void *)(kernbase + 0x233BD0), "\x48\xC7\xC0\x01\x00\x00\x00\xC3", 8);

    memcpy((void *)(kernbase + 0x233C40), "\x48\xC7\xC0\x01\x00\x00\x00\xC3", 8);

    memcpy((void *)(kernbase + 0x233C50), "\x48\xC7\xC0\x01\x00\x00\x00\xC3", 8);

    *(uint8_t *)(kernbase + 0x784120) = 0xC3;

    memcpy((void *)(kernbase + 0xAD2E4), "\x31\xC0\x90\x90\x90", 5);

    *(uint8_t *)(kernbase + 0x10F879) = 0xEB;

    memcpy((void *)(kernbase + 0x10FD22), "\xE9\xE2\x02\x00\x00", 5);

    *(uint8_t *)(kernbase + 0x3CECE1) = 0xEB;

    *(uint8_t *)(kernbase + 0x2507F5) = 0x07;

    *(uint8_t *)(kernbase + 0x250803) = 0x07;

    memcpy((void *)(kernbase + 0x3C17F7), "\x90\x90", 2);

    memcpy((void *)(kernbase + 0x3C1702), "\x90\x90", 2);

    memcpy((void *)(kernbase + 0x459763), "\x90\x90\x90\x90\x90\x90", 6);
}

static void patch_700_v119(uint64_t kernbase) {

    *(uint8_t *)(kernbase + 0x2F04D) = 0xEB;

    memcpy((void *)(kernbase + 0x1CB880), "\x48\xC7\xC0\x01\x00\x00\x00\xC3", 8);

    memcpy((void *)(kernbase + 0x1CB8F0), "\x48\xC7\xC0\x01\x00\x00\x00\xC3", 8);

    memcpy((void *)(kernbase + 0x1CB910), "\x48\xC7\xC0\x01\x00\x00\x00\xC3", 8);

    memcpy((void *)(kernbase + 0x1D40BB), "\x31\xC0\x90\x90\x90", 5);

    *(uint8_t *)(kernbase + 0x448D5) = 0xEB;

    memcpy((void *)(kernbase + 0x44DAF), "\xE9\x7C\x02\x00\x00", 5);

    *(uint8_t *)(kernbase + 0xC1F9A) = 0xEB;

    *(uint8_t *)(kernbase + 0x1171BE) = 0x07;

    *(uint8_t *)(kernbase + 0x1171C6) = 0x07;

    memcpy((void *)(kernbase + 0x2F287), "\x90\x90", 2);

    memcpy((void *)(kernbase + 0x2F192), "\x90\x90", 2);

    memcpy((void *)(kernbase + 0x26C5F3), "\x90\x90\x90\x90\x90\x90", 6);
}

static void patch_750_v119(uint64_t kernbase) {

    *(uint8_t *)(kernbase + 0x28F80D) = 0xEB;

    memcpy((void *)(kernbase + 0x364CD0), "\x48\xC7\xC0\x01\x00\x00\x00\xC3", 8);

    memcpy((void *)(kernbase + 0x364D40), "\x48\xC7\xC0\x01\x00\x00\x00\xC3", 8);

    memcpy((void *)(kernbase + 0x364D60), "\x48\xC7\xC0\x01\x00\x00\x00\xC3", 8);

    *(uint8_t *)(kernbase + 0x77F960) = 0xC3;

    memcpy((void *)(kernbase + 0xDCED1), "\x31\xC0\x90\x90\x90", 5);

    memcpy((void *)(kernbase + 0x3014C8), "\x90\x90\x90\x90\x90\x90", 6);

    *(uint8_t *)(kernbase + 0x361CF5) = 0xEB;

    memcpy((void *)(kernbase + 0x3621CF), "\xE9\x7C\x02\x00\x00", 5);

    *(uint16_t *)(kernbase + 0x218AF4) = 0x00EB;

    *(uint8_t *)(kernbase + 0x1754AC) = 0x07;

    *(uint8_t *)(kernbase + 0x1754B4) = 0x07;

    memcpy((void *)(kernbase + 0x28FA47), "\x90\x90", 2);

    memcpy((void *)(kernbase + 0x28F952), "\x90\x90", 2);
}

static void patch_751_v119(uint64_t kernbase) {

    *(uint8_t *)(kernbase + 0x28F80D) = 0xEB;

    memcpy((void *)(kernbase + 0x364CD0), "\x48\xC7\xC0\x01\x00\x00\x00\xC3", 8);

    memcpy((void *)(kernbase + 0x364D40), "\x48\xC7\xC0\x01\x00\x00\x00\xC3", 8);

    memcpy((void *)(kernbase + 0x364D60), "\x48\xC7\xC0\x01\x00\x00\x00\xC3", 8);

    *(uint8_t *)(kernbase + 0x77F9A0) = 0xC3;

    memcpy((void *)(kernbase + 0xDCED1), "\x31\xC0\x90\x90\x90", 5);

    memcpy((void *)(kernbase + 0x3014C8), "\x90\x90\x90\x90\x90\x90", 6);

    *(uint8_t *)(kernbase + 0x361CF5) = 0xEB;

    memcpy((void *)(kernbase + 0x3621CF), "\xE9\x7C\x02\x00\x00", 5);

    *(uint8_t *)(kernbase + 0x218AF4) = 0xEB;

    *(uint8_t *)(kernbase + 0x1754AC) = 0x07;

    *(uint8_t *)(kernbase + 0x1754B4) = 0x07;

    memcpy((void *)(kernbase + 0x28FA47), "\x90\x90", 2);

    memcpy((void *)(kernbase + 0x28F952), "\x90\x90", 2);
}

static void patch_800_v119(uint64_t kernbase) {

    *(uint8_t *)(kernbase + 0x25E1CD) = 0xEB;

    memcpy((void *)(kernbase + 0x1D5710), "\x48\xC7\xC0\x01\x00\x00\x00\xC3", 8);

    memcpy((void *)(kernbase + 0x1D5780), "\x48\xC7\xC0\x01\x00\x00\x00\xC3", 8);

    memcpy((void *)(kernbase + 0x1D57A0), "\x48\xC7\xC0\x01\x00\x00\x00\xC3", 8);

    *(uint8_t *)(kernbase + 0x766DF0) = 0xC3;

    memcpy((void *)(kernbase + 0xFED61), "\x31\xC0\x90\x90\x90", 5);

    memcpy((void *)(kernbase + 0x3EC68B), "\x90\x90\x90\x90\x90\x90", 6);

    *(uint8_t *)(kernbase + 0x174155) = 0xEB;

    memcpy((void *)(kernbase + 0x17462F), "\xE9\x7C\x02\x00\x00", 5);

    *(uint16_t *)(kernbase + 0x2856F4) = 0x9090;

    *(uint8_t *)(kernbase + 0x1B4BC) = 0x07;

    *(uint8_t *)(kernbase + 0x1B4C4) = 0x07;

    memcpy((void *)(kernbase + 0x25E407), "\x90\x90", 2);

    memcpy((void *)(kernbase + 0x25E312), "\x90\x90", 2);
}

static void patch_850_v119(uint64_t kernbase) {

    *(uint8_t *)(kernbase + 0x3A40FD) = 0xEB;

    memcpy((void *)(kernbase + 0x2935E0), "\x48\xC7\xC0\x01\x00\x00\x00\xC3", 8);

    memcpy((void *)(kernbase + 0x293650), "\x48\xC7\xC0\x01\x00\x00\x00\xC3", 8);

    memcpy((void *)(kernbase + 0x293670), "\x48\xC7\xC0\x01\x00\x00\x00\xC3", 8);

    *(uint8_t *)(kernbase + 0x76CEB0) = 0xC3;

    memcpy((void *)(kernbase + 0x84411), "\x31\xC0\x90\x90\x90", 5);

    memcpy((void *)(kernbase + 0x14D6DB), "\x90\x90\x90\x90\x90\x90", 6);

    *(uint8_t *)(kernbase + 0x132535) = 0xEB;

    memcpy((void *)(kernbase + 0x132A0F), "\xE9\x7C\x02\x00\x00", 5);

    *(uint16_t *)(kernbase + 0x215154) = 0x9090;

    *(uint8_t *)(kernbase + 0x219A6C) = 0x07;

    *(uint8_t *)(kernbase + 0x219A74) = 0x07;

    memcpy((void *)(kernbase + 0x3A4337), "\x90\x90", 2);

    memcpy((void *)(kernbase + 0x3A4242), "\x90\x90", 2);
}

static void patch_900_v119(uint64_t kernbase) {

    *(uint8_t *)(kernbase + 0x2714BD) = 0xEB;

    memcpy((void *)(kernbase + 0x8BC20), "\x48\xC7\xC0\x01\x00\x00\x00\xC3", 8);

    memcpy((void *)(kernbase + 0x8BC90), "\x48\xC7\xC0\x01\x00\x00\x00\xC3", 8);

    memcpy((void *)(kernbase + 0x8BCB0), "\x48\xC7\xC0\x01\x00\x00\x00\xC3", 8);

    *(uint8_t *)(kernbase + 0x767E30) = 0xC3;

    memcpy((void *)(kernbase + 0x168051), "\x31\xC0\x90\x90\x90", 5);

    memcpy((void *)(kernbase + 0x80B8B), "\x90\x90\x90\x90\x90\x90", 6);

    *(uint8_t *)(kernbase + 0x41F4E5) = 0xEB;

    memcpy((void *)(kernbase + 0x41F9D1), "\xE9\x7C\x02\x00\x00", 5);

    *(uint16_t *)(kernbase + 0x5F824) = 0x9090;

    *(uint8_t *)(kernbase + 0x37BF3C) = 0x07;

    *(uint8_t *)(kernbase + 0x37BF44) = 0x07;

    memcpy((void *)(kernbase + 0x2716F7), "\x90\x90", 2);

    memcpy((void *)(kernbase + 0x271602), "\x90\x90", 2);

    memcpy((void *)(kernbase + 0x884BE), "\x90\x90\x90\x90\x90\x90", 6);
}

static void patch_903_v119(uint64_t kernbase) {

    *(uint8_t *)(kernbase + 0x27113D) = 0xEB;

    memcpy((void *)(kernbase + 0x8BC20), "\x48\xC7\xC0\x01\x00\x00\x00\xC3", 8);

    memcpy((void *)(kernbase + 0x8BC90), "\x48\xC7\xC0\x01\x00\x00\x00\xC3", 8);

    memcpy((void *)(kernbase + 0x8BCB0), "\x48\xC7\xC0\x01\x00\x00\x00\xC3", 8);

    *(uint8_t *)(kernbase + 0x765DF0) = 0xC3;

    memcpy((void *)(kernbase + 0x168001), "\x31\xC0\x90\x90\x90", 5);

    memcpy((void *)(kernbase + 0x80B8B), "\x90\x90\x90\x90\x90\x90", 6);

    *(uint8_t *)(kernbase + 0x41D455) = 0xEB;

    memcpy((void *)(kernbase + 0x41D941), "\xE9\x7C\x02\x00\x00", 5);

    *(uint16_t *)(kernbase + 0x5F824) = 0x9090;

    *(uint8_t *)(kernbase + 0x37A13C) = 0x07;

    *(uint8_t *)(kernbase + 0x37A144) = 0x07;

    memcpy((void *)(kernbase + 0x271377), "\x90\x90", 2);

    memcpy((void *)(kernbase + 0x271282), "\x90\x90", 2);

    memcpy((void *)(kernbase + 0x884BE), "\x90\x90\x90\x90\x90\x90", 6);
}

static void patch_950_v119(uint64_t kernbase) {

    *(uint8_t *)(kernbase + 0x201CCD) = 0xEB;

    memcpy((void *)(kernbase + 0x32590), "\x48\xC7\xC0\x01\x00\x00\x00\xC3", 8);

    memcpy((void *)(kernbase + 0x32600), "\x48\xC7\xC0\x01\x00\x00\x00\xC3", 8);

    memcpy((void *)(kernbase + 0x32620), "\x48\xC7\xC0\x01\x00\x00\x00\xC3", 8);

    *(uint8_t *)(kernbase + 0x7603C0) = 0xC3;

    memcpy((void *)(kernbase + 0x124AA1), "\x31\xC0\x90\x90\x90", 5);

    memcpy((void *)(kernbase + 0x196D3B), "\x90\x90\x90\x90\x90\x90", 6);

    *(uint8_t *)(kernbase + 0x47A005) = 0xEB;

    memcpy((void *)(kernbase + 0x47A4F1), "\xE9\x7C\x02\x00\x00", 5);

    *(uint16_t *)(kernbase + 0x29AE74) = 0x9090;

    *(uint8_t *)(kernbase + 0x188A9C) = 0x07;

    *(uint8_t *)(kernbase + 0x188AA4) = 0x07;

    memcpy((void *)(kernbase + 0x201F07), "\x90\x90", 2);

    memcpy((void *)(kernbase + 0x201E12), "\x90\x90", 2);

    memcpy((void *)(kernbase + 0x19E66E), "\x90\x90\x90\x90\x90\x90", 6);
}

static void patch_1000_v119(uint64_t kernbase) {

    *(uint8_t *)(kernbase + 0x472D2D) = 0xEB;

    memcpy((void *)(kernbase + 0xA5C60), "\x48\xC7\xC0\x01\x00\x00\x00\xC3", 8);

    memcpy((void *)(kernbase + 0xA5CD0), "\x48\xC7\xC0\x01\x00\x00\x00\xC3", 8);

    memcpy((void *)(kernbase + 0xA5CF0), "\x48\xC7\xC0\x01\x00\x00\x00\xC3", 8);

    *(uint8_t *)(kernbase + 0x765620) = 0xC3;

    memcpy((void *)(kernbase + 0xEF2C1), "\x31\xC0\x90\x90\x90", 5);

    memcpy((void *)(kernbase + 0x39207B), "\x90\x90\x90\x90\x90\x90", 6);

    *(uint8_t *)(kernbase + 0x44E625) = 0xEB;

    memcpy((void *)(kernbase + 0x44EB11), "\xE9\x7C\x02\x00\x00", 5);

    *(uint16_t *)(kernbase + 0x3BF3A4) = 0x9090;

    *(uint8_t *)(kernbase + 0x33B10C) = 0x07;

    *(uint8_t *)(kernbase + 0x33B114) = 0x07;

    memcpy((void *)(kernbase + 0x472F67), "\x90\x90", 2);

    memcpy((void *)(kernbase + 0x472E72), "\x90\x90", 2);

    memcpy((void *)(kernbase + 0x3999AE), "\x90\x90\x90\x90\x90\x90", 6);
}

static void patch_1071_v119(uint64_t kernbase) {

    *(uint8_t *)(kernbase + 0xD737D) = 0xEB;

    memcpy((void *)(kernbase + 0x1F4470), "\x48\xC7\xC0\x01\x00\x00\x00\xC3", 8);

    memcpy((void *)(kernbase + 0x1F44E0), "\x48\xC7\xC0\x01\x00\x00\x00\xC3", 8);

    memcpy((void *)(kernbase + 0x1F4500), "\x48\xC7\xC0\x01\x00\x00\x00\xC3", 8);

    *(uint8_t *)(kernbase + 0x7673D0) = 0xC3;

    memcpy((void *)(kernbase + 0x19E151), "\x31\xC0\x90\x90\x90", 5);

    memcpy((void *)(kernbase + 0x47B2EC), "\x90\x90\x90\x90\x90\x90", 6);

    *(uint8_t *)(kernbase + 0x424E85) = 0xEB;

    memcpy((void *)(kernbase + 0x425371), "\xE9\x7C\x02\x00\x00", 5);

    *(uint16_t *)(kernbase + 0x345E04) = 0x9090;

    *(uint8_t *)(kernbase + 0x428A2C) = 0x07;

    *(uint8_t *)(kernbase + 0x428A34) = 0x07;

    memcpy((void *)(kernbase + 0xD75B7), "\x90\x90", 2);

    memcpy((void *)(kernbase + 0xD74C2), "\x90\x90", 2);

    memcpy((void *)(kernbase + 0x482D4E), "\x90\x90\x90\x90\x90\x90", 6);
}

static void patch_1100_v119(uint64_t kernbase) {

    *(uint8_t *)(kernbase + 0x2DDDFD) = 0xEB;

    memcpy((void *)(kernbase + 0x3D0DE0), "\x48\xC7\xC0\x01\x00\x00\x00\xC3", 8);

    memcpy((void *)(kernbase + 0x3D0E50), "\x48\xC7\xC0\x01\x00\x00\x00\xC3", 8);

    memcpy((void *)(kernbase + 0x3D0E70), "\x48\xC7\xC0\x01\x00\x00\x00\xC3", 8);

    *(uint8_t *)(kernbase + 0x76D210) = 0xC3;

    memcpy((void *)(kernbase + 0x157F91), "\x31\xC0\x90\x90\x90", 5);

    memcpy((void *)(kernbase + 0x35C8EC), "\x90\x90\x90\x90\x90\x90", 6);

    *(uint8_t *)(kernbase + 0x384285) = 0xEB;

    memcpy((void *)(kernbase + 0x384771), "\xE9\x7C\x02\x00\x00", 5);

    *(uint16_t *)(kernbase + 0x3B11A4) = 0x9090;

    *(uint8_t *)(kernbase + 0x245EDC) = 0x07;

    *(uint8_t *)(kernbase + 0x245EE4) = 0x07;

    memcpy((void *)(kernbase + 0x2DE037), "\x90\x90", 2);

    memcpy((void *)(kernbase + 0x2DDF42), "\x90\x90", 2);

    memcpy((void *)(kernbase + 0x36434E), "\x90\x90\x90\x90\x90\x90", 6);
}

static void patch_1102_v119(uint64_t kernbase) {

    *(uint8_t *)(kernbase + 0x2DDE1D) = 0xEB;

    memcpy((void *)(kernbase + 0x3D0E00), "\x48\xC7\xC0\x01\x00\x00\x00\xC3", 8);

    memcpy((void *)(kernbase + 0x3D0E70), "\x48\xC7\xC0\x01\x00\x00\x00\xC3", 8);

    memcpy((void *)(kernbase + 0x3D0E90), "\x48\xC7\xC0\x01\x00\x00\x00\xC3", 8);

    *(uint8_t *)(kernbase + 0x76D1D0) = 0xC3;

    memcpy((void *)(kernbase + 0x157FB1), "\x31\xC0\x90\x90\x90", 5);

    memcpy((void *)(kernbase + 0x35C90C), "\x90\x90\x90\x90\x90\x90", 6);

    *(uint8_t *)(kernbase + 0x3842A5) = 0xEB;

    memcpy((void *)(kernbase + 0x384791), "\xE9\x7C\x02\x00\x00", 5);

    *(uint16_t *)(kernbase + 0x3B11C4) = 0x9090;

    *(uint8_t *)(kernbase + 0x245EFC) = 0x07;

    *(uint8_t *)(kernbase + 0x245F04) = 0x07;

    memcpy((void *)(kernbase + 0x2DE057), "\x90\x90", 2);

    memcpy((void *)(kernbase + 0x2DDF62), "\x90\x90", 2);

    memcpy((void *)(kernbase + 0x36436E), "\x90\x90\x90\x90\x90\x90", 6);
}

static void patch_1150_v119(uint64_t kernbase) {

    *(uint8_t *)(kernbase + 0x2BD3AD) = 0xEB;

    memcpy((void *)(kernbase + 0x3B2A90), "\x48\xC7\xC0\x01\x00\x00\x00\xC3", 8);

    memcpy((void *)(kernbase + 0x3B2B00), "\x48\xC7\xC0\x01\x00\x00\x00\xC3", 8);

    memcpy((void *)(kernbase + 0x3B2B20), "\x48\xC7\xC0\x01\x00\x00\x00\xC3", 8);

    *(uint8_t *)(kernbase + 0x76B370) = 0xC3;

    memcpy((void *)(kernbase + 0x1FC361), "\x31\xC0\x90\x90\x90", 5);

    memcpy((void *)(kernbase + 0x2FBEAC), "\x90\x90\x90\x90\x90\x90", 6);

    *(uint8_t *)(kernbase + 0x366745) = 0xEB;

    memcpy((void *)(kernbase + 0x366C31), "\xE9\x7C\x02\x00\x00", 5);

    *(uint16_t *)(kernbase + 0x477A14) = 0x9090;

    *(uint8_t *)(kernbase + 0x46586C) = 0x07;

    *(uint8_t *)(kernbase + 0x465874) = 0x07;

    memcpy((void *)(kernbase + 0x2BD5E7), "\x90\x90", 2);

    memcpy((void *)(kernbase + 0x2BD4F2), "\x90\x90", 2);

    memcpy((void *)(kernbase + 0x30390E), "\x90\x90\x90\x90\x90\x90", 6);
}

static void patch_1200_v119(uint64_t kernbase) {

    *(uint8_t *)(kernbase + 0x2BD48D) = 0xEB;

    memcpy((void *)(kernbase + 0x3B2CD0), "\x48\xC7\xC0\x01\x00\x00\x00\xC3", 8);

    memcpy((void *)(kernbase + 0x3B2D40), "\x48\xC7\xC0\x01\x00\x00\x00\xC3", 8);

    memcpy((void *)(kernbase + 0x3B2D60), "\x48\xC7\xC0\x01\x00\x00\x00\xC3", 8);

    *(uint8_t *)(kernbase + 0x76B7F0) = 0xC3;

    memcpy((void *)(kernbase + 0x1FC441), "\x31\xC0\x90\x90\x90", 5);

    memcpy((void *)(kernbase + 0x2FC0EC), "\x90\x90\x90\x90\x90\x90", 6);

    *(uint8_t *)(kernbase + 0x366985) = 0xEB;

    memcpy((void *)(kernbase + 0x366E71), "\xE9\x7C\x02\x00\x00", 5);

    *(uint16_t *)(kernbase + 0x477C54) = 0x9090;

    *(uint8_t *)(kernbase + 0x465AAC) = 0x07;

    *(uint8_t *)(kernbase + 0x465AB4) = 0x07;

    memcpy((void *)(kernbase + 0x2BD6C7), "\x90\x90", 2);

    memcpy((void *)(kernbase + 0x2BD5D2), "\x90\x90", 2);

    memcpy((void *)(kernbase + 0x303B4E), "\x90\x90\x90\x90\x90\x90", 6);
}

static void patch_1250_v119(uint64_t kernbase) {

    *(uint8_t *)(kernbase + 0x2BD4CD) = 0xEB;

    memcpy((void *)(kernbase + 0x3B2D10), "\x48\xC7\xC0\x01\x00\x00\x00\xC3", 8);

    memcpy((void *)(kernbase + 0x3B2D80), "\x48\xC7\xC0\x01\x00\x00\x00\xC3", 8);

    memcpy((void *)(kernbase + 0x3B2DA0), "\x48\xC7\xC0\x01\x00\x00\x00\xC3", 8);

    *(uint8_t *)(kernbase + 0x76B8B0) = 0xC3;

    memcpy((void *)(kernbase + 0x1FC481), "\x31\xC0\x90\x90\x90", 5);

    memcpy((void *)(kernbase + 0x2FC12C), "\x90\x90\x90\x90\x90\x90", 6);

    *(uint8_t *)(kernbase + 0x3669C5) = 0xEB;

    memcpy((void *)(kernbase + 0x366EB1), "\xE9\x7C\x02\x00\x00", 5);

    *(uint16_t *)(kernbase + 0x477C94) = 0x9090;

    *(uint8_t *)(kernbase + 0x465AEC) = 0x07;

    *(uint8_t *)(kernbase + 0x465AF4) = 0x07;

    memcpy((void *)(kernbase + 0x2BD707), "\x90\x90", 2);

    memcpy((void *)(kernbase + 0x2BD612), "\x90\x90", 2);

    memcpy((void *)(kernbase + 0x303B8E), "\x90\x90\x90\x90\x90\x90", 6);
}

static void patch_1300_v119(uint64_t kernbase) {

    *(uint8_t *)(kernbase + 0x2BD4ED) = 0xEB;

    memcpy((void *)(kernbase + 0x3B2D30), "\x48\xC7\xC0\x01\x00\x00\x00\xC3", 8);

    memcpy((void *)(kernbase + 0x3B2DA0), "\x48\xC7\xC0\x01\x00\x00\x00\xC3", 8);

    memcpy((void *)(kernbase + 0x3B2DC0), "\x48\xC7\xC0\x01\x00\x00\x00\xC3", 8);

    *(uint8_t *)(kernbase + 0x76BA30) = 0xC3;

    memcpy((void *)(kernbase + 0x1FC4A1), "\x31\xC0\x90\x90\x90", 5);

    memcpy((void *)(kernbase + 0x2FC14C), "\x90\x90\x90\x90\x90\x90", 6);

    *(uint8_t *)(kernbase + 0x3669E5) = 0xEB;

    memcpy((void *)(kernbase + 0x366ED1), "\xE9\x7C\x02\x00\x00", 5);

    *(uint16_t *)(kernbase + 0x477CB4) = 0x9090;

    *(uint8_t *)(kernbase + 0x465B0C) = 0x07;

    *(uint8_t *)(kernbase + 0x465B14) = 0x07;

    memcpy((void *)(kernbase + 0x2BD727), "\x90\x90", 2);

    memcpy((void *)(kernbase + 0x2BD632), "\x90\x90", 2);

    memcpy((void *)(kernbase + 0x303BAE), "\x90\x90\x90\x90\x90\x90", 6);
}

void patch_kernel() {
    uint64_t kernbase = get_kbase();

    cpu_disable_wp();

    switch (cachedFirmware) {
        case 505: case 507:
            patch_505_v119(kernbase);
            break;
        case 671: case 672:
            patch_671_v119(kernbase);
            break;
        case 700: case 702:
            patch_700_v119(kernbase);
            break;
        case 750:
            patch_750_v119(kernbase);
            break;
        case 751: case 755:
            patch_751_v119(kernbase);
            break;
        case 800: case 803:
            patch_800_v119(kernbase);
            break;
        case 850: case 852:
            patch_850_v119(kernbase);
            break;
        case 900:
            patch_900_v119(kernbase);
            break;
        case 903: case 904:
            patch_903_v119(kernbase);
            break;
        case 950: case 951: case 960:
            patch_950_v119(kernbase);
            break;
        case 1000: case 1001:
            patch_1000_v119(kernbase);
            break;
        // 10.50 / 10.70 / 10.71 share an identical kernel layout (verified: every patch site matches).
        case 1050: case 1070: case 1071:
            patch_1071_v119(kernbase);
            break;
        case 1100:
            patch_1100_v119(kernbase);
            break;
        case 1102:
            patch_1102_v119(kernbase);
            break;
        case 1150: case 1152:
            patch_1150_v119(kernbase);
            break;
        case 1200: case 1202:
            patch_1200_v119(kernbase);
            break;
        case 1250: case 1252:
            patch_1250_v119(kernbase);
            break;
        case 1300:
            patch_1300_v119(kernbase);
            break;
        default:
            printf("[ps4debug-ng] unsupported firmware %u - kernel not patched\n",
                   cachedFirmware);
            break;
    }

    cpu_enable_wp();
}

void *rwx_alloc(uint64_t size) {
    uint64_t alignedSize = (size + 0x3FFFull) & ~0x3FFFull;
    return (void *)kmem_alloc(*kernel_map, alignedSize);
}

int load_kdebugger() {
    uint64_t mapsize;
    void *kmemory;
    int (*payload_entry)(void *p);

    if (elf_mapped_size(kernelelf, &mapsize)) {
        printf("[ps4debug-ng] invalid kdebugger elf!\n");
        return 1;
    }

    kmemory = rwx_alloc(mapsize);
    if(!kmemory) {
        printf("[ps4debug-ng] could not allocate memory for kdebugger!\n");
        return 1;
    }

    if (load_elf(kernelelf, kernelelf_size, kmemory, mapsize, (void **)&payload_entry)) {
        printf("[ps4debug-ng] could not load kdebugger elf!\n");
        return 1;
    }

    if (payload_entry(NULL)) {
        return 1;
    }

    return 0;
}

int load_debugger() {
    struct proc *p;
    struct vmspace *vm;
    struct vm_map *map;
    struct vm_map_entry *entry = NULL;
    uint64_t addr = 0;
    int r;

    p = proc_find_by_name("SceShellCore");
    if(!p) {
        printf("[ps4debug-ng] could not find SceShellCore process!\n");
        return 1;
    }

    vm = p->p_vmspace;
    map = &vm->vm_map;

    uint64_t hint = 0;
    vm_map_lock(map);
    r = vm_map_lookup_entry(map, NULL, &entry);
    if (r) {
        vm_map_unlock(map);
        printf("[ps4debug-ng] failed to look up vm_map header!\n");
        return r;
    }

    struct vm_map_entry *e = entry->next;
    e = e->next;
    hint = (uint64_t)e;

    if (map->nentries > 7) {
        e = e->next;
        e = e->next;
        e = e->next;
        e = e->next;
        hint = e->start;
    }

    uint64_t payload_region = ((uint64_t)(uint32_t)debuggerbin_size + 0xFFFFFull) & ~0xFFFFFull;
    if (payload_region < 0x200000) payload_region = 0x200000;
    r = vm_map_findspace(map, hint, payload_region, &addr);
    if (r) {
        vm_map_unlock(map);
        printf("[ps4debug-ng] failed to find free payload region!\n");
        return r;
    }
    r = vm_map_insert(map, NULL, NULL, addr, addr + payload_region, VM_PROT_ALL, VM_PROT_ALL, 0);
    vm_map_unlock(map);
    if(r) {
        printf("[ps4debug-ng] failed to allocate payload memory!\n");
        return r;
    }

    vm_map_lock(map);
    r = vm_map_lookup_entry(map, NULL, &entry);
    if (r) {
        vm_map_unlock(map);
        printf("[ps4debug-ng] failed to look up first vm_map entry!\n");
        return r;
    }
    int nentries = map->nentries;
    for (int i = 0; i < nentries && entry; i++) {

        if (entry->start >= addr && addr <= entry->end) {
            entry->name[0] = 0;
            break;
        }
        entry = entry->next;
    }
    vm_map_unlock(map);

    r = proc_write_mem(p, (void *)addr, debuggerbin_size, debuggerbin, NULL);
    if(r) {
        printf("[ps4debug-ng] failed to write payload!\n");
        return r;
    }

    r = proc_create_thread(p, addr);
    if(r) {
        printf("[ps4debug-ng] failed to create payload thread!\n");
        return r;
    }

    return 0;
}

int runinstaller() {

    init_ksdk();

    extern unsigned short cachedFirmware;
    extern uint64_t cachedKernelBase;
    if (cachedFirmware == 0) {
        cachedFirmware = 900;
        cachedKernelBase = 0;
        init_ksdk();
    }

    ascii_art();

    printf("[ps4debug-ng] patching kernel...\n");
    patch_kernel();

    printf("[ps4debug-ng] loading kdebugger...\n");
    if(load_kdebugger()) {
        return 1;
    }

    printf("[ps4debug-ng] loading debugger...\n");
    if(load_debugger()) {
        return 1;
    }

    printf("[ps4debug-ng] PS4Debug-NG by OSR v" PS4DEBUG_NG_VERSION_STR "\n");
    return 0;
}
