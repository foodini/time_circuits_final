#include "Wire.h"

//#include <SoftwareSerial.h>  //Must be here to avoid weird compile error in Adafruit_GPS.h
#include <Adafruit_GPS.h>
#include <EEPROM.h>

#include "tc_datetime.h"
#include "tc_util.h"

/*
 * TODO:
 * 
 *  * There are only 38 actual time zones (https://en.wikipedia.org/wiki/List_of_UTC_time_offsets)
 *  * We have one major problem remaining. The 61st second of leap seconds.
 *  * Break out the EEPROM comms into its own .h/.cpp
 *  * Break out button handling into its own .h/.cpp
 *  * Fix the discontinuity in solar_offset. I wrote it very late at night and never got back.
 *  * Standardize commenting style.
 */

/*
      AD1 AD0 A6 A5 A4 A3 A2 A1 A0
  0   GND GND 1  1  0  0  0  0  0
  1   GND V+  1  1  0  0  0  0  1
  2   GND SDA 1  1  0  0  0  1  0
  3   GND SCL 1  1  0  0  0  1  1
  4   V+  GND 1  1  0  0  1  0  0
  5   V+  V+  1  1  0  0  1  0  1
  6   V+  SDA 1  1  0  0  1  1  0
  7   V+  SCL 1  1  0  0  1  1  1
  8   SDA GND 1  1  0  1  0  0  0
  9   SDA V+  1  1  0  1  0  0  1
  10  SDA SDA 1  1  0  1  0  1  0
  11  SDA SCL 1  1  0  1  0  1  1
  12  SCL GND 1  1  0  1  1  0  0
  13  SCL V+  1  1  0  1  1  0  1
  14  SCL SDA 1  1  0  1  1  1  0
  15  SCL SCL 1  1  0  1  1  1  1
*/

class Stopwatch {
  private:
    unsigned long m_start_time;
  public:
    Stopwatch() {
      reset();
    }
    void reset() {
      m_start_time = millis();
    }
    unsigned long elapsed() {
      unsigned long cur_time = millis();
      if (cur_time < m_start_time)
        return 4294967295 - m_start_time + cur_time;
      return cur_time - m_start_time;
    }
};

#define NUM_ROWS 3
uint8_t addresses[NUM_ROWS][2] = {
  {B1100000, B1100001},
  {B1100010, B1100011},
  {B1100100, B1100101},
};

Adafruit_GPS GPS(&Serial);
bool connected_to_gps = false;
SIGNAL(TIMER0_COMPA_vect) {
  char c = GPS.read();
}

// The vast majority of the time, I populate the frame buffer and immediately push its state
// out to the display. It's nice to have it, though, because it means that I can write stuff
// to it during debugging and expect it to display on the next push.
char display_strs[NUM_ROWS][16];

// display states;
//   0  standard time display
//   1  GPS location display
//   2  debug stats
//   3  Turn on all LEDs
//   4  # loop() runs, time since last GPS fix, millis(), millis()@last GPS receipt
//   5  debugging info for lost GPS comms
//   6  EEPROM state for local timezone offset storage.
uint8_t display_state;

// Button states. Each "down" state is set while the button is held down. "pressed"
// is only true when the button went down on the last refresh.
bool prev_down = false;
bool prev_pressed = false;
bool next_down = false;
bool next_pressed = false;
bool mode_down = false;
bool mode_pressed = false;

uint32_t loops = 0;
uint8_t current_tz_offset;

uint32_t sentences_received = 0;
uint32_t first_sentence_timestamp_ms = 0;

// Pressing the "mode" button switches between a number of displays.
const int8_t NUM_MODES = 6;
bool recent_tz_change = false;

Stopwatch tz_change_timer;

int writeMAX6955(char command, char data, uint8_t address)
{
  Wire.beginTransmission(address);
  Wire.write(command);
  Wire.write(data);
  int error = Wire.endTransmission();
}

