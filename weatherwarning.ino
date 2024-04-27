#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>
#include "quickparse.h"
#include <string.h>
#include <ctype.h>

// notes from June 17-18, 2023:
//   to connect to NWS servers, need a newish core, say 2.7.4 or later

#define ANALOGWRITE_BEEPER
#define ANALOGWRITE_BITS 8 // as of 3.0.0; before that, was 10

// code review April 12, 2020:
//   checked for(;;) loops for timely termination
//   checked while loops for timely termination

#include <TFT_eSPI.h> // https://github.com/Bodmer/TFT_eSPI , with a bit of local change

IPAddress staticIP(192, 168, 1, 225); //ESP static ip
IPAddress gateway(192, 168, 1, 1);   //IP Address of your WiFi Router (Gateway)
IPAddress subnet(255, 255, 255, 0);  //Subnet mask
IPAddress dns(8,8,8,8);
IPAddress dns2(8,8,4,4);

#define DELETE_CHILD_ABDUCTION // weather only!
#define UPGRADE_TORNADO_EVENTS
#define DOWNGRADE_HEAT_EVENTS
#define DOWNGRADE_FLOOD_WATCH
#define DEBUG
#undef OLD_API // TODO: Api changeover around September 2017? Still seems to work in April 2020, though.
                 
#undef ALT_ADDRESS //"192.168.1.204" 
#undef ALT_PORT // 8000
#define READ_TIMEOUT 110000  // milliseconds : should be 10000: TODO
#define FAILURE_REBOOT_MILLIS (1000ul * 60 * 30) // 30 minutes
#define LOCATION "TXC309" // "TXZ159" //"TXZ159" // "TXZ159" 
#define LOCATION_NAME "McLennan" // "McLennan" // "TEST" // "TEST" // 
#define TIMEZONE -6*60
#define DST_ADJUST 1

#ifndef DEBUG
# define DEBUGMSG(s) 
# define DEBUGMSG_CHAR(c)
#else
# define DEBUGMSG(s) Serial.println((s))
# define DEBUGMSG_CHAR(c) Serial.write(c)
#endif

#define memzero(p,n) memset((p),0,(n))

#define UNDEF_TIME 0xFFFFFFFFu
#define DEBOUNCE_TIME 50

// Display pins:
// Display SDO/MISO  unconnected
// Display LED       NodeMCU pin VIN (or 5V, see below)
// Display SCK       NodeMCU pin D5
// Display SDI/MOSI  NodeMCU pin D7
// Display DC (RS/AO)NodeMCU pin D1 
// Display RESET     NodeMCU pin D4
// Display CS        D8
// Display GND       NodeMCU pin GND (0V)
// Display VCC       NodeMCU 5V or 3.3V
//#define TFT_CS   PIN_D8  // Chip select control pin D8
//#define TFT_DC   PIN_D1  // Data Command control pin

const uint8_t beeperPin = 12; // D6 
const uint8_t buttonPin = 0; // D3
const uint8_t ledPin = 16; // D0
const uint8_t backlightPin = 4; // D2
const uint8_t powerLedPin = 2;
const uint16_t backlightBright = 512; // 1023 max
const uint16_t backlightDim = 10; // 1023 max

const uint8_t ledReverse = 1; 
const int beeperTones[][2] = { { 800, 500 }, {0, 700} };
const uint32_t informationUpdateDelay = 1000;
const uint32_t screenOffDelay = 30000ul;
const uint32_t retryDelays[] = { 120000ul, 25000ul, 25000ul, 60000ul, 120000ul }; // retryDelays[n] is how to long to wait if this is the nth retry; retryDelays[0] is the default wait period
#define NUM_RETRY_DELAYS (sizeof retryDelays / sizeof *retryDelays)
const uint32_t warnRetryCount = 10; // warn after 10 retries

uint32_t retry = 0;
uint8_t noConnectionWarningLight = 0;
uint32_t toneStart = UNDEF_TIME;
uint32_t lastUpdate = UNDEF_TIME;
uint32_t lastUpdateSuccess = UNDEF_TIME;
uint32_t lastButtonDown = UNDEF_TIME;
uint32_t lastButtonUp = UNDEF_TIME;
uint32_t screenOffTimer = UNDEF_TIME;
uint8_t buttonState = 0;
uint8_t backlightState = 0;
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
//WiFiDisplayClass tft;

#define BEEPER_OFF -1

#define INFORM_LIGHT 1
#define INFORM_SOUND 2

#define SSID_LENGTH 33
#define PSK_LENGTH  64
#if 1
#include "c:/users/alexander_pruss/Arduino/private.h"
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

void backlight(char state) {
  if (state != backlightState) {
    analogWrite(backlightPin, state ? backlightBright : backlightDim);
    backlightState = state;  
  }
}

