#ifndef __VIP_H__
#define __VIP_H__

#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#define _GNU_SOURCE

#define VIP_VERSION "0.0.1"
// remain last 5 bits
#define CTRL_KEY(k) ((k)&0x1f)
typedef unsigned short win_size_t;

/* append buffer */
struct abuf {
  char *b;
  int len;
};

#define ABUF_INIT \
  { NULL, 0 }

void ab_append(struct abuf *ab, const char *s, int len);
void ab_free(struct abuf *ab);

/* terminal */
void die(const char *msg);
void disable_raw_mode();
void disable_raw_mode();
inline int ed_read_key();
inline void ed_move_cursor2(struct abuf *ab, win_size_t x, win_size_t y);
int get_winsize(win_size_t *rows, win_size_t *cols);
int get_cursor_pos(win_size_t *rows, win_size_t *cols);

/* input */
inline void ed_process_move(int key);
inline void ed_normal_process(int key);
inline void ed_insert_process(int key);
inline void ed_process_keypress();

/* output */
inline int println(const char *fmt, ...);
inline void ed_draw_rows(struct abuf *ab);
inline void ed_draw_statusbar(struct abuf *ab);
inline void ed_draw_commandbar(struct abuf *ab);
inline void ed_clear();
inline void ed_refresh();

/* row ops */
// inline void ed_appand_row(char *s, size_t len);

/* file I/O */
void ed_open(const char *filename);

/* init */
inline void init_editor();

#endif  //__VIP_H__