#define max6955_reg_decodeMode      0x01
#define max6955_reg_globalIntensity 0x02
#define max6955_reg_scanLimit       0x03
#define max6955_reg_configuration   0x04
#define max6955_reg_displayTest     0x07
#define max6955_reg_digitType       0x0C
void initMAX6955()
{
  Wire.begin();
  for (int row = 0; row < NUM_ROWS; row++) {
    for (int controller = 0; controller < 2; controller++) {
      uint8_t address = addresses[row][controller];
      // ascii decoding for all digits;
      writeMAX6955(max6955_reg_decodeMode, 0xFF, address);
      // brightness: 0x00 =  1/16 (min on)  2.5 mA/seg;
      // 0x0F = 15/16 (max on) 37.5 mA/segment
      writeMAX6955(max6955_reg_globalIntensity, 0x0f, address);
      // active displays: 0x07 -> all;
      writeMAX6955(max6955_reg_scanLimit, 0x07, address);
      // set normal operation;
      writeMAX6955(max6955_reg_configuration, 0x01, address);
      // segments/display: 0xFF=14-seg; 0=16 or 7-seg;
      writeMAX6955(max6955_reg_digitType, 0xff, address);
      // display test on;
      writeMAX6955(max6955_reg_displayTest, 0x01, address);
    }
  }
  delay(1000);
  for (int row = 0; row < NUM_ROWS; row++) {
    for (int controller = 0; controller < 2; controller++) {
      uint8_t address = addresses[row][controller];
      // display test off;
      writeMAX6955(max6955_reg_displayTest, 0x00, address);
      memset(display_strs[0], ' ', 8);
      writeDisplay(display_strs[0], address);
    }
  }
}

void writeDisplay(char* msg, uint8_t address)
{
  for (int i = 0; i < 8; i++)
  {
    writeMAX6955(0x20 + i, msg[i], address);
  }
}

void writeRow(uint8_t row, char * write_str) {
  writeDisplay(write_str, addresses[row][0]);
  writeDisplay(write_str + 8, addresses[row][1]);
}

/*
void writeChar(byte pos, char letter, boolean dotLit, uint8_t address)
{
  writeMAX6955(0x20 + pos, (dotLit ? 0x80 : 0) | letter, address);
}
*/

bool receive_and_parse() {
  if (GPS.newNMEAreceived()) {
    connected_to_gps = true;
    GPS.parse(GPS.lastNMEA());
    return true;
  }
  return false;
}

void stall(int t, char c0, char c1, uint8_t row) {
  int frac = (t + 31) / 32;
  Stopwatch sw;
  char chars[2] = {c0, c1};
  for(int c = 0; c < 2; c++) {
    for(int i = 0; i < 16; i++) {
      sw.reset();
      while (sw.elapsed() < frac) {
        receive_and_parse();
      }
      display_strs[row][i] = chars[c];
      writeRow(row, display_strs[row]);
    }
  }
}

// strcpy, but no '\0' is written to the destination.
void initGPS() {
  OCR0A = 0xAF;
  TIMSK0 |= _BV(OCIE0A);

  // Give the GPS a few milliseconds to start up, in case it's slower than the arduino.
  bare_strcpy(display_strs[0], (char *)"Flux Cap Fluxing");
  writeRow(0, display_strs[0]);
  stall(500, '-', ' ', 1);

  // The GPS receiver defaults to 9600. We want to run it faster, but have to start at 9600
  // to tell it so.
  Serial.begin(9600);
  memset(display_strs[0]+10, ' ', 6);
  bare_strcpy(display_strs[0], (char *)" 9600 baud");
  writeRow(0, display_strs[0]);
  stall(1000, '-',  ' ', 1);

  GPS.sendCommand(PMTK_SET_BAUD_57600);
  memset(display_strs[0]+10, ' ', 6);
  bare_strcpy(display_strs[0], (char *)"57600 baud");
  writeRow(0, display_strs[0]);
  stall(1000, '-',  ' ', 1);

  memset(display_strs[0]+10, ' ', 6);
  bare_strcpy(display_strs[0], (char *)"wait 4 fix");
  writeRow(0, display_strs[0]);
  while(!connected_to_gps || !GPS.fix) {
    Serial.begin(57600);
    if(connected_to_gps) {
      bare_strcpy(display_strs[2], (char *)"Connected to GPS");
      writeRow(2, display_strs[2]);     
    } else if(millis() > 10.0) {
      bare_strcpy(display_strs[2], (char *)"Rear switch on? ");
      writeRow(2, display_strs[2]);
    }
    stall(500, '=',  ' ', 1);
    GPS.sendCommand(PMTK_SET_BAUD_57600);
    GPS.sendCommand(PMTK_SET_NMEA_OUTPUT_RMCONLY);
    GPS.sendCommand(PMTK_SET_NMEA_UPDATE_10HZ);
    stall(2000, '-',  ' ', 1);
  }
}
const uint8_t magic = 0x88;
uint8_t get_eeprom_tz_offset() {
  //Magic number check:
  if(EEPROM.read(510) == magic && EEPROM.read(511) == magic) {
    uint8_t addr = EEPROM.read(509);
    return(EEPROM.read(addr+1)); 
  } else {
    EEPROM.write(510, magic);
    EEPROM.write(511, magic);
    EEPROM.write(509, 0);     //offset to current tz info
    EEPROM.write(0, 0);       //zeroth write to this address pair.
    // This'll provide a rough guess as to which time zone the user is in, based on their longitude.
    uint8_t tz_offset = map(GPS.longitudeDegrees, -180.0, 180.0, 0, 97);
    // round to the nearest hour, with some bias for the high probability that they're on DST.
    tz_offset = ((tz_offset + 5)/4)*4;
    EEPROM.write(1, tz_offset);
    return tz_offset;
  }
}

