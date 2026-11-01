#ifndef BERIN_RTC_H
#define BERIN_RTC_H

#include <stdint.h>

struct rtc_time {
    uint8_t  sec;
    uint8_t  min;
    uint8_t  hour;
    uint8_t  day;
    uint8_t  month;
    uint16_t year;
};

void rtc_read(struct rtc_time *t);

#endif
