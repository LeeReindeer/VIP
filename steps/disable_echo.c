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
  // it turns out that termianl won' print what you input, and read
  raw.c_lflag &= ~(ECHO | ICANON);

  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) die("tcsetattr");
}

int main(int argc, char const *argv[]) {
  enable_raw_mode();

  char c;
  while (read(STDIN_FILENO, &c, 1) == 1) {
    write(STDOUT_FILENO, &c, 1);
    if (c == 'q') break;
  }
  return 0;
}
