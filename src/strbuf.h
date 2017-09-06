/*
 *   Utilities for managing character string reallocation
 *
 *   Written by Stephane Faroult
 */
#ifndef STRBUF_H

#define STRBUF_H

typedef struct {
                size_t len;
                size_t curlen;
                char  *s;
               } STRBUF;


extern void strbuf_init(STRBUF *sb);
extern void strbuf_dispose(STRBUF *sb);
extern void strbuf_clear(STRBUF *sb);
extern void strbuf_add(STRBUF *sb, char *s);
extern void strbuf_addc(STRBUF *sb, int c);
extern void strbuf_nadd(STRBUF *sb, char *s, size_t len);
extern void strbuf_concat(STRBUF *sb1, STRBUF *sb2);

#endif
