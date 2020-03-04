#ifndef _TC_UTIL_H
#define _TC_UTIL_H

#define MARK do{Serial.println(__LINE__);}while(0)
//#define RUN_TESTS

void bare_strcpy(char *dst, char*src);

#endif //_TC_UTIL_H
