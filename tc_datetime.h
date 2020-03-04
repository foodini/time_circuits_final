#ifndef _TC_DATETIME_H
#define _TC_DATETIME_H
#include "Arduino.h"
#include "tc_util.h"

// I chose not to use time.h (and a number of other libs) because none of them can be updated to take
// leap seconds into account. If I were to use one of them to compute relative times, for each leap
// second that passed between compilation and execution, the relative times would be off by one second.
// By doing everything relative to the time reported by GPS, my only difficulty is in computing time
// offsets of more than a month. I can live with that.

class Datetime {
  public:
    // These are all signed ints because we do subtraction on time. When hour == -1, hour becomes 23 and day--.
    int16_t year;
    int8_t  month;
    int8_t  day;
    int32_t hour;
    int32_t minute;
    int32_t seconds;
    int32_t milliseconds;

    Datetime(int16_t year, int8_t  month, int8_t  day, int32_t hour, int32_t minute, int32_t seconds, int32_t milliseconds) :
      year(year), month(month), day(day), hour(hour), minute(minute), seconds(seconds), milliseconds(milliseconds) {}
    Datetime(const Adafruit_GPS& gps);
    void operator=(const Adafruit_GPS& gps);
    bool operator==(const Datetime& rhs);
    bool operator!=(const Datetime& rhs);
    void init_from_adafruit_gps(const Adafruit_GPS& gps);
    void make_time_str(char * str);
    bool leap_year();
    char* get_month_str(int m);


    void normalize();
#ifdef RUN_TESTS
    void print();
    static void run_test(Datetime* start_time, Datetime* end_time, int32_t milliseconds);
    static void compare_result(Datetime* start, Datetime* result, Datetime* expected, int32_t milliseconds);
    static void run_tests();
#endif
};

#endif // _TC_DATETIME_H
