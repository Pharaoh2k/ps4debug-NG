// SPDX-License-Identifier: GPL-3.0-only

#include "installer.h"
#include "../../version.h"
#include "../../syscalls.h"

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

    memcpy((void *)(kernbase + 0x3AB306), "\x90\x90\x90\x90\x90", 5);

    memcpy((void *)(kernbase + 0x45E4F5), "\x90\x90\x90\x90\x90", 5);
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

    memcpy((void *)(kernbase + 0x3EFBD8), "\x90\x90\x90\x90\x90", 5);

    memcpy((void *)(kernbase + 0x37675), "\x90\x90\x90\x90\x90", 5);
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

    memcpy((void *)(kernbase + 0x1AC435), "\x90\x90\x90\x90\x90", 5);

    memcpy((void *)(kernbase + 0xAD624), "\x90\x90\x90\x90\x90", 5);
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

    memcpy((void *)(kernbase + 0x38AFE5), "\x90\x90\x90\x90\x90", 5);

    memcpy((void *)(kernbase + 0x27A1BA), "\x90\x90\x90\x90\x90", 5);
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

    memcpy((void *)(kernbase + 0x38AFE5), "\x90\x90\x90\x90\x90", 5);

    memcpy((void *)(kernbase + 0x27A1BA), "\x90\x90\x90\x90\x90", 5);
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

    memcpy((void *)(kernbase + 0x3FDE15), "\x90\x90\x90\x90\x90", 5);

    memcpy((void *)(kernbase + 0x2F631A), "\x90\x90\x90\x90\x90", 5);
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

    memcpy((void *)(kernbase + 0x2AC925), "\x90\x90\x90\x90\x90", 5);

    memcpy((void *)(kernbase + 0x241D2A), "\x90\x90\x90\x90\x90", 5);
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

    memcpy((void *)(kernbase + 0xE12A5), "\x90\x90\x90\x90\x90", 5);

    memcpy((void *)(kernbase + 0x30BB9A), "\x90\x90\x90\x90\x90", 5);
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

    memcpy((void *)(kernbase + 0xE1255), "\x90\x90\x90\x90\x90", 5);

    memcpy((void *)(kernbase + 0x30B83A), "\x90\x90\x90\x90\x90", 5);
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

    memcpy((void *)(kernbase + 0x2C61E5), "\x90\x90\x90\x90\x90", 5);

    memcpy((void *)(kernbase + 0x10EDCA), "\x90\x90\x90\x90\x90", 5);
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

    memcpy((void *)(kernbase + 0xA90A5), "\x90\x90\x90\x90\x90", 5);

    memcpy((void *)(kernbase + 0x3D42DA), "\x90\x90\x90\x90\x90", 5);
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

    memcpy((void *)(kernbase + 0x472255), "\x90\x90\x90\x90\x90", 5);

    memcpy((void *)(kernbase + 0x150BDA), "\x90\x90\x90\x90\x90", 5);
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

    memcpy((void *)(kernbase + 0x1DE9A5), "\x90\x90\x90\x90\x90", 5);

    memcpy((void *)(kernbase + 0x5147A), "\x90\x90\x90\x90\x90", 5);
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

    memcpy((void *)(kernbase + 0x1DE9C5), "\x90\x90\x90\x90\x90", 5);

    memcpy((void *)(kernbase + 0x5147A), "\x90\x90\x90\x90\x90", 5);
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

    memcpy((void *)(kernbase + 0x135705), "\x90\x90\x90\x90\x90", 5);

    memcpy((void *)(kernbase + 0x2A4ABA), "\x90\x90\x90\x90\x90", 5);
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

    memcpy((void *)(kernbase + 0x135705), "\x90\x90\x90\x90\x90", 5);

    memcpy((void *)(kernbase + 0x2A4B9A), "\x90\x90\x90\x90\x90", 5);
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

    memcpy((void *)(kernbase + 0x135745), "\x90\x90\x90\x90\x90", 5);

    memcpy((void *)(kernbase + 0x2A4BDA), "\x90\x90\x90\x90\x90", 5);
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

    memcpy((void *)(kernbase + 0x135745), "\x90\x90\x90\x90\x90", 5);

    memcpy((void *)(kernbase + 0x2A4BFA), "\x90\x90\x90\x90\x90", 5);
}