// Spread the writes across 256 bytes of eeprom.
void set_eeprom_tz_offset(uint8_t offset) {
  uint8_t addr = EEPROM.read(509);
  uint8_t write_count = EEPROM.read(addr);
  if(write_count == 0xff) {
    addr = (addr + 2) % 256;
  }
  EEPROM.write(addr, write_count + 1);
  EEPROM.write(addr+1, offset);
}

// I understand this formula to be accurate to within 15s.
float solar_offset(const Datetime& current_utc) {
  float n;
  float days_in_year = 365.0;
  switch (current_utc.month) {
    case  1: n =   0; break;
    case  2: n =   31; break;
    case  3: n =   59; break;
    case  4: n =   90; break;
    case  5: n =   120; break;
    case  6: n =   151; break;
    case  7: n =   181; break;
    case  8: n =   212; break;
    case  9: n =   243; break;
    case  10: n =   273; break;
    case  11: n =   304; break;
    case  12: n =   334; break;
  }
  if (current_utc.month > 2 && !current_utc.leap_year()) {
    n += 1;
    days_in_year += 1;
  }
  n += current_utc.day;
  n += current_utc.hour / 24.0;
  n += current_utc.minute / (24.0 * 60.0);
  n += current_utc.seconds / (24.0 * 60.0 * 60.0);
  n += current_utc.milliseconds / (24.0 * 60.0 * 60.0 * 1000.0);
  //
  // DISCONTINUITY! Leap years will have a stutter point. (it's 2:15am. figure it out.)
  //
  float d = 2.0 * 3.141592653 * (n - 81.0) / days_in_year;
  return 9.873 * sin(2 * d) - 7.655 * sin(d + 1.374);
}

bool make_solar_time(const Datetime& current_utc, char * str) {
  // The GPS.longitude_fixed is in tenmillionths of a degree. To scale our longitude to a float:
  // GPS.longitude_fixed / 180.0     Scale to the 0.0-1.0 range
  //                     * 12        hours per hemisphere
  //                     * 3600      seconds per hour
  //                     * 1000      milliseconds per second
  //                     / 10^7      removed the fixed-point scaling.
  //                     = * 0.024   Do the divide first to avoid overflow
  int32_t displacement = GPS.longitude_fixed / 1000;
  displacement *= 24;
  // displacement is now the distance - in milliseconds - from 0 longitude

  // Earth's axial tilt and the eccentricity of its orbit cause the difference between solar noon
  // and mean noon to move around by as much as +- 13 minutes. Get the number of milliseconds of
  // difference between mean solar time and solar time:
  displacement += solar_offset(current_utc) * 60 * 1000;

  Datetime solar_time(current_utc);
  solar_time.milliseconds += displacement;
  solar_time.normalize();
  solar_time.make_time_str(str);
  return solar_time.milliseconds < 500;
}

