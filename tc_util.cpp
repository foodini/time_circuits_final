#include "Arduino.h"
#include "tc_util.h"

void bare_strcpy(char *dst, char*src) {
  while (*src != '\0') {
    *dst = *src;
    dst++;
    src++;
  }
}