// 13.02 and 13.04 share an identical kernel layout (verified: every patch site matches).
static void patch_1304_v119(uint64_t kernbase) {

    *(uint8_t *)(kernbase + 0x2BD4FD) = 0xEB;

    memcpy((void *)(kernbase + 0x3B2D40), "\x48\xC7\xC0\x01\x00\x00\x00\xC3", 8);

    memcpy((void *)(kernbase + 0x3B2DB0), "\x48\xC7\xC0\x01\x00\x00\x00\xC3", 8);

    memcpy((void *)(kernbase + 0x3B2DD0), "\x48\xC7\xC0\x01\x00\x00\x00\xC3", 8);

    *(uint8_t *)(kernbase + 0x76BA30) = 0xC3;

    memcpy((void *)(kernbase + 0x1FC4B1), "\x31\xC0\x90\x90\x90", 5);

    memcpy((void *)(kernbase + 0x2FC15C), "\x90\x90\x90\x90\x90\x90", 6);

    *(uint8_t *)(kernbase + 0x3669F5) = 0xEB;

    memcpy((void *)(kernbase + 0x366EE1), "\xE9\x7C\x02\x00\x00", 5);

    *(uint16_t *)(kernbase + 0x477CC4) = 0x9090;

    *(uint8_t *)(kernbase + 0x465B1C) = 0x07;

    *(uint8_t *)(kernbase + 0x465B24) = 0x07;

    memcpy((void *)(kernbase + 0x2BD737), "\x90\x90", 2);

    memcpy((void *)(kernbase + 0x2BD642), "\x90\x90", 2);

    memcpy((void *)(kernbase + 0x303BBE), "\x90\x90\x90\x90\x90\x90", 6);

    memcpy((void *)(kernbase + 0x135745), "\x90\x90\x90\x90\x90", 5);

    memcpy((void *)(kernbase + 0x2A4C0A), "\x90\x90\x90\x90\x90", 5);
}

static void patch_1350_v119(uint64_t kernbase) {

    *(uint8_t *)(kernbase + 0x2BD50D) = 0xEB;

    memcpy((void *)(kernbase + 0x3B3180), "\x48\xC7\xC0\x01\x00\x00\x00\xC3", 8);

    memcpy((void *)(kernbase + 0x3B31F0), "\x48\xC7\xC0\x01\x00\x00\x00\xC3", 8);

    memcpy((void *)(kernbase + 0x3B3210), "\x48\xC7\xC0\x01\x00\x00\x00\xC3", 8);

    *(uint8_t *)(kernbase + 0x76BE70) = 0xC3;

    memcpy((void *)(kernbase + 0x1FC4C1), "\x31\xC0\x90\x90\x90", 5);

    memcpy((void *)(kernbase + 0x2FC4AC), "\x90\x90\x90\x90\x90\x90", 6);

    *(uint8_t *)(kernbase + 0x366D45) = 0xEB;

    memcpy((void *)(kernbase + 0x367231), "\xE9\x7C\x02\x00\x00", 5);

    *(uint16_t *)(kernbase + 0x478104) = 0x9090;

    *(uint8_t *)(kernbase + 0x465F5C) = 0x07;

    *(uint8_t *)(kernbase + 0x465F64) = 0x07;

    memcpy((void *)(kernbase + 0x2BD747), "\x90\x90", 2);

    memcpy((void *)(kernbase + 0x2BD652), "\x90\x90", 2);

    memcpy((void *)(kernbase + 0x303F0E), "\x90\x90\x90\x90\x90\x90", 6);

    memcpy((void *)(kernbase + 0x135745), "\x90\x90\x90\x90\x90", 5);

    memcpy((void *)(kernbase + 0x2A4C1A), "\x90\x90\x90\x90\x90", 5);
}

