#include "draw.h"
#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>  // for winsize
#include <termios.h>
#include <unistd.h>

#define CTRL_KEY(k) ((k)&0x1f)
typedef struct editor_config {
  struct termios origin_termios;
  win_size_t winrows;
  win_size_t wincols;
} Editor;

static Editor editor;

/* terminal*/

void die(const char *msg) {
  ed_clear();
  perror(msg);
  exit(1);
}

void disable_raw_mode() {
  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &editor.origin_termios) == -1)
    die("disable_raw_mode");
}

void enable_raw_mode() {
  struct termios raw;
  // get terminal attributes
  if (tcgetattr(STDIN_FILENO, &raw) == -1) die("tcgetattr");
  editor.origin_termios = raw;
  // restore at exit
  atexit(disable_raw_mode);
  // disable echo and canonical mode, turu of sign(CTRL-C, CTRL-Z, CTRL-V)
  // it turns out that termianl won' print what you input, and read
  // byte-by-byte instead of line-by-line
  raw.c_lflag &= ~(ECHO | ICANON | ISIG | IEXTEN);
  // disable CTRL-S, CTRL-Q, fix CTRL-M as 13 and miscellaneous
  raw.c_iflag &= ~(IXON | ICRNL | BRKINT | INPCK | ISTRIP);
  raw.c_cflag |= (CS8);
  // turn off  "\n" to "\r\n" translation
  raw.c_oflag &= ~(OPOST);
  raw.c_cc[VMIN] = 0;
  // read timeout at 200 ms
  // if don't set screen will not refresh until key press
  raw.c_cc[VTIME] = 2;
  // set back attr
  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) die("tcsetattr");
}

char ed_read_key() {
  int nread;
  char c;
  // read() has a timeout, so it loop read util a key press
  // screen refresh after keypress
  while ((nread = read(STDIN_FILENO, &c, 1)) != 1) {
    if (nread == -1 && errno != EAGAIN) die("read");
  }
  return c;
}

/**
 * @brief get win rows and cols
 * @retval return -1 when failed, return 0 when successd
 */
int get_winsize(win_size_t *rows, win_size_t *cols) {
  struct winsize ws;
  if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
    // move cursor to right 999 and move down to 999
    // The C and B commands are specifically documented
    // to stop the cursor from going past the edge of the screen.
    if (write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12) return -1;
    return get_cursor_pos(rows, cols);
  } else {
    *rows = ws.ws_row;
    *cols = ws.ws_col;
    return 0;
  }
}

/**
 * @brief send \x1b[6n to get cursor reply like \x1b[24;80R,
 * then parse the reply.
 */
int get_cursor_pos(win_size_t *rows, win_size_t *cols) {
  if (write(STDOUT_FILENO, "\x1b[6n", 4) != 4) return -1;

  char buf[32];
  unsigned int i = 0;

  while (i < sizeof(buf) - 1) {
    if (read(STDIN_FILENO, &buf[i], 1) != 1) break;
    if (buf[i] == 'R') break;
    i++;
  }
  buf[i] = '\0';
  // println("\r\n&buf[1]: '%s'", &buf[1]);
  if (buf[0] != '\x1b' || buf[1] != '[') return -1;
  if (sscanf(&buf[2], "%hu;%hu", rows, cols) != 2) return -1;
  return 0;
}

/* input */

void ed_process_keypress() {
  char c = ed_read_key();
  switch (c) {
    case CTRL_KEY('q'):
      ed_clear();
      exit(0);
      break;
    default:
      break;
  }
}

/* output */
int println(const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  vprintf(fmt, args);
  va_end(args);
  return printf("\r\n");
}

// assume input and output are alaphabet
// lowercase <-> uppercase
static inline int ed_toggle_case(int a) { return isalpha(a) ? a ^ 0x20 : -1; }

// center a line, appand spaces in front of it
static inline void ed_draw_center(struct abuf *ab, int line_size) {
  int margin = (editor.wincols - line_size) / 2;
  while (margin--) {
    ab_append(ab, " ", 1);
  }
}

// todo tilde, wlecome
void ed_draw_rows(struct abuf *ab) {
  for (int y = 0; y < editor.winrows; y++) {
    ab_append(ab, "~", 1);
    if (y == editor.winrows / 3) {
      char buf[80];
      int welcomelen = snprintf(
          buf, sizeof(buf), "VIP Editor - Vi Poor - version %s", VIP_VERSION);
      ed_draw_center(ab, welcomelen);
      ab_append(ab, buf,
                welcomelen > editor.wincols ? editor.wincols : welcomelen);
    }

    if (y == editor.winrows / 3 + 1) {
      char buf[20];
      int authorlen = snprintf(buf, sizeof(buf), "by LeeReindeer.");
      ed_draw_center(ab, authorlen);
      ab_append(ab, buf, authorlen);
    }
    // erases the part of the line to the right of the cursor.
    ab_append(ab, "\x1b[K", 3);
    if (y < editor.winrows - 1) ab_append(ab, "\r\n", 2);
  }
}

void ed_clear() {
  // clear screen
  write(STDOUT_FILENO, "\x1b[2J", 4);
  // reposition cursor
  write(STDOUT_FILENO, "\x1b[H", 3);
}

void ed_refresh() {
  struct abuf ab = ABUF_INIT;

  // hide cursor
  ab_append(&ab, "\x1b[?25l", 6);

  // ab_append(&ab, "\x1b[2J", 4);  // clear entire screen`;
  // reposition cursor
  ab_append(&ab, "\x1b[H", 3);

  ed_draw_rows(&ab);

  ab_append(&ab, "\x1b[H", 3);

  // hide cursor
  ab_append(&ab, "\x1b[?25h", 6);

  write(STDOUT_FILENO, ab.b, ab.len);
  ab_free(&ab);
}

/* append buffer */

void ab_append(struct abuf *ab, const char *s, int len) {
  char *new = realloc(ab->b, ab->len + len);

  if (new == NULL) return;
  // appand s at end of new
  memcpy(&new[ab->len], s, len);
  ab->b = new;
  ab->len += len;
}

void ab_free(struct abuf *ab) { free(ab->b); }
/* init */

void init_editor() {
  enable_raw_mode();

  if (get_winsize(&editor.winrows, &editor.wincols) == -1) die("get_winsize");
}

int main(int argc, char const *argv[]) {
  init_editor();

  while (1) {
    ed_refresh();
    ed_process_keypress();
  }

  return 0;
}