#include <Adafruit_GPS.h>
#include "tc_datetime.h"

Datetime::Datetime(const Adafruit_GPS& gps) {
  init_from_adafruit_gps(gps);
}

void Datetime::operator=(const Adafruit_GPS& gps) {
  init_from_adafruit_gps(gps);
}

bool Datetime::operator==(const Datetime& rhs) {
  return (milliseconds == rhs.milliseconds && seconds == rhs.seconds && minute == rhs.minute && 
          hour == rhs.hour && day == rhs.day && month == rhs.month && year == rhs.year);
}

bool Datetime::operator!=(const Datetime& rhs) {
  return !operator==(rhs);
}

void Datetime::init_from_adafruit_gps(const Adafruit_GPS& gps) {
  year = uint16_t(gps.year)+2000;
  month = gps.month;
  day = gps.day;
  hour = gps.hour;
  minute = gps.minute;
  seconds = gps.seconds;
  milliseconds = gps.milliseconds;      
}

char* Datetime::get_month_str(int m) {
  switch (m) {
    case  1: return (char *)"JAN ";
    case  2: return (char *)"FEB ";
    case  3: return (char *)"MAR ";
    case  4: return (char *)"APR ";
    case  5: return (char *)"MAY ";
    case  6: return (char *)"JUN ";
    case  7: return (char *)"JUL ";
    case  8: return (char *)"AUG ";
    case  9: return (char *)"SEP ";
    case 10: return (char *)"OCT ";
    case 11: return (char *)"NOV ";
    case 12: return (char *)"DEC ";
    default: return (char *)"    ";
  }
}

/*
 * Returns whether self's year is a leap year.
 */
bool Datetime::leap_year() {
  if (year % 4 == 0) {
    if (year % 100 == 0) {
      if (year % 400 == 0) {
        return true;
      }
      return false;
    }
    return true;
  }
  return false;
}

/*
 * Pack the Datetime into a character string, fit for display through the LED controller.
 * The format will look something like this:
 * 
 *  "DEC 132020101500"
 * 
 * It ends up being more readable on the LED display, as the presentation is more like:
 * 
 *  "DEC  13 2020 10:15:00"
 */
void Datetime::make_time_str(char* str) {
  bare_strcpy(str, Datetime::get_month_str(month));
  str[3] = ' ';
  // TODO: use a 0-filling rjust for this
  str[4] = '0' + day / 10;
  str[5] = '0' + day % 10;
  itoa(year, str + 6, 10);
  str[10] = '0' + hour / 10;
  str[11] = '0' + hour % 10;
  str[12] = '0' + minute / 10;
  str[13] = '0' + minute % 10;
  str[14] = '0' + seconds / 10;
  str[15] = '0' + seconds % 10;
}

/*
 * You can add or subtract small deltas to/from Datetimes, but to ensure that the result is a
 * valid date and time afterward, call normalize().
 * 
 * WARNING:
 * Don't try to add or subtract more than 24 hours. Bad things may happen at month boundaires - 
 * especially with regard to leap days. 
 */
void Datetime::normalize() {
  if (milliseconds < 0) {
    seconds += (milliseconds/1000) - 1;
    milliseconds = milliseconds % 1000 + 1000;
  }
  // This isn't an else-if case because it's possible milliseconds became exactly 1000 as a result of
  // the if(milliseconds < 0) block.
  if (milliseconds >= 1000) {
    seconds += milliseconds / 1000;
    milliseconds = milliseconds % 1000;
  }

  if (seconds < 0) {
    minute += (seconds/60) - 1;
    seconds = seconds % 60 + 60;
  } 
  if (seconds >= 60) {
    minute += seconds / 60;
    seconds = seconds % 60;
  }

  if (minute < 0) {
    hour += (minute/60) - 1;
    minute = minute % 60 + 60;
  } 
  if (minute >= 60) {
    hour += minute / 60;
    minute = minute % 60;
  }

  if (hour < 0) {
    day += (hour/24) - 1;
    hour = hour % 24 + 24;
  } 
  if (hour >= 24) {
    day += hour / 24;
    hour = hour % 24;
  }

  if (day < 1) {
    switch (month) {
      case 1: case 2: case 4: case 6: case 8: case 9: case 11:
        day = 31; break;
      case 3:
        if (leap_year())
          day = 29;
        else
          day = 28;
        break;
      default:
        day = 30;
    }
    month -= 1;
  } else {
    switch (month) {
      case 1: case 3: case 5: case 7: case 8: case 10: case 12:
        if (day > 31) {
          day = 1;
          month += 1;
        }
        break;
      case 2:
        if (leap_year()) {
          if (day > 29) {
            day = 1;
            month = 3;
          }
        }
        else if (day > 28) {
          day = 1;
          month = 3;
        }      
        break;
      default:
        if (day > 30) {
          day = 1;
          month += 1;
        }
        break;
    }
  }

  if (month < 1) {
    month = 12;
    year -= 1;
  }

  if (month == 13) {
    month = 1;
    year += 1;
  }

  switch (month) {
    //advancing
    case 1: //31 -> 1
    case 3:
    case 5:
    case 7:
    case 8:
    case 10:
    case 12:
      if (day > 31) {
        if (month == 12) {
          year++;
          month = 0;
        }
        day = 1;
        month++;
      }
      break;
    case 2: // 2[89] -> 1
      if ((year % 4 == 0 && day > 29) || (year % 4 && day > 28)) {
        day = 1;
        month = 3;
      }
      break;
    case 4: // 30 -> 1
    case 6:
    case 9:
    case 11:
      if (day > 30) {
        day = 1;
        month ++;
      }
  }

  if (day < 1) {
    switch (month) {
      //retreating
      case 1: //
        day = 31;
        month--;
        year--;
        break;
      case 2: // 1 -> 31
      case 4:
      case 6:
      case 8:
      case 9:
      case 11:
        day = 31;
        month--;
        break;
      case 3: // 1 -> 2[29]
        if (year % 4 == 0)
          day = 29;
        else
          day = 28;
        month = 2;
        break;
      case 5: // 1 -> 30
      case 7:
      case 10:
      case 12:
        day = 30;
        month--;
        break;
    }
  }
}