// TODO: Get off of the float latitude and longitude and switch to using the fixed-point
//       version? I do use fixed, elsewhere.
float degmins_to_decimal(float degmins) {
  //Adafruit_GPS stores latitude and longitude in a format where everything to the left of
  //the decimal point is (degrees * 100) + minutes. Right of the decimal point is fraction
  //of minutes. We have to unpack this format to get an old-fashioned float. They probably
  //did it this way for the sake of getting the best precision out of a float that they
  //could.
  degmins /= 100.0;
  int degs = (int)degmins;
  float fract = degmins - degs;
  return degs + fract * 100.0 / 60.0;
}

void set_colons(int8_t pin, bool state) {
  if (state)
    digitalWrite(pin, HIGH);
  else
    digitalWrite(pin, LOW);
}

// For whatever tz offset the user has selected, we've stored it as a value
// between 0 and 97, inclusive. Convert it to an hour/minute offset. Note that
// a negative offset will return negative minutes. This is useful where we add
// these offsets to GPS hours and minutes before normalizing them.
void get_tz_offset(int8_t * hours, int8_t * minutes) {
  int8_t tz_offset_in_qtr_hours = int8_t(current_tz_offset) - 48;
  *hours = tz_offset_in_qtr_hours / 4;
  *minutes = (tz_offset_in_qtr_hours % 4) * 15;
}

// A low-memory way to print ints right-justified. (sprintf aint cheap in terms of
// CPU or memory. Just including it in your code is a huge progmem hit.)
// TODO: allow caller to choose between space-preamble and 0-preamble. You can't just change
//       the memset to do it. Negatives would come out "00-5" instead of "-005".
void rjust(int32_t val, char * buf, int width) {
  memset(buf, ' ', width);
  char tmp[12];
  ltoa(val, tmp, 10);
  int len = strlen(tmp);
  bare_strcpy(buf + width - len, tmp);
}

// Given the current state of the timezone offset, print it to a display row. For the moment,
// it will only print where the LED colons neatly line up (the last 7 digits) so you ge
// "-12:15:00".
void draw_tz_offset(int8_t row) {
  int8_t time_zone_offset_hours, time_zone_offset_minutes;
  get_tz_offset(&time_zone_offset_hours, &time_zone_offset_minutes);
  rjust(time_zone_offset_hours, display_strs[row]+9, 3);
  rjust(abs(time_zone_offset_minutes), display_strs[row]+12, 2);
  // TODO: have rjust take a bool that fills leading zeroes
  if(time_zone_offset_minutes == 0)
    display_strs[row][12] = '0';
  memset(display_strs[row]+14, '0', 2);
  if(time_zone_offset_hours == 0 && time_zone_offset_minutes < 0)
    display_strs[row][10] = '-';
  set_colons(11 + row, GPS.milliseconds < 500);
}

// The ftoa() stdlib function generates a decimal point as a character, e.g., "12.345".
// We don't have to waste a digit to print the column - we simply set the high bit of
// the charcter preceding the decimal point and the DP will light on that display.
// I can't get rid of the temp buffer and render the temp string in-place because the temp string
// is two characters (a '.' and a '\0') longer than the output. 
void ftoa_msb_decimal(float f, char * buf, int int_chars, int frac_chars) {
  char tmpbuf[18];
  char * tmpbufptr = tmpbuf;
  //render the float to a temporary buffer:
  dtostrf(f, int_chars + frac_chars + 1,  frac_chars, tmpbuf);
  while (*tmpbufptr) {
    if (*tmpbufptr == '.') {
      *(buf - 1) |= 0x80;
      tmpbufptr++;
    } else {
      *buf = *tmpbufptr;
      buf++;
      tmpbufptr++;
    }
  }
}

void setup()
{
#ifdef RUN_TESTS
  Serial.begin(19200);
  Datetime::run_tests();
  Serial.end();
  while(1);
#endif
  initMAX6955();

  initGPS();
  pinMode(11, OUTPUT);
  pinMode(12, OUTPUT);
  pinMode(13, OUTPUT);
  pinMode(5, INPUT_PULLUP);
  pinMode(6, INPUT_PULLUP);
  pinMode(7, INPUT_PULLUP);
  display_state = 0;

  current_tz_offset = get_eeprom_tz_offset();

}

