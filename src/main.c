// main.c  --  SPI verification after OSPEEDR fix
// TEMPORARY. Delete once verified and logged.

#include <stm32f4xx.h>
#include "bsp/clock.h"
#include "bsp/board.h"
#include "drivers/spi.h"
#include "drivers/ADXL345.h"

#define N          8
#define BLK_BASE   0x1D          /* 0x1D..0x24, writable scratch */

/* A: write-free. Reset values only, so the write path is not involved. */
static const uint8_t seqA_reg[N] = {
    0x00, 0x00, 0x2C, 0x2C, 0x00, 0x2C, 0x00, 0x00
};
static const uint8_t seqA_want[N] = {
    0xE5, 0xE5, 0x0A, 0x0A, 0xE5, 0x0A, 0xE5, 0xE5
};

/* B/C: bit0 and bit1 vary independently; every upper-6 value unique. */
static const uint8_t patB[N] = {
    0x85, 0x8B, 0x8D, 0x92, 0x94, 0x9A, 0x9F, 0xA0
};

static volatile uint8_t logA[N], logB[N], logC[N];
static volatile uint8_t scoreA[4] = {0xAA, 0xAA, 0xAA, 0xAA};
static volatile uint8_t scoreB[4] = {0xAA, 0xAA, 0xAA, 0xAA};
static volatile uint8_t scoreC[4] = {0xAA, 0xAA, 0xAA, 0xAA};

static volatile int16_t accel_x = 0x7FFF;
static volatile int16_t accel_y = 0x7FFF;
static volatile int16_t accel_z = 0x7FFF;
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
    ADXL345_Accel a;

    for (i = 0; i < N; i++) {
        logA[i] = 0xAA; logB[i] = 0xAA; logC[i] = 0xAA;
    }

    clock_init();
    spi1_init();
    adxl345_init();

    /* ---- A: write-free reads, one CS assertion each ---- */
    for (i = 0; i < N; i++)
        logA[i] = adxl345_read_register(seqA_reg[i]);
    score_models(logA, seqA_want, scoreA);

    /* ---- seed scratch block ---- */
    for (i = 0; i < N; i++)
        adxl345_write_register(BLK_BASE + i, patB[i]);

    /* ---- B: individual reads of the seeded block ---- */
    for (i = 0; i < N; i++)
        logB[i] = adxl345_read_register(BLK_BASE + i);
    score_models(logB, patB, scoreB);

    /* ---- C: same block, ONE CS assertion (burst path) ---- */
    adxl345_read_multiple_registers(BLK_BASE, buf, N);
    for (i = 0; i < N; i++)
        logC[i] = buf[i];
    score_models(logC, patB, scoreC);

    /* ---- restore offset regs, then take a live sample ---- */
    adxl345_write_register(0x1E, 0x00);
    adxl345_write_register(0x1F, 0x00);
    adxl345_write_register(0x20, 0x00);

    adxl345_write_register(ADXL345_REG_DATA_FORMAT, 0x00);  /* +/-2g, 10-bit */
    adxl345_write_register(ADXL345_REG_BW_RATE,     0x0A);  /* 100 Hz        */
    adxl345_write_register(ADXL345_REG_POWER_CTL,   0x08);  /* measure mode  */

    for (volatile uint32_t d = 0; d < 200000; d++)
        ;

    adxl345_read_acceleration(&a);
    accel_x = a.x;
    accel_y = a.y;
    accel_z = a.z;

    reached_end = 1;

    while (1)
        ;
}