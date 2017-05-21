#include <ESP8266WiFi.h>
#include <WiFiClientSecure.h>
#include "quickparse.h"
#include <string.h>
#include <ctype.h>

#include <TFT_eSPI.h> // https://github.com/Bodmer/TFT_eSPI

#undef OLD_API // TODO: Api changeover around September 2017
#define LOCATION "TXZ159" // "TXZ159" 
#define LOCATION_NAME "McLennan" // "McLennan" // "TEST" // "TEST" // 
#define TIMEZONE -6*60
#define DST_ADJUST 1

#define DEBUGMSG(s) Serial.println((s))

#define memzero(p,n) memset((p),0,(n))

#define UNDEF_TIME 0xFFFFFFFFu
#define DEBOUNCE_TIME 50

// Display SDO/MISO  // to NodeMCU pin D6 (or leave disconnected if not reading TFT)
// Display LED       to NodeMCU pin VIN (or 5V, see below)
// Display SCK       to NodeMCU pin D5
// Display SDI/MOSI  to NodeMCU pin D7
// Display DC (RS/AO)to NodeMCU pin D1 // GPIO5
// Display RESET     to +3.3V // was: to NodeMCU pin D4 (or RST, see below)
// Display CS        to NodeMCU pin D8 (or GND, see below)
// Display GND       to NodeMCU pin GND (0V)
// Display VCC       to NodeMCU 5V or 3.3V
//#define TFT_CS   PIN_D8  // Chip select control pin D8
//#define TFT_DC   PIN_D1  // Data Command control pin

const uint8_t beeperPin = 12; // D6 // 2; // D4
const uint8_t buttonPin = 0; // D3
const uint8_t ledPin = 16; // D0
const uint8_t backlightPin = 4; // D2
const uint8_t ledReverse = 1; 
uint8_t noConnectionWarningLight = 0;
const int beeperTones[][2] = { { 800, 500 }, {0, 700} };
const uint32_t delayTime = 120000ul; // in milliseconds
const uint32_t informationUpdateDelay = 1000;
const uint32_t screenOffDelay = 30000ul;
const uint32_t noConnectionWarnDelay = 60ul*60000ul; // warn after an hour of failure to connect
uint32_t noConnectionWarnTimer = UNDEF_TIME;
uint32_t toneStart = UNDEF_TIME;
uint32_t lastUpdate = UNDEF_TIME;
uint32_t lastUpdateSuccess = UNDEF_TIME;
uint32_t curUpdateDelay = delayTime;
uint32_t lastButtonDown = UNDEF_TIME;
uint32_t lastButtonUp = UNDEF_TIME;
uint32_t screenOffTimer = UNDEF_TIME;
uint8_t buttonState = 0;
uint8_t backlightState = 0;
uint8_t updateFailed = 0;
int beeperState;
#define screenHeight 128
#define screenWidth  160
#define dataFontLineHeight 8
#define dataFontCharWidth 6
#define statusFontLineHeight 8
#define statusFontCharWidth 6
#define numDataLines ((screenHeight-2*statusFontLineHeight)/dataFontLineHeight)
uint32_t lastInformationUpdate = UNDEF_TIME;

#define COLOR565(r,g,b) ( (((r)>>11)<<11) | ( ((g)>>10)<<5 ) | ( (b)>>11 ) )

int dataColors[2][2] = { { TFT_BLACK, TFT_WHITE }, { TFT_YELLOW, TFT_BLACK } };
int statusColors[2] = { TFT_BLUE, TFT_WHITE };
uint8_t informLight = 0;

#define MAX_CHARS_PER_LINE (screenWidth / (dataFontCharWidth<statusFontCharWidth ? dataFontCharWidth : statusFontCharWidth))
char dataLines[numDataLines+2][MAX_CHARS_PER_LINE+1];
#define STATUS_LINE1 numDataLines
#define STATUS_LINE2 (numDataLines+1)

TFT_eSPI tft = TFT_eSPI();

#define BEEPER_OFF -1

#define RETRY_TIME 25000

#define INFORM_LIGHT 1
#define INFORM_SOUND 2

#define SSID_LENGTH 33
#define PSK_LENGTH  64
#if 1
#include "c:/users/alexander_pruss/Documents/Arduino/private.h"
#else
char ssid[SSID_LENGTH] = "SSID";
char psk[PSK_LENGTH] = "password";
#endif

#define MAX_EVENTS 20

//#define MAX_SUMMARY 450
#define MAX_EVENT 64
#define ID_SIZE 22

