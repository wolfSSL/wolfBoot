/* unit-psoc-c3-divider.c
 *
 * Exercises the PSOC Control C3 16.5 fractional peripheral divider maths.
 * The HAL cannot be run on the host, but this computation is pure and decides
 * the console baud rate and the SPI clock, so it is worth covering here.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <inttypes.h>

#include "../../hal/psoc_c3.h"

static int failures;

/* Reconstruct the frequency the divider fields actually produce. */
static uint32_t produced(uint32_t pclk, uint32_t i, uint32_t f)
{
    return (uint32_t)(((uint64_t)pclk * 32U) / (((uint64_t)i + 1U) * 32U + f));
}

static void check(uint32_t pclk, uint32_t target, uint32_t exp_i,
        uint32_t exp_f, const char *what)
{
    uint32_t i = 0xFFFFFFFFUL, f = 0xFFFFFFFFUL;

    psoc_c3_div16_5(pclk, target, &i, &f);
    if ((i != exp_i) || (f != exp_f)) {
        printf("FAIL %s: pclk=%" PRIu32 " target=%" PRIu32 " -> int=%" PRIu32 " frac=%" PRIu32 ", expected %" PRIu32 "/%" PRIu32 "\n",
                what, pclk, target, i, f, exp_i, exp_f);
        failures++;
        return;
    }
    printf("ok   %s: int=%" PRIu32 " frac=%" PRIu32 " -> %" PRIu32 " Hz\n", what, i, f,
            produced(pclk, i, f));
}

int main(void)
{
    uint32_t i, f, hz;

    /* Console: 115200 baud at oversample 8 from the power-on IHO rate. */
    check(48000000UL, 115200UL * 8UL, 51, 2, "115200x8 from 48MHz");

    /* SPI bring-up rate: 1 MHz at oversample 4 divides exactly. */
    check(48000000UL, 1000000UL * 4UL, 11, 0, "1MHz x4 from 48MHz");

    /* A target at or above the source clamps to a divisor of one. */
    check(48000000UL, 48000000UL, 0, 0, "target == pclk");
    check(48000000UL, 96000000UL, 0, 0, "target above pclk");

    /* A target low enough to overflow the 16-bit integer field clamps. */
    psoc_c3_div16_5(48000000UL, 1UL, &i, &f);
    if (i > 0xFFFFUL) {
        printf("FAIL int field overflowed: %" PRIu32 "\n", i);
        failures++;
    }
    else {
        printf("ok   very low target clamps: int=%" PRIu32 " frac=%" PRIu32 "\n", i, f);
    }

    /* Zero target must not divide by zero, and must leave outputs alone. */
    i = 0xAAAAUL; f = 0x55UL;
    psoc_c3_div16_5(48000000UL, 0UL, &i, &f);
    if ((i != 0xAAAAUL) || (f != 0x55UL)) {
        printf("FAIL zero target modified outputs: %" PRIu32 "/%" PRIu32 "\n", i, f);
        failures++;
    }
    else {
        printf("ok   zero target is rejected\n");
    }

    /* The console rate must land within 2%% or the UART will not frame. */
    psoc_c3_div16_5(48000000UL, 115200UL * 8UL, &i, &f);
    hz = produced(48000000UL, i, f) / 8U;
    if ((hz < (115200UL - 2304UL)) || (hz > (115200UL + 2304UL))) {
        printf("FAIL console baud %" PRIu32 " Hz is out of tolerance\n", hz);
        failures++;
    }
    else {
        printf("ok   console baud %" PRIu32 " Hz within 2%% of 115200\n", hz);
    }

    /* The integer divider the I2C clock uses rounds to nearest, so check the
     * rate it produces rather than the register value. The same expression is
     * in psoc_c3_pclk_setup(); a bus that is too fast is out of spec, so the
     * tolerance is one-sided in practice but checked both ways here. */
    i = psoc_c3_div16(48000000UL, 100000UL * 16UL);
    hz = 48000000UL / i / 16U;
    if ((hz < 90000UL) || (hz > 100000UL)) {
        printf("FAIL i2c rate %" PRIu32 " Hz out of tolerance (divider %" PRIu32 ")\n", hz, i);
        failures++;
    }
    else {
        printf("ok   i2c rate %" PRIu32 " Hz within tolerance of 100000\n", hz);
    }

    /* A target faster than the source must still divide by at least one
     * rather than by zero. */
    i = psoc_c3_div16(48000000UL, 96000000UL);
    if (i == 0UL) {
        printf("FAIL integer divider returned zero\n");
        failures++;
    }
    else {
        printf("ok   over-fast target clamps to divider %" PRIu32 "\n", i);
    }

    /* The register field is 16 bits and holds the divisor less one, so a
     * target slow enough to overflow it must clamp rather than wrap. */
    i = psoc_c3_div16(48000000UL, 1UL);
    if (i != 0x10000UL) {
        printf("FAIL slow target did not clamp (divider %" PRIu32 ")\n", i);
        failures++;
    }
    else {
        printf("ok   over-slow target clamps to divider %" PRIu32 "\n", i);
    }

    /* A zero target is a caller error; it must not divide by zero. */
    i = psoc_c3_div16(48000000UL, 0UL);
    if (i == 0UL) {
        printf("FAIL zero target returned zero\n");
        failures++;
    }
    else {
        printf("ok   zero target yields divider %" PRIu32 "\n", i);
    }

    /* A non-integral ratio is where rounding direction shows. The I2C rate is
     * a ceiling, so the produced clock must never come out above it; 50 MHz
     * with the default 100 kHz bus is the documented case that rounding to
     * nearest got wrong. */
    i = psoc_c3_div16(50000000UL, 100000UL * 16UL);
    hz = 50000000UL / i / 16U;
    if (hz > 100000UL) {
        printf("FAIL non-integral ratio exceeds the bus maximum: %" PRIu32
                " Hz (divider %" PRIu32 ")\n", hz, i);
        failures++;
    }
    else {
        printf("ok   non-integral ratio stays under the maximum: %" PRIu32
                " Hz\n", hz);
    }

    if (failures != 0) {
        printf("%d failure(s)\n", failures);
        return 1;
    }
    printf("All PSOC C3 divider tests passed\n");
    return 0;
}