static void patch_1352_v119(uint64_t kernbase) {

    *(uint8_t *)(kernbase + 0x2BD5AD) = 0xEB;

    memcpy((void *)(kernbase + 0x3B3580), "\x48\xC7\xC0\x01\x00\x00\x00\xC3", 8);

    memcpy((void *)(kernbase + 0x3B35F0), "\x48\xC7\xC0\x01\x00\x00\x00\xC3", 8);

    memcpy((void *)(kernbase + 0x3B3610), "\x48\xC7\xC0\x01\x00\x00\x00\xC3", 8);

    *(uint8_t *)(kernbase + 0x76C270) = 0xC3;

    memcpy((void *)(kernbase + 0x1FC561), "\x31\xC0\x90\x90\x90", 5);

    memcpy((void *)(kernbase + 0x2FC8AC), "\x90\x90\x90\x90\x90\x90", 6);

    *(uint8_t *)(kernbase + 0x367145) = 0xEB;

    memcpy((void *)(kernbase + 0x367631), "\xE9\x7C\x02\x00\x00", 5);

    *(uint16_t *)(kernbase + 0x478504) = 0x9090;

    *(uint8_t *)(kernbase + 0x46635C) = 0x07;

    *(uint8_t *)(kernbase + 0x466364) = 0x07;

    memcpy((void *)(kernbase + 0x2BD7E7), "\x90\x90", 2);

    memcpy((void *)(kernbase + 0x2BD6F2), "\x90\x90", 2);

    memcpy((void *)(kernbase + 0x30430E), "\x90\x90\x90\x90\x90\x90", 6);

    memcpy((void *)(kernbase + 0x1357E5), "\x90\x90\x90\x90\x90", 5);

    memcpy((void *)(kernbase + 0x2A4CBA), "\x90\x90\x90\x90\x90", 5);
}

/* Match complete eight-byte windows before writing any patches. Accept retail
 * code, our replacements, or the verified loader forms below with retail tails.
 * Every accepted form is normalized to the replacement bytes. */