#ifdef RUN_TESTS

/*
 * For the sake of displaying a failed test's result, this method dumps the Datetime to Serial like:
 * 
 *  "2020.02.29 01:59:59.999"
 *  (YYYY MM DD HH MM SS.SSS)
 */
void Datetime::print() {
  Serial.print(year);
  Serial.print('.');
  if(month < 10)
    Serial.print(0);
  Serial.print(month);
  Serial.print('.');
  if(day < 10)
    Serial.print(0);
  Serial.print(day);
  
  Serial.print(' ');
  if (hour < 10)
    Serial.print(0);
  Serial.print(hour);
  Serial.print(':');
  if (minute < 10)
    Serial.print(0);
  Serial.print(minute);
  Serial.print(':');
  if (seconds < 10)
    Serial.print(0);
  Serial.print(seconds);
  Serial.print('.');
  if (milliseconds < 100) {
    Serial.print(0);
    if (milliseconds < 10) {
      Serial.print(0);
    }
  }
  Serial.print(milliseconds);
}


// USE PRINT()
void Datetime::compare_result(Datetime* start, Datetime* result, Datetime* expected, int32_t milliseconds) {
  static int passed = 0;
  static int failed = 0;
  if(*result == *expected) {
    passed++;
  } else {
    failed++;
    char buf[17];
    Serial.println("FAILED: ");
    Serial.print(" start:    ");
    start->print();
    if(milliseconds >= 0)
      Serial.print(" +");
    else
      Serial.print(" ");
    Serial.print(milliseconds);
    Serial.println("ms");
    Serial.print(" expected: ");
    expected->print();
    Serial.println();
    Serial.print(" result:   ");
    result->print();
    Serial.println();
  }
  Serial.print(passed);
  Serial.print(" passed, ");
  Serial.print(failed);
  Serial.println(" failed.");  
}

/*
 * Given a start time, an end time, and the delta, in ms, between them, do the math-and-normalize
 * in both directions and check that the result matches expectations.
 */
void Datetime::run_test(Datetime* start_time, Datetime* end_time, int32_t milliseconds) {
  {
    Datetime result(*start_time);
    int32_t dt = milliseconds;
    result.milliseconds += dt % 1000;
    dt /= 1000;
    result.seconds += dt % 60;
    dt /= 60;
    result.minute += dt % 60;
    dt /= 60;
    result.hour += dt % 24;
    dt /= 24;
    result.day += dt;
    result.normalize();
    compare_result(start_time, &result, end_time, milliseconds);
  }  
  {
    Datetime result(*end_time);
    int32_t dt = milliseconds;
    result.milliseconds -= dt % 1000;
    dt /= 1000;
    result.seconds -= dt % 60;
    dt /= 60;
    result.minute -= dt % 60;
    dt /= 60;
    result.hour -= dt % 24;
    dt /= 24;
    result.day -= dt;
    result.normalize();
    compare_result(end_time, &result, start_time, -milliseconds);
  }  
}

/*
 * Every time I've had an issue with the conversion, I add a test that would catch it.
 */
void Datetime::run_tests() {
  {
    // Test that math within the 29th of Feb is correct on leap years:
    Datetime start(2020, 2, 29, 10, 0, 0, 0);
    Datetime expected(2020, 2, 29, 2, 0, 0, 0);
    Datetime::run_test(&start, &expected, -28800000L);
  }
  {
    //Test that the leap year day exists in years that are divisible by 400:
    Datetime start(2000, 3, 1, 4, 0, 0, 0);
    Datetime expected(2000, 2, 29, 20, 0, 0, 0);
    Datetime::run_test(&start, &expected, -28800000L);
  }
  {
    //Test that the leap year day DOES NOT exist in years that are divisible by 100:
    Datetime start(2100, 3, 1, 4, 0, 0, 0);
    Datetime expected(2100, 2, 28, 20, 0, 0, 0);
    Datetime::run_test(&start, &expected, -28800000L);
  }
  {
    //Test the wraparound at the beginning of the year.
    Datetime start(2000, 1, 1, 4, 0, 0, 0);
    Datetime expected(1999, 12, 31, 20, 0, 0, 0);
    Datetime::run_test(&start, &expected, -28800000L);
  }
  {
    Datetime start(2020, 2, 29, 4, 0, 0, 0);
    Datetime expected(2020, 2, 28, 20, 0, 0, 0);
    Datetime::run_test(&start, &expected, -28800000L);
  }
  {
    Datetime start(2020, 3, 1, 4, 0, 0, 0);
    Datetime expected(2020, 2, 29, 20, 0, 0, 0);
    Datetime::run_test(&start, &expected, -28800000L);
  }
}
#endif
