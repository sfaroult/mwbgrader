#ifndef DEBUG_H

#define DEBUG_H

extern void debug_on(void);
extern void debug_off(void);
extern char debugging(void);
extern void debug(short indent, const char *fmt, ...);
extern void debug_no_nl(short indent, const char *fmt, ...);

#endif
