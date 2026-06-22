#include "rtc.h"
#include "../lib/io.h"

/*==============================================================================
 * CMOS RTC Driver for ARES OS
 *
 * Reads the real-time clock via the CMOS RAM at ports 0x70 (index) and
 * 0x71 (data).  The RTC typically runs in BCD mode; this driver converts
 * to binary integers.
 *
 * Port 0x70: bit 7 = NMI disable (1 = disable), bits 0-6 = register index.
 * Port 0x71: data byte for the selected register.
 *==============================================================================
 *
 * Status Register B (0x0B):
 *   bit 1  (0x02) — 24-hour mode (0 = 12h, 1 = 24h)
 *   bit 2  (0x04) — BCD mode (0 = BCD, 1 = binary)
 *==============================================================================*/

/*==============================================================================
 * Helper: read a CMOS register
 *==============================================================================*/

/* NMI enable/disable — we enable NMI (bit 7 = 0) after reads */
#define CMOS_NMI_ENABLE  0x00

static uint8_t cmos_read(uint8_t reg)
{
    outb(0x70, reg);
    /* Small I/O delay — reading 0x80 port works as a delay on QEMU/real HW */
    inb(0x80);
    return inb(0x71);
}

/*==============================================================================
 * Helper: convert BCD to binary
 *==============================================================================*/
static uint8_t bcd_to_bin(uint8_t bcd)
{
    return (uint8_t)(((bcd >> 4) & 0x0F) * 10 + (bcd & 0x0F));
}

/*==============================================================================
 * Public API
 *==============================================================================*/

void rtc_init(void)
{
    /* Nothing to initialise — CMOS is always available on PC hardware.
       We could set up periodic interrupts here, but for now just read
       the values on demand. */

    /* Read status register B to determine encoding mode */
    uint8_t status_b = cmos_read(CMOS_STATUS_B);
    (void)status_b;

    /* Re-enable NMI after probe */
    outb(0x70, CMOS_NMI_ENABLE);
}

uint8_t rtc_read_second(void)
{
    uint8_t val = cmos_read(CMOS_SECONDS);
    outb(0x70, CMOS_NMI_ENABLE);
    return bcd_to_bin(val);
}

uint8_t rtc_read_minute(void)
{
    uint8_t val = cmos_read(CMOS_MINUTES);
    outb(0x70, CMOS_NMI_ENABLE);
    return bcd_to_bin(val);
}

uint8_t rtc_read_hour(void)
{
    uint8_t val = cmos_read(CMOS_HOURS);
    outb(0x70, CMOS_NMI_ENABLE);
    /* Status register B bit 1 = 1 means 24h mode */
    uint8_t status_b = cmos_read(CMOS_STATUS_B);
    outb(0x70, CMOS_NMI_ENABLE);
    uint8_t hour = bcd_to_bin(val);
    if (!(status_b & 0x02)) {
        /* 12h mode — check bit 7 for PM flag */
        if (val & 0x80) {
            hour = (uint8_t)((hour & 0x7F) + 12);
        }
    }
    return hour;
}

uint8_t rtc_read_day(void)
{
    uint8_t val = cmos_read(CMOS_DAY);
    outb(0x70, CMOS_NMI_ENABLE);
    return bcd_to_bin(val);
}

uint8_t rtc_read_month(void)
{
    uint8_t val = cmos_read(CMOS_MONTH);
    outb(0x70, CMOS_NMI_ENABLE);
    return bcd_to_bin(val);
}

uint8_t rtc_read_year(void)
{
    uint8_t val = cmos_read(CMOS_YEAR);
    outb(0x70, CMOS_NMI_ENABLE);
    return bcd_to_bin(val);
}

void rtc_format_time(char *buf, uint32_t max_len)
{
    uint8_t hh = rtc_read_hour();
    uint8_t mm = rtc_read_minute();
    uint8_t ss = rtc_read_second();
    uint8_t dd = rtc_read_day();
    uint8_t mo = rtc_read_month();
    uint8_t yy = rtc_read_year();

    /* Build a format string manually — no sprintf in freestanding */
    const char *src = "20  /  /     : : ";
    char *dst = buf;
    uint32_t remain = max_len > 0 ? max_len : 1;

    for (const char *p = src; *p && remain > 1; p++) {
        *dst = *p;
        dst++;
        remain--;
    }

    /* Insert two-digit values — only works if we know the template offsets.
       Simpler: write known positions. */
    if (max_len >= 24) {
        buf[0] = '2';
        buf[1] = '0';
        buf[2] = (char)('0' + (yy / 10));
        buf[3] = (char)('0' + (yy % 10));
        buf[4] = '-';
        buf[5] = (char)('0' + (mo / 10));
        buf[6] = (char)('0' + (mo % 10));
        buf[7] = '-';
        buf[8] = (char)('0' + (dd / 10));
        buf[9] = (char)('0' + (dd % 10));
        buf[10] = ' ';
        buf[11] = (char)('0' + (hh / 10));
        buf[12] = (char)('0' + (hh % 10));
        buf[13] = ':';
        buf[14] = (char)('0' + (mm / 10));
        buf[15] = (char)('0' + (mm % 10));
        buf[16] = ':';
        buf[17] = (char)('0' + (ss / 10));
        buf[18] = (char)('0' + (ss % 10));
        buf[19] = '\0';
    } else if (max_len > 0) {
        buf[0] = '\0';
    }
}