void setup() {
#ifdef DEBUG
  Serial.begin(115200);
#endif

  numEvents = 0;
  beeperState = BEEPER_OFF;
  lastUpdate = UNDEF_TIME;
  retry = 0;

  pinMode(ledPin, OUTPUT);
  digitalWrite(ledPin, ledReverse);
  pinMode(buttonPin, INPUT_PULLUP);
  pinMode(beeperPin, OUTPUT);
  pinMode(backlightPin, OUTPUT);
  digitalWrite(backlightPin, 0);

  //delay(1500);

  backlightState = 0;
  backlight(1);

  tft.begin();
  //tft.coordinates(screenHeight,screenWidth);
  tft.setRotation(1);
  tft.fillScreen(statusColors[0]);
  tft.setTextColor(statusColors[1]);
  tft.setCursor(0,0);
  tft.print("WeatherWarning for ESP8266");

  tft.setCursor(0,dataFontLineHeight);
  WiFi.config(staticIP, gateway, subnet, dns, dns2);
  WiFi.begin(ssid, psk);
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);    
  }
  tft.print(String("Connected to ")+String(ssid));
  delay(1000);
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

uint8_t match(EventInfo& e1) {
  if(0==memcmp(e1.id, curEvent.id, ID_SIZE))
    return 1;
  if(0==strcmp(e1.event, curEvent.event) && e1.severity == curEvent.severity)
    return 1;
  return 0;
}

void storeEvent(int i) {
  if (match(events[i]) && events[i].fresh) {
    if (events[i].expires > curEvent.expires)
      curEvent.expires = events[i].expires;
    curEvent.needInform |= events[i].needInform;
    curEvent.didInform |= events[i].didInform;
  }
  
  events[i] = curEvent;
  events[i].fresh = 1;
}

