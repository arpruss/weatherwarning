#include <ESP8266WiFi.h>
#include <WiFiClientSecure.h>
#include "TinyXML.h"

uint32_t delayTime = 30000; // in milliseconds
uint8_t xmlBuffer[1024];

#define SSID_LENGTH 33
#define PSK_LENGTH  64
#if 1
#include "c:/users/alexander/Documents/Arduino/private.h"
#else
char ssid[SSID_LENGTH] = "SSID";
char psk[PSK_LENGTH] = "password";
#endif

void setup() {
  Serial.begin(115200);

  WiFi.begin(ssid, psk);

  while (WiFi.status() != WL_CONNECTED)
    delay(500);

  Serial.println(String("WeatherWarning connected as ")+String(WiFi.localIP()));
}

uint8_t inEntry = 0;

void XML_reset() {
  inEntry = 0;
}

void XML_callback( uint8_t statusflags, char* tagName,  uint16_t tagNameLen,  char* data,  uint16_t dataLen ) {
  if (statusflags&STATUS_START_TAG && 0==strcmp(tagName, "/http:/entry")) {
    inEntry = 1;
    Serial.println("Start entry");
  }
  if (statusflags&STATUS_END_TAG && 0==strcmp(tagName, "/http:/entry")) {
    inEntry = 0;
    Serial.println("End entry");
  }
  if (inEntry && (statusflags & STATUS_TAG_TEXT) && 0==strcmp(tagName, "/http:/entry/summary")) {
    Serial.println("Summary: ");
    Serial.println(data);
  }
  if (inEntry && (statusflags & STATUS_TAG_TEXT) && 0==strcmp(tagName, "/http:/entry/cap:severity")) {
    Serial.println("Severity: ");
    Serial.println(data);
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

void monitorWeather() {
  WiFiClientSecure client;
  TinyXML xml;
  xml.init((uint8_t*)&xmlBuffer,sizeof(xmlBuffer),&XML_callback);
  if (client.connect("alerts.weather.gov", 443)) { // TXZ159=McLennan; TXZ419=El Paso; AZZ015=Flagstaff, AZ
    client.print("GET /cap/wwaatmget.php?x=AZZ015 HTTP/1.1\r\n"
      "Host: alerts.weather.gov\r\n"
      "User-Agent: weatherwarningESP8266\r\n"
      "Connection: close\r\n\r\n");
    while (client.connected()) {
      if (client.available()) {
        xml.processChar(client.read());
        //Serial.write(client.read());
      }
    }
  }
  else {
    Serial.println("Connection failed");
  }
}

void loop() {
  monitorWeather();
  delay(delayTime);
}

