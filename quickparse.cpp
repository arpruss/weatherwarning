#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <stdint.h>
#include <string.h>
#include "quickparse.h"

// TODO: handle attributes (currently skipped, as not needed for my application)

typedef enum {
  XML_NONE,
  XML_WAIT_QUOTE,
  XML_WAIT_APOSTROPHE,
  XML_TAG,
  XML_TAG_NAME,
  XML_COMMENT
} XMLState;

static uint8_t state;
static uint8_t closingTag;
static uint8_t alsoClosed;
static char* tagBuffer;
static int tagBufferSize;
static int tagBufferPos;
static char* dataBuffer;
static int dataBufferSize;
static int dataBufferPos;
static int dashCount;

static XMLCallback callback;

void xmlParseInit(XMLCallback c, char* tb, int tbs, char* db, int dbs) {
  callback = c;
  state = XML_NONE;
  tagBuffer = tb;
  tagBufferSize = tbs;
  dataBuffer = db;
  dataBufferSize = dbs;
  dataBufferPos = 0;
}

static void startTag() {
  state = XML_TAG_NAME;
  tagBufferPos = 0;
  closingTag = 0;
  alsoClosed = 0;
}

void xmlParseChar(char c) {
  switch(state) {
      case XML_NONE:
        if (c=='<') {
            startTag();
        }
        else {
          if (dataBufferPos + 1 < dataBufferSize) 
            dataBuffer[dataBufferPos++] = c;
        }
        break;
      case XML_TAG_NAME:
        if (isspace(c)) {
           tagBuffer[tagBufferPos] = 0;
           state = XML_TAG;
        }
        else if (c == '/') {
          if (tagBufferPos == 0) {
            closingTag = 1;
          }
          else {
            alsoClosed = 1;  
            tagBuffer[tagBufferPos] = 0;
            state = XML_TAG;    
          }              
        }
        else if (c == '>') {
           tagBuffer[tagBufferPos] = 0;
           state = XML_TAG;
           xmlParseChar('>');
        }
        else 
        {
          if (tagBufferPos + 1 < tagBufferSize)
            tagBuffer[tagBufferPos++] = c;   
        }
        if (state == XML_TAG && 0==strcmp(tagBuffer, "!--")) {
          state = XML_COMMENT;
          dashCount = 0;
        }
        break;
      case XML_TAG:
        if (c == '"') {
          state = XML_WAIT_QUOTE;
        }        
        else if (c == '\'') {
          state = XML_WAIT_APOSTROPHE;
        }
        else if (c == '/') {
          alsoClosed = 1;
        }
        else if (c == '>') {
          tagBuffer[tagBufferPos] = 0;          
          if (!closingTag) {
                callback(tagBuffer, NULL, XML_START_TAG);
              if (alsoClosed)
                callback(tagBuffer, NULL, XML_END_TAG);
          }
          else {
            if (dataBufferPos > 0) {
              dataBuffer[dataBufferPos] = 0;
              callback(tagBuffer, dataBuffer, XML_TAG_TEXT);
            }
            callback(tagBuffer, NULL, XML_END_TAG);
          }
          dataBufferPos = 0;
          state = XML_NONE;
        }
        break;
      case XML_WAIT_QUOTE:
        if (c == '"') {
            state = XML_TAG;
        }
        else if (c == '<') {
            startTag(); // malformed, but don't want to miss tag
        }
        break;
      case XML_WAIT_APOSTROPHE:
        if (c == '\'') {
            state = XML_TAG;
        }
        else if (c == '<') {
            startTag(); // malformed, but don't want to miss tag
        }
        break;
      case XML_COMMENT:
        if (c == '-') {
           dashCount++;
        }
        else if (c == '>') {
           if (dashCount >= 2) {
              state = XML_NONE; 
           }
           dashCount = 0;
        }
        else {
           dashCount = 0; 
        }
        break;        
  }
}

/*
void testCallback(char* tag, char* data, XMLEvent event) {
    if (event == XML_TAG_TEXT) {
        printf("data for %s: %s\n", tag, data);
    }
    else if (event == XML_START_TAG) {
        printf("start: %s\n", tag);
    }
    else if (event == XML_END_TAG) {
        printf("end: %s\n", tag);
    }
}

int main() 
{
    int c;
    char tag[257];
    char data[120];
    init(testCallback, tag, 257, data, 120);
    while (-1 != (c=getchar())) {
        parseChar(c);
    }
    return 0;
}
*/
