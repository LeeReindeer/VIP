#include "vip.h"
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
  win_size_t cx, cy;
  win_size_t winrows;
  win_size_t wincols;
  struct termios origin_termios;
} Editor;

enum EditorKey {
  ARROW_LEFT = 1000,
  ARROW_RIGHT = 1001,
  ARROW_UP = 1002,
  ARROW_DOWN = 1003,

  HOME_KEY = 2001,
  DEL_KEY = 2003,
  END_KEY = 2004,
  PAGE_DOWN = 2005,
  PAGE_UP = 2006,

  LEFT = 'h',
  RIGHT = 'l',
  UP = 'k',
  DOWN = 'j',
  LINE_START = '0',
  LINE_END = '$',
};

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

int ed_read_key() {
  int nread;
  char c;
  // read() has a timeout, so it loop read util a key press
  // screen refresh after keypress
  while ((nread = read(STDIN_FILENO, &c, 1)) != 1) {
    if (nread == -1 && errno != EAGAIN) die("read");
  }
  // read arrow key(\x1b[A, \x1b[B, \x1b[C, \x1b[D), Home, page up down, end key
  if (c == '\x1b') {
    char seq[3];
    if (read(STDIN_FILENO, &seq[0], 1) != 1) return '\x1b';
    if (read(STDIN_FILENO, &seq[1], 1) != 1) return '\x1b';
    if (seq[0] == '[') {                     // seq[0]
      if (seq[1] >= '0' && seq[1] <= '9') {  // seq[1]
        if (read(STDIN_FILENO, &seq[2], 1) != 1) return '\x1b';
        if (seq[2] == '~') {  // seq[2]
          switch (seq[1]) {
            case '1':
              return HOME_KEY;
            case '3':
              return DEL_KEY;
            case '4':
              return END_KEY;
            case '5':
              return PAGE_UP;
            case '6':
              return PAGE_DOWN;
            case '7':
              return HOME_KEY;
            case '8':
              return END_KEY;
          }
        }
      } else {  // seq[1]
        switch (seq[1]) {
          case 'A':
            return ARROW_UP;
          case 'B':
            return ARROW_DOWN;
          case 'C':
            return ARROW_RIGHT;
          case 'D':
            return ARROW_LEFT;
          case 'H':
            return HOME_KEY;
          case 'F':
            return END_KEY;
        }
      }
    } else if (seq[0] == 'O') {  // seq[0]
      switch (seq[1]) {
        case 'H':
          return HOME_KEY;
        case 'F':
          return END_KEY;
      }
    }
    return '\x1b';
  } else {
    return c;
  }
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

void ed_move_cursor2(struct abuf *ab, win_size_t x, win_size_t y) {
  char buf[32];
  snprintf(buf, sizeof(buf), "\x1b[%hu;%huH", y + 1, x + 1);
  ab_append(ab, buf, strlen(buf));
}

/* input */

void ed_process_move(int key) {
  switch (key) {
    case 'h':
    case ARROW_LEFT:
      if (editor.cx > 0) editor.cx--;
      break;
    case 'l':
    case ARROW_RIGHT:
      if (editor.cx < editor.wincols - 1) editor.cx++;
      break;
    case 'j':
    case ARROW_DOWN:
      if (editor.cy < editor.winrows - 1) editor.cy++;
      break;
    case 'k':
    case ARROW_UP:
      if (editor.cy > 0) editor.cy--;
      break;
    default:
      break;
  }
}

void ed_process_keypress() {
  int c = ed_read_key();
  switch (c) {
    case CTRL_KEY('q'):
      ed_clear();
      exit(0);
      break;
    case 'h':
    case ARROW_LEFT:
    case 'l':
    case ARROW_RIGHT:
    case 'j':
    case ARROW_DOWN:
    case 'k':
    case ARROW_UP:
      ed_process_move(c);
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

  // move cursor to
  ed_move_cursor2(&ab, editor.cx, editor.cy);

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

  editor.cx = editor.cy = 0;

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