static int patch_1400_v119(uint64_t kernbase) {
    static const struct {
        uint32_t offset;
        uint8_t length;
        uint8_t original[8];
        uint8_t replacement[8];
        /* Loader prefixes: MOV AL,1; RET; NOP, or XOR EAX,EAX; JMP +1.
         * The short jump skips one leftover byte to the original continuation. */
        uint8_t loader_length;
        uint8_t loader[8];
    } patches[] = {
        { 0x2BD85D, 1, { 0x76, 0x17, 0x49, 0x89, 0xEA, 0x49, 0x83, 0xC2 }, { 0xEB } }, /* site_00 */
        { 0x3B3830, 8, { 0x55, 0x48, 0x89, 0xE5, 0xE8, 0x57, 0xC9, 0x06 }, { 0x48, 0xC7, 0xC0, 0x01, 0x00, 0x00, 0x00, 0xC3 }, 4, { 0xB0, 0x01, 0xC3, 0x90 } }, /* site_01 */
        { 0x3B38A0, 8, { 0x55, 0x48, 0x89, 0xE5, 0x48, 0x8B, 0x47, 0x68 }, { 0x48, 0xC7, 0xC0, 0x01, 0x00, 0x00, 0x00, 0xC3 }, 4, { 0xB0, 0x01, 0xC3, 0x90 } }, /* site_02 */
        { 0x3B38C0, 8, { 0x55, 0x48, 0x89, 0xE5, 0xF6, 0x47, 0x17, 0x04 }, { 0x48, 0xC7, 0xC0, 0x01, 0x00, 0x00, 0x00, 0xC3 }, 4, { 0xB0, 0x01, 0xC3, 0x90 } }, /* site_03 */
        { 0x76C530, 1, { 0x55, 0x48, 0x89, 0xE5, 0x41, 0x56, 0x53, 0x80 }, { 0xC3 } }, /* site_04 */
        { 0x1FC801, 5, { 0xE8, 0x3A, 0x63, 0x44, 0x00, 0x85, 0xC0, 0x0F }, { 0x31, 0xC0, 0x90, 0x90, 0x90 }, 5, { 0x31, 0xC0, 0xEB, 0x01, 0x00 } }, /* site_05 */
        { 0x2FCB5C, 6, { 0x0F, 0x85, 0xAD, 0x07, 0x00, 0x00, 0x49, 0x8B }, { 0x90, 0x90, 0x90, 0x90, 0x90, 0x90 } }, /* site_06 */
        { 0x3673F5, 1, { 0x77, 0x1C, 0x48, 0xB8, 0x36, 0x10, 0x00, 0x00 }, { 0xEB } }, /* site_07 */
        { 0x3678E1, 5, { 0x48, 0x8B, 0x41, 0x08, 0x44, 0x89, 0xC3, 0x48 }, { 0xE9, 0x7C, 0x02, 0x00, 0x00 } }, /* site_08 */
        { 0x4787B4, 2, { 0x74, 0x0B, 0x49, 0xC7, 0x44, 0x24, 0x38, 0x00 }, { 0x90, 0x90 } }, /* site_09 */
        { 0x46660C, 1, { 0x03, 0x00, 0x00, 0x00, 0x6A, 0x00, 0x50, 0x6A }, { 0x07 } }, /* site_10 */
        { 0x466614, 1, { 0x03, 0xE8, 0x06, 0x27, 0xE9, 0xFF, 0x48, 0x83 }, { 0x07 } }, /* site_11 */
        { 0x2BDA97, 2, { 0x77, 0x47, 0x48, 0xB8, 0xFF, 0xFF, 0xFF, 0xFF }, { 0x90, 0x90 } }, /* site_12 */
        { 0x2BD9A2, 2, { 0x77, 0x5C, 0x48, 0xB8, 0xFF, 0xFF, 0xFF, 0xFF }, { 0x90, 0x90 } }, /* site_13 */
        { 0x3045BE, 6, { 0x0F, 0x85, 0x98, 0x00, 0x00, 0x00, 0xE8, 0x57 }, { 0x90, 0x90, 0x90, 0x90, 0x90, 0x90 } }, /* site_14 */
        { 0x135A45, 5, { 0xE8, 0x76, 0xAD, 0x1A, 0x00, 0x48, 0x8B, 0x45 }, { 0x90, 0x90, 0x90, 0x90, 0x90 } }, /* site_15 */
        { 0x2A4F6A, 5, { 0xE8, 0x51, 0xB8, 0x03, 0x00, 0x41, 0x89, 0xDF }, { 0x90, 0x90, 0x90, 0x90, 0x90 } }, /* site_16 */
    };
    int mismatch = 0;
    for (uint64_t i = 0; i < sizeof(patches) / sizeof(patches[0]); ++i) {
        const volatile uint8_t *code = (void *)(kernbase + patches[i].offset);
        uint8_t actual[8];
        int original = 1, patched = 1;
        int loader_match = patches[i].loader_length != 0;
        for (uint64_t j = 0; j < sizeof(patches[i].original); ++j) {
            uint8_t value = actual[j] = code[j];
            uint8_t expected = j < patches[i].length ?
                patches[i].replacement[j] : patches[i].original[j];
            original &= value == patches[i].original[j];
            patched &= value == expected;
            loader_match &= value == (j < patches[i].loader_length ?
                patches[i].loader[j] : patches[i].original[j]);
        }
        if (!original && !patched && !loader_match) {
            printf("[ps4debug-ng] 14.00 patch mismatch at %x - kernel not patched "
                   "(found: %02x %02x %02x %02x %02x %02x %02x %02x)\n",
                   patches[i].offset, (unsigned)actual[0], (unsigned)actual[1],
                   (unsigned)actual[2], (unsigned)actual[3], (unsigned)actual[4],
                   (unsigned)actual[5], (unsigned)actual[6], (unsigned)actual[7]);
            mismatch = 1;
        }
    }
    if (mismatch) {
        return 1;
    }

    /* These slots are nosys in the retail ELF. Do not overwrite another payload. */
    for (uint32_t slot = PS4DEBUG_SYS_PROC_LIST; slot <= PS4DEBUG_SYS_CONSOLE_CMD; ++slot) {
        if (sysents[slot].sy_call != (void *)(kernbase + 0x1C0520)) {
            printf("[ps4debug-ng] 14.00 syscall %u is occupied - kernel not patched\n", slot);
            return 1;
        }
    }

    uint64_t cr0 = read_cr0();
    write_cr0(cr0 & ~X86_CR0_WP);
    for (uint64_t i = 0; i < sizeof(patches) / sizeof(patches[0]); ++i) {
        volatile uint8_t *code = (void *)(kernbase + patches[i].offset);
        for (uint64_t j = 0; j < patches[i].length; ++j) {
            code[j] = patches[i].replacement[j];
        }
    }
    write_cr0(cr0);
    return 0;
}

