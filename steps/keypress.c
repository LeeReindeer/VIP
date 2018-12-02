#include <ctype.h>  // for iscntrl
#include <stdarg.h>
#include <stdlib.h>
#include <termios.h>
#include <unistd.h>

typedef struct editor_config {
  struct termios origin_termios;
} Editor;

static Editor editor;

void die(const char *msg) {
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
  // disable echo and canonical mode, turn off sign(CTRL-C, CTRL-Z, CTRL-V)
  // it turns out that termianl int what you input, and read
  raw.c_lflag &= ~(ECHO | ICANON | ISIG | IEXTEN);
  // disable CTRL-S, CTRL-Q, fix CTRL-M as 13 and miscellaneous
  raw.c_cflag |= (CS8);
  raw.c_iflag &= ~(IXON | ICRNL | BRKINT | INPCK | ISTRIP);
  raw.c_oflag &= ~(OPOST);
  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) die("tcsetattr");
}

static inline int println(const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  vprintf(fmt, args);
  va_end(args);
  return printf("\r\n");
}

int main(int argc, char const *argv[]) {
  enable_raw_mode();

  char c;
  while (read(STDIN_FILENO, &c, 1) == 1) {
    if (iscntrl(c)) {
      println("%d", c);
      // printf("%d\n", c);
    } else {
      println("%d ('%c')", c, c);
      // printf("%d ('%c')\n", c, c);
    }
    if (c == 'q') break;
  }
  return 0;
}
