#ifndef __VIP_H__
#define __VIP_H__

#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#define _GNU_SOURCE

typedef unsigned short win_size_t;

/* terminal */
void die(const char *msg);
void disable_raw_mode();
void disable_raw_mode();
inline char ed_read_key();
int get_winsize(win_size_t *rows, win_size_t *cols);

/* input */
inline void ed_process_keypress();

/* output */
inline int println(const char *fmt, ...);
inline void ed_refresh();
inline void ed_clear();

#endif  //__VIP_H__