#ifndef __VIP_H__
#define __VIP_H__

#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#define _GNU_SOURCE

typedef unsigned short win_size_t;

/* append buffer */
struct abuf {
  char *b;
  int len;
};
#define VIP_VERSION "0.0.1"
#define ABUF_INIT \
  { NULL, 0 }

inline void ab_append(struct abuf *ab, const char *s, int len);
inline void ab_free(struct abuf *ab);

/* terminal */
void die(const char *msg);
void disable_raw_mode();
void disable_raw_mode();
inline int ed_read_key();
int get_winsize(win_size_t *rows, win_size_t *cols);
int get_cursor_pos(win_size_t *rows, win_size_t *cols);
inline void ed_move_cursor2(struct abuf *ab, win_size_t x, win_size_t y);

/* input */
inline void ed_process_keypress();
inline void ed_process_move(int key);

/* output */
inline int println(const char *fmt, ...);
inline void ed_refresh();
inline void ed_clear();
inline void ed_draw_rows(struct abuf *ab);

/* init */
inline void init_editor();

#endif  //__VIP_H__