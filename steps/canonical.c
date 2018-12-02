#include <unistd.h>

int main(int argc, char const *argv[]) {
  char c;
  while (read(STDIN_FILENO, &c, 1) == 1) {
    write(STDOUT_FILENO, &c, 1);
    if (c == 'q') break;
  }
  return 0;
}