typedef struct {
  char id[ID_SIZE+1]; 
  char event[MAX_EVENT+1]; // empty for empty event
//  char summary[MAX_SUMMARY+1]; 
  //char effective[26]; 
  time_t expires;
  uint8_t needInform;
  uint8_t didInform;
  uint8_t severity;
  uint8_t fresh;
} EventInfo;

static char tagBuffer[20];
static char dataBuffer[MAX_EVENT+1]; // [MAX_SUMMARY+1];
static void XML_callback( char* tagName, char* data, XMLEvent event);
static const char* severityList[] = { "extreme", "severe", "moderate", "minor", "unknown" };
#define ARRAY_LEN(x) (sizeof((x))/sizeof(*(x)))

EventInfo events[MAX_EVENTS];
EventInfo curEvent;
int numEvents = 0;

void clearScreen() {
  tft.fillRect(0,0,screenWidth,screenHeight-2*statusFontLineHeight,dataColors[informLight][0]);
  tft.fillRect(0,screenHeight-2*statusFontLineHeight,screenWidth,2*statusFontLineHeight,statusColors[0]);
  for (int i=0; i<sizeof(dataLines)/sizeof(*dataLines); i++) {
    dataLines[i][0] = 0;
  }
}

void setup() {
  Serial.begin(115200);

  numEvents = 0;
  beeperState = BEEPER_OFF;
  lastUpdate = UNDEF_TIME;
  curUpdateDelay = delayTime;

  pinMode(buttonPin, INPUT_PULLUP);
  pinMode(beeperPin, OUTPUT);
  pinMode(ledPin, OUTPUT);
  pinMode(backlightPin, OUTPUT);
  digitalWrite(ledPin, ledReverse);
  tft.begin();
  
  tft.setRotation(1);
  tft.fillScreen(statusColors[0]);
  tft.setTextColor(statusColors[1]);
  tft.setCursor(0,0);
  tft.print("WeatherWarning for ESP8266");
  digitalWrite(backlightPin, HIGH);
  backlightState = 1;
  
  WiFi.begin(ssid, psk);

  while (WiFi.status() != WL_CONNECTED)
    delay(500);

  tft.setCursor(0,dataFontLineHeight);
  tft.print(String("Connected to ")+String(ssid));
  delay(4000);
  clearScreen();
  screenOffTimer = millis();

  DEBUGMSG("Hello!");
}

static char abridged[MAX_CHARS_PER_LINE+1];

void abridge(char* out, const char* in) {
  // TODO: font!
  if (strlen(in) > MAX_CHARS_PER_LINE) {
    strncpy(out, in, MAX_CHARS_PER_LINE-3);
    strcpy(out+MAX_CHARS_PER_LINE-3, "...");
  }
  else {
    strcpy(out, in);
  }
}

void writeText(int lineNumber, const char* data) {
  if (lineNumber == STATUS_LINE1) {
    tft.setCursor(screenWidth-statusFontCharWidth*strlen(data), screenHeight-2*statusFontLineHeight);
  }
  else if (lineNumber<STATUS_LINE1 && lineNumber%2) {
    tft.setCursor(screenWidth-dataFontCharWidth*strlen(data), lineNumber*dataFontLineHeight);
  }
  else {
    tft.setCursor(0, lineNumber*dataFontLineHeight);
  }
  tft.print(data);
}

void eraseText(int lineNumber, const char* data) {
  if (lineNumber>=STATUS_LINE1) 
    tft.setTextColor( statusColors[0] );
  else
    tft.setTextColor( dataColors[informLight][0] );
  writeText(lineNumber, data);
}

void drawText(int lineNumber, const char* data) {
  if (lineNumber>=STATUS_LINE1) 
    tft.setTextColor( statusColors[1] );
  else
    tft.setTextColor( dataColors[informLight][1] );
  writeText(lineNumber, data);
}

void displayLine(int lineNumber, const char* data) {
  char* current = dataLines[lineNumber];
  abridge(abridged, data);
  if (0==strcmp(abridged, current)) { // TODO: font
    return;
  }
  eraseText(lineNumber, current);
  drawText(lineNumber, abridged);
  strcpy(current, abridged);
}

uint8_t inEntry;
uint8_t inFeed;
uint8_t gotFeed;
uint8_t haveId;
uint8_t tooMany;

void XML_reset() {
  inFeed = 0;
  inEntry = 0;
  gotFeed = 0;
  tooMany = 0;
  for (int i=0; i<numEvents; i++)
    events[i].fresh = 0;
  xmlParseInit(XML_callback, tagBuffer, sizeof(tagBuffer), dataBuffer, sizeof(dataBuffer));
}

void deleteEvent(int i) {
  events[i].event[0] = 0;
  i++;
  for (i = i; i<numEvents; i++)
    events[i-1] = events[i];
  numEvents--;
}

