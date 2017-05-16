#include <ESP8266WiFi.h>
#include <WiFiClientSecure.h>
#include "quickparse.h"
#include "sha1.h"
#include <string.h>
#include <ctype.h>

#define memzero(p,n) memset((p),0,(n))

#define UNDEF_TIME 0xFFFFFFFFu
#define DEBOUNCE_TIME 50

const uint8_t beeperPin = 16;
const uint8_t buttonPin = 0;
const int beeperTones[][2] = { { 800, 500 }, {0, 700} };
const uint32_t delayTime = 60000; // in milliseconds
uint32_t toneStart = UNDEF_TIME;
uint32_t lastUpdate = UNDEF_TIME;
uint32_t lastUpdateSuccess = UNDEF_TIME;
uint32_t curUpdateDelay = delayTime;
uint32_t lastButtonDown = UNDEF_TIME;
uint8_t buttonState = 0;
int beeperState;
int screenHeight = 160;
int dataFontLineHeight = 12;
int statusFontLineHeight = 12;
int numDataLines = (160-statusFontLineHeight)/dataFontLineHeight;

#define MAX_LINES 30
#define MAX_CHARS_PER_LINE 40
char dataLines[MAX_LINES][MAX_CHARS_PER_LINE+1];
char statusLine[MAX_CHARS_PER_LINE];
#define STATUS_LINE -1

#define BEEPER_OFF -1

#define RETRY_TIME 30000

typedef enum {
   INFORM_NONE = 0,
   INFORM_LIGHT = 1,
   INFORM_LIGHT_AND_SOUND = 2
} InformLevel;

#define SSID_LENGTH 33
#define PSK_LENGTH  64
#if 1
#include "c:/users/alexander/Documents/Arduino/private.h"
#else
char ssid[SSID_LENGTH] = "SSID";
char psk[PSK_LENGTH] = "password";
#endif

#define MAX_EVENTS 64

//#define MAX_SUMMARY 450
#define MAX_EVENT 64
#define HASH_SIZE 20

