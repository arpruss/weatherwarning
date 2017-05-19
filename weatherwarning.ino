#include <ESP8266WiFi.h>
#include <WiFiClientSecure.h>
#include "quickparse.h"
#include "sha1.h"
#include <string.h>
#include <ctype.h>

#include <TFT_eSPI.h> // https://github.com/Bodmer/TFT_eSPI

#define memzero(p,n) memset((p),0,(n))

#define UNDEF_TIME 0xFFFFFFFFu
#define DEBOUNCE_TIME 50

// Display SDO/MISO  to NodeMCU pin D6 (or leave disconnected if not reading TFT)
// Display LED       to NodeMCU pin VIN (or 5V, see below)
// Display SCK       to NodeMCU pin D5
// Display SDI/MOSI  to NodeMCU pin D7
// Display DC (RS/AO)to NodeMCU pin D3
// Display RESET     to NodeMCU pin D4 (or RST, see below)
// Display CS        to NodeMCU pin D8 (or GND, see below)
// Display GND       to NodeMCU pin GND (0V)
// Display VCC       to NodeMCU 5V or 3.3V
//#define TFT_CS   PIN_D8  // Chip select control pin D8
//#define TFT_DC   PIN_D3  // Data Command control pin

#define LOCATION "KSZ032" // "TXZ159" // "KSZ032" // 
#define LOCATION_NAME "McLennan" // "TEST" // 

const uint8_t beeperPin = 16;
const uint8_t buttonPin = 0;
const int beeperTones[][2] = { { 800, 500 }, {0, 700} };
const uint32_t delayTime = 60000; // in milliseconds
const uint32_t informationUpdateDelay = 1000;
uint32_t toneStart = UNDEF_TIME;
uint32_t lastUpdate = UNDEF_TIME;
uint32_t lastUpdateSuccess = UNDEF_TIME;
uint32_t curUpdateDelay = delayTime;
uint32_t lastButtonDown = UNDEF_TIME;
uint8_t buttonState = 0;
int beeperState;
#define screenHeight 128
#define screenWidth  160
#define dataFontLineHeight 8
#define dataFontCharWidth 6
#define statusFontLineHeight 8
#define statusFontCharWidth 6
#define numDataLines ((screenHeight-2*statusFontLineHeight)/dataFontLineHeight)
uint32_t lastInformationUpdate = UNDEF_TIME;

int colors[2] = { 0x0000, 0xFFFF };
uint8_t colorReverse = 0;

#define MAX_CHARS_PER_LINE (screenWidth / (dataFontCharWidth<statusFontCharWidth ? dataFontCharWidth : statusFontCharWidth))
char dataLines[numDataLines+2][MAX_CHARS_PER_LINE+1];
#define STATUS_LINE1 numDataLines
#define STATUS_LINE2 (numDataLines+1)

TFT_eSPI tft = TFT_eSPI();

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

#define MAX_EVENTS 20

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

void clearScreen() {
  tft.fillRect(0,0,screenWidth,screenHeight-2*statusFontLineHeight,colors[colorReverse]);
  tft.fillRect(0,screenHeight-2*statusFontLineHeight,screenWidth,2*statusFontLineHeight,colors[1^colorReverse]);
}

void setup() {
  Serial.begin(9600);

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
//  tft.initR(INITR_BLACKTAB); // initR(tftVersion);
  tft.begin();
  Serial.println(String(tft.width()));
  Serial.println(String(tft.height()));
  
  tft.setRotation(1);
  Serial.println(String(tft.width()));
  Serial.println(String(tft.height()));
  tft.fillScreen(colors[0]);
  tft.setCursor(0,0);
  tft.setTextColor(colors[1]);
  tft.print("WeatherWarning");
  delay(2500);
  clearScreen();
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
  tft.setTextColor( colors[ (lineNumber>=STATUS_LINE1)^colorReverse ] );
  writeText(lineNumber, data);
}

void drawText(int lineNumber, const char* data) {
  tft.setTextColor( colors[ 1^(lineNumber>=STATUS_LINE1)^colorReverse ] );
  Serial.println("Color "+String(colors[ (lineNumber>=STATUS_LINE1)^colorReverse ]));
  writeText(lineNumber, data);
  Serial.println(String(lineNumber)+String(" ")+String(data));
}

void displayLine(int lineNumber, const char* data) {
  Serial.println(String(lineNumber)+" "+String(data));
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
  Serial.println(curEvent.event);
  
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

  Serial.println("adding");
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
        haveId = 1;
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

void formatTime(char* buf, const char* t) {
  strncpy(buf, t+5, 2);
  buf[2] = '/';
  strncpy(buf+3, t+8, 2);
  buf[5] = ' ';
  strncpy(buf+6, t+11, 5);
  buf[6+5] = 0;
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

char buf[MAX_CHARS_PER_LINE+1];

void updateInformation() {
  if (numEvents > numDataLines/2) {
    snprintf(buf, MAX_CHARS_PER_LINE, "+ %d more events", numEvents-numDataLines/2);
    displayLine(STATUS_LINE1, buf);
  }
  else {
    displayLine(STATUS_LINE1, "");
  }
  
  if (lastUpdateSuccess == UNDEF_TIME) {
    displayLine(STATUS_LINE2, "No data received yet.");
  }
  else {
    uint32_t delta = millis()-lastUpdateSuccess;
    snprintf(buf, MAX_CHARS_PER_LINE, LOCATION_NAME " %ld.%us ago", delta/1000, (unsigned int)((delta/1000)%10));
    displayLine(STATUS_LINE2, buf);
  }

  for (int i=0; 2*i < numDataLines; i++) {
    if (i<numEvents) {
      displayLine(2*i, events[i].event);
      if (strlen(events[i].expires)>11) {
        strcpy(buf, "expires ");
        formatTime(buf+8, events[i].expires);
        displayLine(2*i+1, buf);
      }
    }
    else {
      displayLine(2*i, i==0 ? "No weather warnings." : "");
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
      if (eventCompare(events+i-1,events+i)) {
        EventInfo t = events[i-1];
        events[i-1] = events[i];
        events[i] = t;
        newN = i;
      }
    }
    n = newN;
  } while(n>0);
}

void monitorWeather() {
  WiFiClientSecure client;
  if (client.connect("alerts.weather.gov", 443)) { // TXZ159=McLennan; AZZ015=Flagstaff, AZ
    client.print("GET /cap/wwaatmget.php?x=" LOCATION " HTTP/1.1\r\n"
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
    if (numEvents>1) {
      // there are so few events that we'll just do a bubble sort
      sortEvents();
    }

    lastUpdateSuccess = millis();
    curUpdateDelay = delayTime;
    Serial.println("Updating");
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
  if ((uint32_t)(millis() - lastInformationUpdate) >= informationUpdateDelay) {
    updateInformation();
  }
}

