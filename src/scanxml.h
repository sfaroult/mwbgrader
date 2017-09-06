#ifndef _SCANXML_H
#define _SCANXML_H

extern int parseXML(short variant, char *xml, char show_tags, char debug);
extern int parsePipedXML(int fd, char show_tags, char debug);

#endif