void storeEvent(int i) {
  events[i] = curEvent;
  events[i].didInform = 0;
  events[i].fresh = 1;
}

void storeEventIfNeeded() {
  DEBUGMSG(curEvent.event);
  
  if (curEvent.event[0] == 0)
    return;
    
  if (NULL != strstr(curEvent.event, "tornado") || curEvent.severity == 0)
    curEvent.needInform = INFORM_LIGHT | INFORM_SOUND;
  else if (curEvent.severity == 1 || curEvent.severity == ARRAY_LEN(severityList) - 1 )
    curEvent.needInform = INFORM_LIGHT;
  else
    curEvent.needInform = 0;
  
  for (int i=0; i<numEvents; i++) {
    if (0==memcmp(events[i].id, curEvent.id, ID_SIZE)) {
      curEvent.didInform = events[i].didInform;
      storeEvent(i);
      return;
    }
  }

  DEBUGMSG("adding");
  if (numEvents < MAX_EVENTS) {
    storeEvent(numEvents);
    numEvents++;
  }
  else {
    tooMany = 1;
    deleteEvent(0);
    storeEvent(MAX_EVENTS-1);
    numEvents = MAX_EVENTS;
  }
}

static void XML_callback( char* tagName, char* data, XMLEvent event) {
  if (event == XML_START_TAG && 0==strcmp(tagName, "feed")) {
    DEBUGMSG("Start feed");
    inFeed = 1;
  }
  else if (inFeed) {
    if (event == XML_END_TAG && 0==strcmp(tagName, "feed")) {
      DEBUGMSG("End feed");
      inFeed = 0;
      gotFeed = 1;
    }

    if (event == XML_START_TAG && 0==strcmp(tagName, "entry")) {
      inEntry = 1;
      haveId = 0;
      memzero(&curEvent, sizeof(EventInfo));
      DEBUGMSG("Start entry");
    }
    else if (inEntry) {
      if (event == XML_END_TAG && 0==strcmp(tagName, "entry")) {
        if (haveId) 
          storeEventIfNeeded();
        inEntry = 0;
        DEBUGMSG("End entry");
      }
      else if (event == XML_TAG_TEXT && 0==strcmp(tagName, "id")) {
        int len = strlen(data);
        if (len >= ID_SIZE) {
          strcpy(curEvent.id, data+len-ID_SIZE);
        }
        else {
          strcpy(curEvent.id, data);
          memset(curEvent.id+len, 0, ID_SIZE-len);
        }
        DEBUGMSG(curEvent.id);
        haveId = 1;
      }
/*      else if (event == XML_TAG_TEXT && 0==strcmp(tagName, "summary")) {
        strncpy(curEvent.summary, data, MAX_SUMMARY);
        DEBUGMSG("Summary: ");
        DEBUGMSG(curEvent.summary);
      } */
      else if (event == XML_TAG_TEXT && 0==strcmp(tagName, "cap:event")) {
        for (int i=0; i<MAX_EVENT && data[i]; i++)
          curEvent.event[i] = tolower(data[i]);
        DEBUGMSG(String("Event: ")+String(curEvent.event));
      }
      else if (event == XML_TAG_TEXT && 0==strcmp(tagName, "cap:expires")) {
        DEBUGMSG(String("Expires: ")+data);
        curEvent.expires = nwsToUTC(data);
        DEBUGMSG(String("Expires: ")+String( formatTime(curEvent.expires, TIMEZONE, DST_ADJUST) ));
      }
      else if (event == XML_TAG_TEXT && 0==strcmp(tagName, "cap:severity")) {
        int i;
        for(i=0; i<ARRAY_LEN(severityList)-1; i++) // last one is "unknown" 
          if(0==strcasecmp(data, severityList[i]))
            break;
        curEvent.severity = i;
        DEBUGMSG(String("Severity: ")+String(severityList[i])+" "+String(curEvent.severity));
      }
    }
/*    if (inEntry && (statusflags & STATUS_TAG_TEXT) && 0==strcmp(tagName, "/http:/entry/cap:severity")) {
      DEBUGMSG("Severity: ");
      DEBUGMSG(data);
    } */
  }
  /*
  if (statusflags&STATUS_END_TAG) {
    DEBUGMSG("End tag: ");
    DEBUGMSG(tagName);
  }
  if (statusflags&STATUS_TAG_TEXT) {
    DEBUGMSG("Tag: ");
    DEBUGMSG(tagName);
    DEBUGMSG("Data: ");
    DEBUGMSG(data);
  }
  if (statusflags&STATUS_ATTR_TEXT) {
    DEBUGMSG("Attr: ");
    DEBUGMSG(tagName);
    DEBUGMSG("Data: ");
    DEBUGMSG(data);
  } */
}