void update_button(bool * down, bool * pressed, int pin) {
  *pressed = false;
  if (!digitalRead(pin)) { // if the button is down
    if (!(*down)) { // if it was up
      *pressed = true;
    }
    *down = true;
  } else {
    *down = false;
  }
}

Stopwatch button_repeat_timer;
void update_buttons(const Datetime& current_utc, bool gps_out) {
  bool hold_time_elapsed = false;
  update_button(&prev_down, &prev_pressed, 7);
  update_button(&next_down, &next_pressed, 6);
  update_button(&mode_down, &mode_pressed, 5);

  if(mode_pressed || prev_pressed || next_pressed) {
    button_repeat_timer.reset();
  } else if(button_repeat_timer.elapsed() > 200) {
    button_repeat_timer.reset();
    hold_time_elapsed = true;
  }
  if(mode_pressed || (hold_time_elapsed && mode_down)) {
    display_state = (display_state + 1) % NUM_MODES;
    draw_display_state(current_utc, gps_out);
  }
  if(display_state == 0) {
    if(prev_pressed || (hold_time_elapsed && prev_down)) {
      current_tz_offset = (current_tz_offset + 96) % 97;
      draw_display_state(current_utc, gps_out);
    }
    if(next_pressed || (hold_time_elapsed && next_down)) {
      current_tz_offset = (current_tz_offset + 1) % 97;
      draw_display_state(current_utc, gps_out);
    }
  }
}

void draw_time_strings(const Datetime & current_utc) {
  set_colons(11, current_utc.milliseconds % 1000 < 500);
  set_colons(12, current_utc.milliseconds % 1000 < 500);
  current_utc.make_time_str(display_strs[0]);

  Datetime current_local(current_utc);
  int8_t time_zone_offset_hours, time_zone_offset_minutes;
  get_tz_offset(&time_zone_offset_hours, &time_zone_offset_minutes);
  current_local.hour += time_zone_offset_hours;
  current_local.minute += time_zone_offset_minutes;
  current_local.normalize();
  
  current_local.make_time_str(display_strs[1]);

  display_strs[0][5] |= 0x80;
  display_strs[0][9] |= 0x80;
  display_strs[1][5] |= 0x80;
  display_strs[1][9] |= 0x80;

  if(recent_tz_change) {
    memset(display_strs[2], ' ', 8);
    bare_strcpy(display_strs[2]+6, (char *)"UTC");
    draw_tz_offset(2);
  } else {
    bool solar_led_state = make_solar_time(current_utc, display_strs[2]);
    set_colons(13, solar_led_state);
    display_strs[2][5] |= 0x80;
    display_strs[2][9] |= 0x80;
  }
}

void draw_location_string(float latlon, int row, char latlonchar) {
  memset(display_strs[row], ' ', 16);
#ifdef FILMING
  ftoa_msb_decimal(123.456789, display_strs[row] + 3, 3, 7);
#else
  ftoa_msb_decimal(degmins_to_decimal(latlon), display_strs[row] + 3, 3, 7);
#endif
  display_strs[row][5] |= 0x80;
  display_strs[row][13] = '`';
  display_strs[row][15] = latlonchar;
}

void draw_location_strings(const Datetime& current_utc) {
  memset(display_strs[0]+8, ' ', 8);
  bare_strcpy(display_strs[0], (char *)"solar dt");
  ftoa_msb_decimal(solar_offset(current_utc), display_strs[0] + 8, 3, 5);
  draw_location_string(GPS.latitude, 1, GPS.lat);
  draw_location_string(GPS.longitude, 2, GPS.lon);
  set_colons(11, false);
  set_colons(12, false);
  set_colons(13, false);
}

//TODO: rename debug methods
void draw_debug0() {
  rjust(loops, display_strs[0], 8);
  ftoa_msb_decimal(GPS.secondsSinceTime(), display_strs[0] + 8, 5, 3);
  rjust(millis(), display_strs[1], 16);
  bare_strcpy(display_strs[1], (char *)"now");
  float rate;
  if(sentences_received == 0)
    rate = 0.0;
  else {
    rate = (float)(millis() - first_sentence_timestamp_ms) / sentences_received;
  }
  ftoa_msb_decimal(rate, display_strs[2]+6, 4, 6);
  bare_strcpy(display_strs[2], (char *)"sntnce");
  set_colons(12, false);
  set_colons(12, false);
}