/* Same checks as 14.00, with every window read from the retail 14.02 ELF.
 * Replacement and loader bytes are unchanged; site_08's jump destination and
 * site_05's leftover loader byte were rechecked against 14.02. */
static int patch_1402_v119(uint64_t kernbase) {
    static const struct {
        uint32_t offset;
        uint8_t length;
        uint8_t original[8];
        uint8_t replacement[8];
        /* Loader prefixes: MOV AL,1; RET; NOP, or XOR EAX,EAX; JMP +1.
         * The short jump skips one leftover byte to the original continuation. */
        uint8_t loader_length;
        uint8_t loader[8];
    } patches[] = {
        { 0x2BDB5D, 1, { 0x76, 0x17, 0x49, 0x89, 0xEA, 0x49, 0x83, 0xC2 }, { 0xEB } }, /* site_00 */
        { 0x3B3BA0, 8, { 0x55, 0x48, 0x89, 0xE5, 0xE8, 0xC7, 0xC9, 0x06 }, { 0x48, 0xC7, 0xC0, 0x01, 0x00, 0x00, 0x00, 0xC3 }, 4, { 0xB0, 0x01, 0xC3, 0x90 } }, /* site_01 */
        { 0x3B3C10, 8, { 0x55, 0x48, 0x89, 0xE5, 0x48, 0x8B, 0x47, 0x68 }, { 0x48, 0xC7, 0xC0, 0x01, 0x00, 0x00, 0x00, 0xC3 }, 4, { 0xB0, 0x01, 0xC3, 0x90 } }, /* site_02 */
        { 0x3B3C30, 8, { 0x55, 0x48, 0x89, 0xE5, 0xF6, 0x47, 0x17, 0x04 }, { 0x48, 0xC7, 0xC0, 0x01, 0x00, 0x00, 0x00, 0xC3 }, 4, { 0xB0, 0x01, 0xC3, 0x90 } }, /* site_03 */
        { 0x76C9B0, 1, { 0x55, 0x48, 0x89, 0xE5, 0x41, 0x56, 0x53, 0x80 }, { 0xC3 } }, /* site_04 */
        { 0x1FCC01, 5, { 0xE8, 0xBA, 0x63, 0x44, 0x00, 0x85, 0xC0, 0x0F }, { 0x31, 0xC0, 0x90, 0x90, 0x90 }, 5, { 0x31, 0xC0, 0xEB, 0x01, 0x00 } }, /* site_05 */
        { 0x2FCEBC, 6, { 0x0F, 0x85, 0xAD, 0x07, 0x00, 0x00, 0x49, 0x8B }, { 0x90, 0x90, 0x90, 0x90, 0x90, 0x90 } }, /* site_06 */
        { 0x367765, 1, { 0x77, 0x1C, 0x48, 0xB8, 0x36, 0x10, 0x00, 0x00 }, { 0xEB } }, /* site_07 */
        { 0x367C51, 5, { 0x48, 0x8B, 0x41, 0x08, 0x44, 0x89, 0xC3, 0x48 }, { 0xE9, 0x7C, 0x02, 0x00, 0x00 } }, /* site_08 */
        { 0x478B94, 2, { 0x74, 0x0B, 0x49, 0xC7, 0x44, 0x24, 0x38, 0x00 }, { 0x90, 0x90 } }, /* site_09 */
        { 0x4669EC, 1, { 0x03, 0x00, 0x00, 0x00, 0x6A, 0x00, 0x50, 0x6A }, { 0x07 } }, /* site_10 */
        { 0x4669F4, 1, { 0x03, 0xE8, 0x86, 0x26, 0xE9, 0xFF, 0x48, 0x83 }, { 0x07 } }, /* site_11 */
        { 0x2BDD97, 2, { 0x77, 0x47, 0x48, 0xB8, 0xFF, 0xFF, 0xFF, 0xFF }, { 0x90, 0x90 } }, /* site_12 */
        { 0x2BDCA2, 2, { 0x77, 0x5C, 0x48, 0xB8, 0xFF, 0xFF, 0xFF, 0xFF }, { 0x90, 0x90 } }, /* site_13 */
        { 0x30492E, 6, { 0x0F, 0x85, 0x98, 0x00, 0x00, 0x00, 0xE8, 0x47 }, { 0x90, 0x90, 0x90, 0x90, 0x90, 0x90 } }, /* site_14 */
        { 0x135A55, 5, { 0xE8, 0xC6, 0xB0, 0x1A, 0x00, 0x48, 0x8B, 0x45 }, { 0x90, 0x90, 0x90, 0x90, 0x90 } }, /* site_15 */
        { 0x2A526A, 5, { 0xE8, 0xB1, 0xB8, 0x03, 0x00, 0x41, 0x89, 0xDF }, { 0x90, 0x90, 0x90, 0x90, 0x90 } }, /* site_16 */
    };
    int mismatch = 0;
    for (uint64_t i = 0; i < sizeof(patches) / sizeof(patches[0]); ++i) {
        const volatile uint8_t *code = (void *)(kernbase + patches[i].offset);
        uint8_t actual[8];
        int original = 1, patched = 1;
        int loader_match = patches[i].loader_length != 0;
        for (uint64_t j = 0; j < sizeof(patches[i].original); ++j) {
            uint8_t value = actual[j] = code[j];
            uint8_t expected = j < patches[i].length ?
                patches[i].replacement[j] : patches[i].original[j];
            original &= value == patches[i].original[j];
            patched &= value == expected;
            loader_match &= value == (j < patches[i].loader_length ?
                patches[i].loader[j] : patches[i].original[j]);
        }
        if (!original && !patched && !loader_match) {
            printf("[ps4debug-ng] 14.02 patch mismatch at %x - kernel not patched "
                   "(found: %02x %02x %02x %02x %02x %02x %02x %02x)\n",
                   patches[i].offset, (unsigned)actual[0], (unsigned)actual[1],
                   (unsigned)actual[2], (unsigned)actual[3], (unsigned)actual[4],
                   (unsigned)actual[5], (unsigned)actual[6], (unsigned)actual[7]);
            mismatch = 1;
        }
    }
    if (mismatch) {
        return 1;
    }

    /* These slots are nosys in the retail ELF. Do not overwrite another payload. */
    for (uint32_t slot = PS4DEBUG_SYS_PROC_LIST; slot <= PS4DEBUG_SYS_CONSOLE_CMD; ++slot) {
        if (sysents[slot].sy_call != (void *)(kernbase + 0x1C07A0)) {
            printf("[ps4debug-ng] 14.02 syscall %u is occupied - kernel not patched\n", slot);
            return 1;
        }
    }

    uint64_t cr0 = read_cr0();
    write_cr0(cr0 & ~X86_CR0_WP);
    for (uint64_t i = 0; i < sizeof(patches) / sizeof(patches[0]); ++i) {
        volatile uint8_t *code = (void *)(kernbase + patches[i].offset);
        for (uint64_t j = 0; j < patches[i].length; ++j) {
            code[j] = patches[i].replacement[j];
        }
    }
    write_cr0(cr0);
    return 0;
}