void updateBeeper() {
  if (beeperState == BEEPER_OFF)
    return;
    
  if (toneStart != UNDEF_TIME && millis() < toneStart + beeperTones[beeperState][1])
    return;
  
  if (toneStart != UNDEF_TIME) {
    beeperState = (beeperState+1) % (sizeof(beeperTones) / sizeof(*beeperTones));
    toneStart = UNDEF_TIME;
  }
  
    int pitch = beeperTones[beeperState][0];
    if (pitch>0) {
      //tone(beeperPin, pitch);
      analogWriteFreq(pitch);
      analogWrite(beeperPin, 128);
    }
    else {
      analogWrite(beeperPin, 0);
      //noTone(beeperPin);
    }
    toneStart = millis();
}

void startBeeper() {
  if (beeperState == BEEPER_OFF) {
    toneStart = -1;
    beeperState = 0;
    updateBeeper();
  }
}

void stopBeeper() {
  if (beeperState != BEEPER_OFF) {
    beeperState = BEEPER_OFF;
    //noTone(beeperPin);
    analogWrite(beeperPin, 0);
  }
}

char durationBuf[20];

char* formatDuration(uint32_t t) {
  if (t < 1000ul*60*3) {
    snprintf(durationBuf, 19, "%lds", (t/1000));
  }
  else if (t < 1000ul*60*180) {
    snprintf(durationBuf, 19, "%ldmin", (t/(1000ul*60)));
  }
  else {
    snprintf(durationBuf, 19, "%ldhrs", (t/(1000ul*60*60)));
  }
  return durationBuf;
}

char buf[MAX_CHARS_PER_LINE+1];

void updateInformation() {
  uint8_t informNeeded = 0;
  for (int i=0; i<numEvents; i++)
    informNeeded |= events[i].needInform & ~events[i].didInform;

  if (noConnectionWarningLight)
    informNeeded |= INFORM_LIGHT;

  if (informNeeded & INFORM_LIGHT) {
    if (!informLight) {
      informLight = 1;
      clearScreen();
    }
    backlightState = 1;
    digitalWrite(backlightPin, HIGH);
  }
  else {
    if (informLight) {
      informLight = 0;
      screenOffTimer = millis();
      clearScreen();
    }
  }
  
  if (informNeeded & INFORM_SOUND) {
    startBeeper();
  }
  else {
    stopBeeper();
  }
  
  digitalWrite(ledPin, ledReverse^(numEvents>0 || informLight));
  
  if (numEvents > numDataLines/2) {
    snprintf(buf, MAX_CHARS_PER_LINE, "+ %d more events", numEvents-numDataLines/2);
    displayLine(STATUS_LINE1, buf);
  }
  else {
    displayLine(STATUS_LINE1, "");
  }

  if (lastUpdateSuccess == UNDEF_TIME && ! noConnectionWarningLight) {
    displayLine(STATUS_LINE2, "No data received yet.");
  }
  else {
    uint32_t delta = millis()-lastUpdateSuccess;
    const char *fmt = noConnectionWarningLight ? "No connect in %s" : LOCATION_NAME " %s ago";
    snprintf(buf, MAX_CHARS_PER_LINE, fmt, formatDuration(delta));
    displayLine(STATUS_LINE2, buf);
  }

  for (int i=0; 2*i < numDataLines; i++) {
    if (i<numEvents) {
      displayLine(2*i, events[i].event);
      if (0 != events[i].expires) {
        snprintf(buf, MAX_CHARS_PER_LINE+1, "expires %s", formatTime(events[i].expires, TIMEZONE, DST_ADJUST));
        displayLine(2*i+1, buf);
      }
    }
    else {
      displayLine(2*i, i==0 ? (updateFailed ? "Update failed: Will retry." : "No weather warnings." ) : "");
      displayLine(2*i+1, "");
    }
  }

  lastInformationUpdate = millis();
}

int eventCompare(const EventInfo* a, const EventInfo* b) {
  return (int) a->severity - (int) b->severity;
}

void sortEvents() {
  // https://en.wikipedia.org/wiki/Bubble_sort
  int n = numEvents;
  do {
    int newN = 0;
    for (int i=1; i<n; i++) {
      if (eventCompare(events+i-1,events+i)>0) {
        EventInfo t = events[i-1];
        events[i-1] = events[i];
        events[i] = t;
        newN = i;
      }
    }
    n = newN;
  } while(n>0);
}