typedef struct {
  uint8_t hash[20]; 
  char event[MAX_EVENT+1]; // empty for empty event
//  char summary[MAX_SUMMARY+1]; 
  //char effective[26]; 
  char expires[26];
  InformLevel needInform;
  InformLevel didInform;
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

void hash(uint8_t* hash, char* data) {
  int len = strlen(data);
  SHA1_CTX ctx;
  SHA1Init(&ctx);
  while(*data) {
    SHA1Update(&ctx, (const unsigned char*)data, 1);
    data++;
  }
  SHA1Final((unsigned char*)hash, &ctx);
}

void setup() {
  Serial.begin(115200);

  WiFi.begin(ssid, psk);

  while (WiFi.status() != WL_CONNECTED)
    delay(500);

  Serial.println(String("WeatherWarning connected as ")+String(WiFi.localIP()));

  numEvents = 0;
  beeperState = BEEPER_OFF;
  lastUpdate = UNDEF_TIME;
  curUpdateDelay = delayTime;

  pinMode(buttonPin, INPUT_PULLUP);
  pinMode(beeperPin, OUTPUT);
}

static char abridged[MAX_CHARS_PER_LINE+1];

void abridge(char* out, char* in) {
  // TODO: font!
  if (strlen(in) > MAX_CHARS_PER_LINE) {
    strncpy(out, in, MAX_CHARS_PER_LINE-3);
    strcpy(out+MAX_CHARS_PER_LINE-3, "...");
  }
  else {
    strcpy(out, in);
  }
}

void eraseText(int lineNumber, char* data) {
}

void drawText(int lineNumber, char* data) {
  Serial.println(String(lineNumber)+String(" ")+String(data));
}

void displayLine(int lineNumber, char* data) {
  char* current = lineNumber == STATUS_LINE ? statusLine : dataLines[lineNumber];
  abridge(abridged, data);
  if (0==strcmp(abridged, data)) { // TODO: font
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
  events[i].didInform = INFORM_NONE;
  events[i].fresh = 1;
}

void storeEventIfNeeded() {
  if (curEvent.event[0] == 0)
    return;
    
  if (NULL != strstr(curEvent.event, "tornado") || curEvent.severity == 0)
    curEvent.needInform = INFORM_LIGHT_AND_SOUND;
  else if (curEvent.severity == 1 || curEvent.severity == ARRAY_LEN(severityList) - 1 )
    curEvent.needInform = INFORM_LIGHT;
  else
    curEvent.needInform = INFORM_NONE;
  
  for (int i=0; i<numEvents; i++) {
    if (0==memcmp(events[i].hash, curEvent.hash, HASH_SIZE)) {
      curEvent.didInform = events[i].didInform;
      storeEvent(i);
      return;
    }
  }

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
    Serial.println("Start feed");
    inFeed = 1;
  }
  else if (inFeed) {
    if (event == XML_END_TAG && 0==strcmp(tagName, "feed")) {
      Serial.println("End feed");
      inFeed = 0;
      gotFeed = 1;
    }

    if (event == XML_START_TAG && 0==strcmp(tagName, "entry")) {
      inEntry = 1;
      haveId = 0;
      memzero(&curEvent, sizeof(EventInfo));
      Serial.println("Start entry");
    }
    else if (inEntry) {
      if (event == XML_END_TAG && 0==strcmp(tagName, "entry")) {
        if (haveId) 
          storeEventIfNeeded();
        inEntry = 0;
        Serial.println("End entry");
      }
      else if (event == XML_TAG_TEXT && 0==strcmp(tagName, "id")) {
        hash(curEvent.hash, data);
        char out[31];
        for (int i=0; i<20; i++) 
          sprintf(out+2*i, "%02x", curEvent.hash[i]);
        Serial.println(String("Hash ")+String(out));
      }
/*      else if (event == XML_TAG_TEXT && 0==strcmp(tagName, "summary")) {
        strncpy(curEvent.summary, data, MAX_SUMMARY);
        Serial.println("Summary: ");
        Serial.println(curEvent.summary);
      } */
      else if (event == XML_TAG_TEXT && 0==strcmp(tagName, "cap:event")) {
        for (int i=0; i<MAX_EVENT && data[i]; i++)
          curEvent.event[i] = tolower(data[i]);
        Serial.println(String("Event: ")+String(curEvent.event));
      }
      else if (event == XML_TAG_TEXT && 0==strcmp(tagName, "cap:expires")) {
        strncpy(curEvent.expires, data, 25);
        Serial.println(String("Expires: ")+String(curEvent.expires));
      }
      else if (event == XML_TAG_TEXT && 0==strcmp(tagName, "cap:severity")) {
        int i;
        for(i=0; i<ARRAY_LEN(severityList)-1; i++) // last one is "unknown" 
          if(0==strcasecmp(data, severityList[i]))
            break;
        curEvent.severity = i;
        Serial.println(String("Severity: ")+String(severityList[i]));
      }
    }
/*    if (inEntry && (statusflags & STATUS_TAG_TEXT) && 0==strcmp(tagName, "/http:/entry/cap:severity")) {
      Serial.println("Severity: ");
      Serial.println(data);
    } */
  }
  /*
  if (statusflags&STATUS_END_TAG) {
    Serial.println("End tag: ");
    Serial.println(tagName);
  }
  if (statusflags&STATUS_TAG_TEXT) {
    Serial.println("Tag: ");
    Serial.println(tagName);
    Serial.println("Data: ");
    Serial.println(data);
  }
  if (statusflags&STATUS_ATTR_TEXT) {
    Serial.println("Attr: ");
    Serial.println(tagName);
    Serial.println("Data: ");
    Serial.println(data);
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
    if (pitch>0)
      tone(beeperPin, pitch);
    else
      noTone(beeperPin);
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
    noTone(beeperPin);
  }
}

void updateInformation() {
  //TODO
}

int eventCompare(const void* a, const void* b) {
  return (int) ((const EventInfo*)a)->severity - (int) ((const EventInfo*)b)->severity;
}

void monitorWeather() {
  WiFiClientSecure client;
  if (client.connect("alerts.weather.gov", 443)) { // TXZ159=McLennan; TXZ419=El Paso; AZZ015=Flagstaff, AZ
    client.print("GET /cap/wwaatmget.php?x=AZZ015 HTTP/1.1\r\n"
      "Host: alerts.weather.gov\r\n"
      "User-Agent: weatherwarningESP8266\r\n"
      "Connection: close\r\n\r\n"); 
    XML_reset();
    while (client.connected()) {
      if (client.available()) {
        xmlParseChar(client.read());
        //Serial.write(client.read());
      }
      updateBeeper(); 
    }
    yield();
    if (gotFeed) {
      for (int i=numEvents-1; i>=0; i--) {
        if (!events[i].fresh)
          deleteEvent(i);
      }
      Serial.println("Successful read of feed");
      Serial.println(String("Have ") + String(numEvents) + " events");
    }
    if (numEvents>1)
      qsort(events,numEvents,sizeof(*events),eventCompare);
    lastUpdateSuccess = millis();
    curUpdateDelay = delayTime;
    updateInformation();
  }
  else {
    Serial.println("Connection failed");
    curUpdateDelay = RETRY_TIME;
  }
  lastUpdate = millis();
}

void handlePressed() {
  Serial.println("Pressed");
}

void handleButton() {
  if (LOW == digitalRead(buttonPin)) {
    if (!buttonState) {
       buttonState = 1;
       handlePressed();
    }  
    lastButtonDown = millis();
  }
  else {
    if (buttonState && millis() >= lastButtonDown + DEBOUNCE_TIME) {
      buttonState = 0;
    }
  }
}

void loop() {
  handleButton();
  yield();
  if (lastUpdate == UNDEF_TIME || (uint32_t)(millis() - lastUpdate) >= curUpdateDelay) {
    lastUpdate = millis();
    monitorWeather();
  }
  yield();
  updateBeeper();
  yield();
}