void storeEventIfNeeded() {
  DEBUGMSG(curEvent.event);
  
  if (curEvent.event[0] == 0)
    return;

  curEvent.didInform = 0;

#ifdef DELETE_CHILD_ABDUCTION  
  if (!strcmp(curEvent.event, "child abduction emergency"))
    return;
#endif    
    
  if (
#ifdef UPGRADE_TORNADO_EVENTS    
      NULL != strstr(curEvent.event, "tornado") || 
#endif      
      curEvent.severity == 0)
    curEvent.needInform = INFORM_LIGHT | INFORM_SOUND;
  else if (NULL == strstr(curEvent.event, "child abduction") && ( curEvent.severity == 1 || curEvent.severity == ARRAY_LEN(severityList) - 1 ) 
#ifdef DOWNGRADE_HEAT_EVENTS  
      && (NULL == strstr(curEvent.event, "excessive heat")) 
#endif      
#ifdef DOWNGRADE_FLOOD_WATCH
      && (NULL == strstr(curEvent.event, "flood watch")) 
#endif      
      )
    curEvent.needInform = INFORM_LIGHT; 
  else
    curEvent.needInform = 0;
  
  for (int i=0; i<numEvents; i++) {
    if (match(events[i])) {
      curEvent.didInform = events[i].didInform;
      storeEvent(i);
      DEBUGMSG(String("match ")+i+" didInform "+events[i].didInform);
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
        DEBUGMSG(String("ID:")+curEvent.id);
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
#ifdef ANALOGWRITE_BEEPER      
      analogWriteFreq(pitch);
      analogWrite(beeperPin, 1<<(ANALOGWRITE_BITS-1));
#else      
      tone(beeperPin, pitch);
#endif      
      DEBUGMSG("beeper on");
    }
    else {
#ifdef ANALOGWRITE_BEEPER      
      analogWrite(beeperPin, 0);
#else      
      noTone(beeperPin);
#endif     
      DEBUGMSG("beeper off");
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
#ifdef ANALOGWRITE_BEEPER    
    analogWrite(beeperPin, 0);
#else    
    noTone(beeperPin);
#endif    
  }
}

char durationBuf[20];

char* formatDuration(uint32_t t) {
  if (t < 1000ul*60*3) {
    snprintf(durationBuf, 19, "%us", (t/1000));
  }
  else if (t < 1000ul*60*180) {
    snprintf(durationBuf, 19, "%lumin", (t/(1000ul*60)));
  }
  else {
    snprintf(durationBuf, 19, "%luhrs", (t/(1000ul*60*60)));
  }
  return durationBuf;
}

char buf[MAX_CHARS_PER_LINE+1];

void updateInformation() {
  uint8_t informNeeded = 0;
  for (int i=0; i<numEvents; i++) {
//    DEBUGMSG(String("inform ")+i+" "+events[i].needInform+" "+events[i].didInform);
    informNeeded |= events[i].needInform & ~events[i].didInform;
  }

  if (noConnectionWarningLight)
    informNeeded |= INFORM_LIGHT;

  if (informNeeded & INFORM_LIGHT) {
    if (!informLight) {
      informLight = 1;
      clearScreen();
    }
    backlight(1);
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

  uint8_t ledState = numEvents>0 || informLight;
//  DEBUGMSG(String("LED ")+ledState);
  analogWrite(ledPin, (ledReverse ? (1023-(ledState << 7)) : ( ledState << 7 )) >> (10-ANALOGWRITE_BITS) );
  
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
      displayLine(2*i, i==0 ? (retry != 0 ? "Update failed: Will retry." : "No weather warnings." ) : "");
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
  // Bubble sort is inefficient, but since we never expect to go beyond n=6, it's
  // fine, and we want a simpler algorithm so we can more easily verify that it
  // is bug-free.
  
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
  } while(n>0); //OK
}

uint32_t currentDelay() {
  if (retry < NUM_RETRY_DELAYS) {
    return retryDelays[retry];
  }
  else {
    return retryDelays[NUM_RETRY_DELAYS-1];
  }
}


void failureUpdateCheck() {
  if (retry >= warnRetryCount) {
    noConnectionWarningLight = 1;
  }
  else {
    noConnectionWarningLight = 0;
  }
}

WiFiClientSecure secureClient;
WiFiClient insecureClient;

void monitorWeather() {
  displayLine(STATUS_LINE2, "Connecting...");
  uint32 t0 = millis();

  const char* address;
  unsigned port;
  const char* request;

  bool success = false;

  WiFiClient* client;

#ifdef ALT_ADDRESS  
  if (retry % 3 == 2) {
      address = ALT_ADDRESS;
      port = ALT_PORT;
      request = "GET / HTTP/1.1\r\n"      
        "Connection: close\r\n\r\n";
      insecureClient.stop();
      client = &insecureClient;
  }
  else 
#endif  
  {
#ifdef OLD_API
    address = "alerts.weather.gov";
    port = 443;
    request = "GET /cap/wwaatmget.php?x=" LOCATION " HTTP/1.1\r\n"
      "Host: alerts.weather.gov\r\n"
      "User-Agent: weatherwarning-ESP8266\r\n"
      "Connection: close\r\n\r\n";
#else
    address = "api.weather.gov";
    port = 443;
    request = "GET /alerts/active/zone/" LOCATION " HTTP/1.1\r\n"
      "Host: api.weather.gov\r\n"
      "User-Agent: weatherwarning-ESP8266-arpruss@gmail.com\r\n"
      "Accept: application/atom+xml\r\n"
      "Connection: close\r\n\r\n";
#endif      
    secureClient.stop(); // just in case
    secureClient.setInsecure();
    client = &secureClient;
  }
  DEBUGMSG(address);
  if (client->connect(address, port)) {
    client->print(request);
    
    displayLine(STATUS_LINE2, "Loading...");
    
    XML_reset();
    
    while (client->connected()) { 
      if ((uint32)(millis()-t0) >= READ_TIMEOUT) 
        goto DONE;

      if ('\n' == client->read())
        break;

      ESP.wdtFeed();
    }
    
    ESP.wdtFeed();

    while ((uint32)(millis()-t0) < READ_TIMEOUT && (client->connected() || client->available())) { 
      if (client->available()) {
        char c = client->read();
        //DEBUGMSG_CHAR(c);
        xmlParseChar(c); 
        //ESP.wdtFeed();
      }
    }

    DEBUGMSG((uint32)(millis()-t0) < READ_TIMEOUT ? "on time" : "timeout");
   
DONE:
    client->stop();
    updateBeeper();
    yield();

    if (gotFeed) {
      for (int i=numEvents-1; i>=0; i--) {
        if (!events[i].fresh)
          deleteEvent(i);
      }
      DEBUGMSG("Successful read of feed");
      DEBUGMSG(String("Have ") + String(numEvents) + " events");
      lastUpdateSuccess = millis();
      retry = 0;
    }
    else {
      DEBUGMSG("Incomplete");
      retry++;
    }
  }
  else {
    DEBUGMSG("Connection failed");
    retry++;
  }
  
  if (numEvents>1) {
    // there are so few events that we'll just do a bubble sort
    sortEvents();
  }

  lastUpdate = millis();
  
  failureUpdateCheck();

  if (millis()-lastUpdateSuccess > FAILURE_REBOOT_MILLIS) 
    ESP.restart();
    
  updateInformation();
}

void handlePressed() {
  backlight(1);
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
  if (retry >= warnRetryCount) 
    retry = NUM_RETRY_DELAYS - 1;
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
      digitalWrite(ledPin, !ledReverse);
      while ((uint32_t)(millis() - lastButtonDown) < 10*1000ul) { //OK
        yield();
        updateBeeper();
      }
      stopBeeper();
      digitalWrite(ledPin, ledReverse);
      updateInformation();
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
  if (lastUpdate == UNDEF_TIME || (uint32_t)(millis() - lastUpdate) >= currentDelay()) {
    monitorWeather();
  }
  yield();
  updateBeeper();
  yield();
  if (!informLight && backlightState && (uint32_t)(millis() - screenOffTimer) >= screenOffDelay) {
    backlight(0);
  }
  failureUpdateCheck();
  if ((uint32_t)(millis() - lastInformationUpdate) >= informationUpdateDelay) {
    updateInformation();
  }
  yield();
}
