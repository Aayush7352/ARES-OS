#ifndef ARES_RTC_H
#define ARES_RTC_H

#include <stdint.h>

/* CMOS RTC registers (indexed via port 0x70) */
#define CMOS_SECONDS    0x00
#define CMOS_MINUTES    0x02
#define CMOS_HOURS      0x04
#define CMOS_WEEKDAY    0x06
#define CMOS_DAY        0x07
#define CMOS_MONTH      0x08
#define CMOS_YEAR       0x09
#define CMOS_STATUS_A   0x0A
#define CMOS_STATUS_B   0x0B

/* Initialize RTC (no-op for now) */
void rtc_init(void);

/* Read current time/date values directly from CMOS */
uint8_t rtc_read_second(void);
uint8_t rtc_read_minute(void);
uint8_t rtc_read_hour(void);
uint8_t rtc_read_day(void);
uint8_t rtc_read_month(void);
uint8_t rtc_read_year(void);

/* Format a timestamp string into buf (max len 32) */
void rtc_format_time(char *buf, uint32_t max_len);

#endif /* ARES_RTC_H */
