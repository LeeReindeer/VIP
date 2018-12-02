#include "vip.h"
#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
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
    return -1;
  } else {
    *rows = ws.ws_row;
    *cols = ws.ws_col;
    return 0;
  }
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

void ed_clear() {
  // clear screen
  write(STDOUT_FILENO, "\x1b[2J", 4);
  // reposition cursor
  write(STDOUT_FILENO, "\x1b[H", 3);
}

void ed_refresh() {
  // clear screen
  write(STDOUT_FILENO, "\x1b[2J", 4);
  // reposition cursor
  write(STDOUT_FILENO, "\x1b[H", 3);
}

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