int patch_kernel(void) {
    uint64_t kernbase = get_kbase();

    if (cachedFirmware == 1400) {
        return patch_1400_v119(kernbase);
    }
    if (cachedFirmware == 1402) {
        return patch_1402_v119(kernbase);
    }

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
        case 1302: case 1304:
            patch_1304_v119(kernbase);
            break;
        case 1350:
            patch_1350_v119(kernbase);
            break;
        case 1352:
            patch_1352_v119(kernbase);
            break;
        default:
            printf("[ps4debug-ng] unsupported firmware %u - kernel not patched\n",
                   cachedFirmware);
            cpu_enable_wp();
            return 1;
    }

    cpu_enable_wp();
    return 0;
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

    /* No guessed firmware fallback: unresolved kernel pointers are unsafe. */
    if (init_ksdk()) {
        return 1;
    }

    ascii_art();

    printf("[ps4debug-ng] patching kernel...\n");
    if (patch_kernel()) {
        return 1;
    }

    printf("[ps4debug-ng] loading kdebugger...\n");
    if(load_kdebugger()) {
        return 1;
    }

    printf("[ps4debug-ng] loading debugger...\n");
    if(load_debugger()) {
        return 1;
    }

    printf("[ps4debug-ng] PS4Debug-NG by Pharaoh2k v" PS4DEBUG_NG_VERSION_STR "\n");
    return 0;
}
