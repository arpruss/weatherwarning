#include <ESP8266WiFi.h>
#include <WiFiClientSecure.h>
#include "TinyXML.h"
#include "sha1.h"
#include <string.h>

#define memzero(p,n) memset((p),0,(n))

uint32_t delayTime = 60000; // in milliseconds
uint8_t xmlBuffer[1024];
TinyXML xml;

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

#define MAX_EVENTS 40

#define MAX_SUMMARY 450
#define MAX_EVENT 64
#define HASH_SIZE 20

typedef struct {
  uint8_t hash[20]; 
  char event[MAX_EVENT+1]; // empty for empty event
  char summary[MAX_SUMMARY+1]; 
  //char effective[26]; 
  char expires[26];
  InformLevel needInform;
  InformLevel didInform;
  uint8_t fresh;
} EventInfo;

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

  xml.init((uint8_t*)&xmlBuffer,sizeof(xmlBuffer),&XML_callback);

  numEvents = 0;
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
  xml.reset();
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
  events[i].needInform = INFORM_LIGHT_AND_SOUND;
  events[i].didInform = INFORM_NONE;
  events[i].fresh = 1;
}

void storeEventIfNeeded() {
  if (curEvent.event[0] == 0)
    return;
  if (NULL == strcasestr(curEvent.event, "tornado"))
    return;
  
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

void XML_callback( uint8_t statusflags, char* tagName,  uint16_t tagNameLen,  char* data,  uint16_t dataLen ) {
  if (statusflags&STATUS_START_TAG && 0==strcmp(tagName, "/http:/feed")) {
    inFeed = 1;
  }
  else if (inFeed) {
    if (statusflags&STATUS_END_TAG && 0==strcmp(tagName, "/http:/feed")) {
      inFeed = 0;
      gotFeed = 1;
    }
    if (statusflags&STATUS_START_TAG && 0==strcmp(tagName, "/http:/entry")) {
      inEntry = 1;
      haveId = 0;
      memzero(&curEvent, sizeof(EventInfo));
      Serial.println("Start entry");
    }
    else if (inEntry) {
      if (statusflags&STATUS_END_TAG && 0==strcmp(tagName, "/http:/entry")) {
        if (haveId) 
          storeEventIfNeeded();
        inEntry = 0;
        Serial.println("End entry");
      }
      else if (statusflags&STATUS_TAG_TEXT && 0==strcmp(tagName, "/http:/id")) {
        hash(curEvent.hash, data);
        Serial.println("Stored hash");
        char out[31];
        for (int i=0; i<20; i++) 
          sprintf(out+2*i, "%02x", curEvent.hash[i]);
      }
      else if ((statusflags & STATUS_TAG_TEXT) && 0==strcmp(tagName, "/http:/entry/summary")) {
        strncpy(curEvent.summary, data, MAX_SUMMARY);
        Serial.println("Summary: ");
        Serial.println(curEvent.summary);
      }
      else if ((statusflags & STATUS_TAG_TEXT) && 0==strcmp(tagName, "/http:/entry/cap:event")) {
        strncpy(curEvent.event, data, MAX_EVENT);
        Serial.println("Event: ");
        Serial.println(curEvent.event);
      }
      else if ((statusflags & STATUS_TAG_TEXT) && 0==strcmp(tagName, "/http:/entry/cap:expires")) {
        strncpy(curEvent.expires, data, 25);
        Serial.println("Expires: ");
        Serial.println(curEvent.expires);
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

void updateInformation() {
  //TODO
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
        xml.processChar(client.read());
        //Serial.write(client.read());
      }
    }
    if (gotFeed) {
      for (int i=numEvents-1; i>=0; i--) {
        if (!events[i].fresh)
          deleteEvent(i);
      }
      Serial.println("Successful read of feed");
      Serial.println(String("Have ") + String(numEvents) + " events");
    }
    updateInformation();
  }
  else {
    Serial.println("Connection failed");
  }
}

void loop() {
  monitorWeather();
  delay(delayTime);
}