//TODO: move this back to a sane spot
//I have had processors that didn't tick at 1000 millis() per second, so I need to
//have a dynamic computation of the device's actual rate so we can (sorta) keep time
//when we lose a gps connection.
float arduino_ticks_per_second = 1000.0;

void draw_debug1() {
  set_colons(11, false);
  set_colons(12, false);
  set_colons(13, false);
  memset(display_strs[0], ' ', 24);
  ftoa_msb_decimal(arduino_ticks_per_second, display_strs[2], 4, 4);
}

void draw_debug2() {
  for(int row=0; row<3; row++)
    memset(display_strs[row], ' ', 16);
  bare_strcpy(display_strs[0], (char *)"magic");
  uint8_t data = EEPROM.read(510);
  display_strs[0][6] = data >> 4;
  display_strs[0][7] = data & 0xf;
  data = EEPROM.read(511);
  display_strs[0][8] = data >> 4;
  display_strs[0][9] = data & 0xf;
  display_strs[0][12] = 'A';
  display_strs[0][13] = 'D';
  data = EEPROM.read(509); 
  display_strs[0][14] = data >> 4;
  display_strs[0][15] = data & 0xf;

  bare_strcpy(display_strs[1], (char *)"UTC");
  rjust(current_tz_offset, display_strs[1]+4, 2);
  draw_tz_offset(1);

  bare_strcpy(display_strs[2], (char *)"writes/val");
  rjust(EEPROM.read(data), display_strs[2]+10, 2);
  rjust(EEPROM.read(data+1), display_strs[2]+14, 2);
}

void draw_xmas_tree() {
  for (int row = 0; row < 3; row++) {
    for (int col = 0; col < 16; col++) {
      display_strs[row][col] = B10100011;  // Everything on.st
    }
  }
  set_colons(11, false);
  set_colons(12, false);
  set_colons(13, false);
}

void draw_display_state(const Datetime& current_utc, bool gps_out) {
  bool time_draw_done = false;

  switch(display_state) {
    case 0:  draw_time_strings(current_utc);      break;
    case 1:  draw_location_strings(current_utc);  break;
    case 2:  draw_xmas_tree();                    break;
    case 3:  draw_debug0();                       break;
    case 4:  draw_debug1();                       break;
    case 5:  draw_debug2();                       break;
  }

  if(gps_out) {
    //Turn on the indicator whenever the GPS is out.
    display_strs[0][0] |= 0x80;
    //...but have it start blinking if it's a long outage:
    if(GPS.secondsSinceTime() > 1.0 && (millis() % 500) > 250) {
      display_strs[0][0] ^= 0x80;
    }
  }

  // TODO: do the blit only when something has changed?
  writeRow(0, display_strs[0]);
  writeRow(1, display_strs[1]);
  writeRow(2, display_strs[2]);

}

Stopwatch millis_timer;
bool gps_out = false;

void loop() {
  loops++;

  Datetime current_utc(GPS);
  update_buttons(current_utc, gps_out);

  if(display_state == 0 && (next_down or prev_down)) {
    recent_tz_change = true;
    tz_change_timer.reset();
  }
  if(recent_tz_change && tz_change_timer.elapsed() > 3000) {
    recent_tz_change = false;
    set_eeprom_tz_offset(current_tz_offset);
  }

  if(receive_and_parse()) {
    //uint32_t sentences_received;
    //uint32_t first_sentence_timestamp_ms = 0;
    if(sentences_received == 0)
      first_sentence_timestamp_ms = millis();
    sentences_received++;
  } 
  
  float seconds_since_gps_sentence = GPS.secondsSinceTime();
  if(seconds_since_gps_sentence > 0.250) {
    current_utc.milliseconds += seconds_since_gps_sentence * arduino_ticks_per_second;
    current_utc.normalize();
    gps_out = true;
  } else {
    gps_out = false;
  }

  draw_display_state(current_utc, gps_out);
}
