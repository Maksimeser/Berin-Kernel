/* Berin OS - CMOS/RTC (ports 0x70/0x71)
 * Handles BCD and 12h/24h modes.
 */

#include "rtc.h"
#include "io.h"

#define CMOS_ADDR 0x70
#define CMOS_DATA 0x71

static uint8_t cmos_read(uint8_t reg) {
    outb(CMOS_ADDR, reg);
    return inb(CMOS_DATA);
}

static int update_in_progress(void) {
    return cmos_read(0x0A) & 0x80;
}

static uint8_t bcd_to_bin(uint8_t v) {
    return (uint8_t)((v & 0x0F) + ((v >> 4) * 10));
}

void rtc_read(struct rtc_time *t) {
    while (update_in_progress()) ;

    uint8_t sec   = cmos_read(0x00);
    uint8_t min   = cmos_read(0x02);
    uint8_t hour  = cmos_read(0x04);
    uint8_t day   = cmos_read(0x07);
    uint8_t month = cmos_read(0x08);
    uint8_t year  = cmos_read(0x09);
    uint8_t regB  = cmos_read(0x0B);

    /* if not binary, convert BCD -> bin */
    if (!(regB & 0x04)) {
        sec   = bcd_to_bin(sec);
        min   = bcd_to_bin(min);
        hour  = bcd_to_bin(hour & 0x7F) | (hour & 0x80);
        day   = bcd_to_bin(day);
        month = bcd_to_bin(month);
        year  = bcd_to_bin(year);
    }

    /* if 12h mode, convert to 24h */
    if (!(regB & 0x02)) {
        uint8_t pm = hour & 0x80;
        hour = hour & 0x7F;
        if (pm) hour = (uint8_t)((hour % 12) + 12);
        else    hour = (uint8_t)(hour % 12);
        if (hour == 0) hour = 12;
        if (pm && hour != 12) hour = (uint8_t)(hour + 12);
        if (!pm && hour == 12) hour = 0;
    }

    t->sec   = sec;
    t->min   = min;
    t->hour  = hour;
    t->day   = day;
    t->month = month;
    t->year  = (uint16_t)(2000 + year);
}