void failureUpdate() {
  if (updateFailed) {
    if (noConnectionWarnTimer == UNDEF_TIME) {
      noConnectionWarnTimer = millis();
    }
    else if ((uint32_t)(millis()-noConnectionWarnTimer) >= noConnectionWarnDelay) {
      noConnectionWarningLight = 1;
    }
  }  
}

void monitorWeather() {
  WiFiClientSecure client;
  displayLine(STATUS_LINE2, "Connecting...");
#ifdef OLD_API  
    if (client.connect("alerts.weather.gov", 443)) { // TXZ159=McLennan; AZZ015=Flagstaff, AZ
    client.print("GET /cap/wwaatmget.php?x=" LOCATION " HTTP/1.1\r\n"
      "Host: alerts.weather.gov\r\n"
      "User-Agent: weatherwarning-ESP8266\r\n"
      "Connection: close\r\n\r\n");  
#else
    if (client.connect("api.weather.gov", 443)) { // TXZ159=McLennan; AZZ015=Flagstaff, AZ
    client.print("GET /alerts/active?zone=" LOCATION " HTTP/1.1\r\n"
      "Host: api.weather.gov\r\n"
      "User-Agent: weatherwarning-ESP8266-arpruss@gmail.com\r\n"
      "Accept: application/atom+xml\r\n"
      "Connection: close\r\n\r\n");  // TODO: alerts-v2??? */
#endif      
    displayLine(STATUS_LINE2, "Loading...");
    XML_reset();
    while (client.connected()) {
      String line = client.readStringUntil('\n');
      DEBUGMSG(line);
      if (String("\r") == line || String("") == line) // headers done
        break;
    }
    while (client.connected()) {
      if (client.available()) {
        char c = client.read();
        xmlParseChar(c);
        //Serial.write(c);
      }
    }
    client.stop();
    updateBeeper();
    yield();

    if (gotFeed) {
      for (int i=numEvents-1; i>=0; i--) {
        if (!events[i].fresh)
          deleteEvent(i);
      }
      DEBUGMSG("Successful read of feed");
      DEBUGMSG(String("Have ") + String(numEvents) + " events");
      updateFailed = 0;
      lastUpdateSuccess = millis();
      curUpdateDelay = delayTime;
    }
    else {
      DEBUGMSG("Incomplete");
      curUpdateDelay = RETRY_TIME;
      updateFailed = 1;
    }

  }
  else {
    DEBUGMSG("Connection failed");
    curUpdateDelay = RETRY_TIME;
    updateFailed = 1;
  }
  
  if (numEvents>1) {
    // there are so few events that we'll just do a bubble sort
    sortEvents();
  }

  lastUpdate = millis();
  
  updateInformation();

  failureUpdate();
}

void handlePressed() {
  if (!backlightState) {
    backlightState = 1;
    digitalWrite(backlightPin, HIGH);
  }
  if (beeperState != BEEPER_OFF) {
    for(int i=0; i<numEvents; i++)
      events[i].didInform |= INFORM_SOUND;
    stopBeeper();
  }
  if (informLight) {
    for(int i=0; i<numEvents; i++)
      events[i].didInform |= INFORM_LIGHT;
    informLight = 0;
    noConnectionWarningLight = 0;
    clearScreen();
    updateInformation();
  }
  noConnectionWarnTimer = UNDEF_TIME;
  screenOffTimer = millis();
}

void handleButton() {
  if (LOW == digitalRead(buttonPin)) {
    if (!buttonState) {
       buttonState = 1;
       handlePressed();
    }  
    lastButtonDown = millis();
    if ((uint32_t)(lastButtonDown - lastButtonUp) >= 3*1000ul) {
      startBeeper();
      while ((uint32_t)(millis() - lastButtonDown) < 10*1000ul) {
        yield();
        updateBeeper();
      }
      stopBeeper();
    }
  }
  else {
    lastButtonUp = millis();
    if (buttonState && millis() >= lastButtonDown + DEBOUNCE_TIME) {
      buttonState = 0;
    }
  }
}

void loop() {
  handleButton();
  yield();
  if (lastUpdate == UNDEF_TIME || (uint32_t)(millis() - lastUpdate) >= curUpdateDelay) {
    monitorWeather();
  }
  yield();
  updateBeeper();
  yield();
  if (!informLight && backlightState && (uint32_t)(millis() - screenOffTimer) >= screenOffDelay) {
    backlightState = 0;
    digitalWrite(backlightPin, LOW);
  }
  failureUpdate();
  if ((uint32_t)(millis() - lastInformationUpdate) >= informationUpdateDelay) {
    updateInformation();
  }
  yield();
}

