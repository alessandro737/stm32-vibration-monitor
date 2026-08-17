// main.c  --  SPI bit0 corruption: model discrimination harness
// TEMPORARY. Delete once root cause is logged.

#include <stm32f4xx.h>
#include "bsp/clock.h"
#include "bsp/board.h"
#include "drivers/spi.h"
#include "drivers/ADXL345.h"

#define N          8
#define BLK_BASE   0x1D          /* 0x1D..0x24, all writable scratch */

/* Sequence A: write-free. Only reset values, so the write path is not
 * involved at all. LSB order 1,1,0,0,1,0,1,1 -- deliberately NOT
 * alternating, so stale-hold and inversion predict different strings. */
static const uint8_t seqA_reg[N] = {
    0x00, 0x00, 0x2C, 0x2C, 0x00, 0x2C, 0x00, 0x00
};
static const uint8_t seqA_want[N] = {
    0xE5, 0xE5, 0x0A, 0x0A, 0xE5, 0x0A, 0xE5, 0xE5
};

/* Sequence B/C pattern: bit0 and bit1 vary INDEPENDENTLY, and every
 * upper-6 value is unique so any whole-byte shift is unmistakable.
 *   bit0: 1,1,1,0,0,0,1,0
 *   bit1: 0,1,0,1,0,1,1,0   */
static const uint8_t patB[N] = {
    0x85, 0x8B, 0x8D, 0x92, 0x94, 0x9A, 0x9F, 0xA0
};

/* ---- captures ---- */
static volatile uint8_t logA[N], logB[N], logC[N];
static volatile uint16_t sr_pre[3] = {0xFFFF, 0xFFFF, 0xFFFF};

/* ---- model scores, each out of N-1 = 7 ---- */
static volatile uint8_t scoreA[4] = {0xAA, 0xAA, 0xAA, 0xAA};
static volatile uint8_t scoreB[4] = {0xAA, 0xAA, 0xAA, 0xAA};
static volatile uint8_t scoreC[4] = {0xAA, 0xAA, 0xAA, 0xAA};
static volatile uint8_t reached_end = 0xAA;

/* score[0] clean, [1] stale-hold, [2] inversion, [3] bit0:=bit1 */
static void score_models(const volatile uint8_t *got,
                         const uint8_t *want,
                         volatile uint8_t *out)
{
    uint8_t i, c = 0, s = 0, v = 0, b = 0;

    for (i = 1; i < N; i++) {
        uint8_t up = want[i] & 0xFEU;
        if (got[i] == want[i])                        c++;
        if (got[i] == (up | (want[i - 1] & 1U)))      s++;
        if (got[i] == (want[i] ^ 1U))                 v++;
        if (got[i] == (up | ((want[i] >> 1) & 1U)))   b++;
    }
    out[0] = c; out[1] = s; out[2] = v; out[3] = b;
}

int main(void)
{
    uint8_t i, buf[N];

    for (i = 0; i < N; i++) {
        logA[i] = 0xAA; logB[i] = 0xAA; logC[i] = 0xAA;
    }

    clock_init();
    spi1_init();
    adxl345_init();

    /* ---- A: write-free reads, one CS assertion each ---- */
    sr_pre[0] = (uint16_t)SPI1->SR;
    for (i = 0; i < N; i++)
        logA[i] = adxl345_read_register(seqA_reg[i]);

    /* ---- seed the scratch block ---- */
    for (i = 0; i < N; i++)
        adxl345_write_register(BLK_BASE + i, patB[i]);

    /* ---- B: individual reads of the seeded block ---- */
    sr_pre[1] = (uint16_t)SPI1->SR;
    for (i = 0; i < N; i++)
        logB[i] = adxl345_read_register(BLK_BASE + i);

    /* ---- C: same block, ONE CS assertion ---- */
    sr_pre[2] = (uint16_t)SPI1->SR;
    adxl345_read_multiple_registers(BLK_BASE, buf, N);
    for (i = 0; i < N; i++)
        logC[i] = buf[i];

    score_models(logA, seqA_want, scoreA);
    score_models(logB, patB,      scoreB);
    score_models(logC, patB,      scoreC);

    /* restore offset regs so later acceleration reads aren't skewed */
    adxl345_write_register(0x1E, 0x00);
    adxl345_write_register(0x1F, 0x00);
    adxl345_write_register(0x20, 0x00);

    reached_end = 1;

    while (1)
        ;
}