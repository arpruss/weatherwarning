#include <Time.h>
#include <TimeLib.h>

const char* weekdays[7] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
time_t nwsToUTC(char* t) {
  int years, months, days, hr, minutes, sec, tzSign, tzHour, tzMin;
//          1         2
//0123456789012345678901234
//YYYY-MM-DDTHH:MM:SS+HH:MM
  years = atoi(t);
  months = atoi(t+5);
  days = atoi(t+8);
  hr = atoi(t+11);
  minutes = atoi(t+14);
  sec = atoi(t+17);
  tzSign = t[19];
  tzHour = atoi(t+20);
  tzMin = atoi(t+23);
//  sscanf(t, "%04d-%02d-%02dT%02d:%02d:%02d%c%02d:%02d", &year, &months, &days, &hr, &minutes, &sec, &tzSign, &tzHour, &tzMin);
  tmElements_t el;
  el.Second = sec;
  el.Minute = minutes;
  el.Hour = hr;
  el.Day = days;
  el.Month = months;
  el.Year = years - 1970;
  time_t t1 = makeTime(el) - ( (tzSign == '-') ? -1 : 1 ) * (60 * tzHour + tzMin) * 60;
  Serial.println(String(t1));
  Serial.println(String(year(t1)));
  Serial.println(String(month(t1)));
  Serial.println(String(day(t1)));
  return t1;
}

// adjust hour for DST if needed
// US+Canada only
// Political message: Let's abolish DST, or move to DST all year round!
time_t adjustDST(time_t t) {
  uint8_t m = month(t);
  uint8_t h = hour(t);
  char dst = 0;
  if (m == 3) {
    // check for March spring-forward
    // spring forward at 3 am second Sunday in March
    int8_t w = weekday(t);
    int8_t d = day(t);
    if (w == 0) {
      // it's Sunday
      if (d > 14) {
        dst = 1; // three or more Sundays have passed
      }
      else if (d > 7) {
        // it's that pesky second Sunday
        
        if (h >= 3)
          dst = 1; // after 3 am ST
      }
    }
    else if (d - w > 7) {
      dst = 1; // two or more Sundays have passed
    }
  } else if (m == 11) {
    // check for November fall-back
    dst = 1;
    int8_t w = weekday(t);
    int8_t d = day(t);
    if (w == 0) {
      if (d > 7) {
        dst = 0; // two or more Sundays have passed
      }
      else {
        // that first Sunday
        if (h >= 1)
          dst = 0; // after 1 am ST
      }
    } else if (d - w >= 1) {
      // it's after the first Sunday
      dst = 0;
    }
  }
  else if (3 < m && m < 11) {
    dst = 1;
  }
  return dst ? t + 60*60 : t;
}

char timeBuffer[18];

void format2Digit(char*s, int x) {
  s[0] = (x/10)%10 + '0';
  s[1] = x%10 +'0';
  s[2] = 0;
}

char* formatTime(time_t t, int timeZoneInMinutes, uint8_t dstAdjust) {
  time_t localT = t + timeZoneInMinutes*60;
  if (dstAdjust) 
    localT = adjustDST(localT);
  int h = hour(localT);
  char amPm = 'a';
  if (h >= 12) {
    amPm = 'p';
    if (h > 12)
      h -= 12;
  }
  else {
    if (h == 0)
      h = 12;
  }

  char dayBuf[3];
  char minuteBuf[3];
  format2Digit(dayBuf, day(localT));
  format2Digit(minuteBuf, minute(localT)); 
  snprintf(timeBuffer, 18, "%s %d/%s %d:%s%cm", weekdays[weekday(localT)-1], month(localT), dayBuf, h, minuteBuf, amPm);
  return timeBuffer;
}

