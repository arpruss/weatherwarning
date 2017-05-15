#ifndef _QUICKPARSE_H
#define _QUICKPARSE_H

typedef enum {
  XML_START_TAG,
  XML_END_TAG,
  XML_TAG_TEXT
} XMLEvent;

typedef void (*XMLCallback)(char* tag, char* data, XMLEvent event);
void xmlParseInit(XMLCallback c, char* tb, int tbs, char* db, int dbs);
void xmlParseChar(char c);

#endif