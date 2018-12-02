#ifndef __VIP_H__
#define __VIP_H__

#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#define _GNU_SOURCE

/* terminal */
void die(const char *msg);
void disable_raw_mode();
void disable_raw_mode();
inline char ed_read_key();

/* input */
inline void ed_process_keypress();

/* output */
inline int println(const char *fmt, ...);

#endif  //__VIP_H__