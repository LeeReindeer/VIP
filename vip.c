#include "vip.h"
#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

enum EditorMode { NORMAL_MODE = 0, INSERT_MODE };

struct text_row {
  int size;
  int rsize;  // render size
  char *string;
  char *render;  // render tab as multiple spaces
};

typedef struct editor_config {
  struct termios origin_termios;
  win_size_t cx, cy;   // cursor position
  win_size_t prev_cx;  // previous cursor's x coordinate
  int row_offset;  // current first line offset, MAX_ROW_OFFSET, MIN_ROW_OFFSET
  win_size_t winrows;
  win_size_t wincols;
  enum EditorMode mode;  // NORMAL_MODE, INSERT_MODE

  TextRow *row;
  int numrows;
  int rownum_width;  // line number width for printf("%*d", width, data)

  char *filename;  // opend file name, if argc == 1, display as [No Name]
} Editor;

enum EditorKey {
  // virtual map key in large number
  ARROW_LEFT = 1000,
  ARROW_RIGHT = 1001,
  ARROW_UP = 1002,
  ARROW_DOWN = 1003,

  HOME_KEY = 2001,
  DEL_KEY = 2003,
  END_KEY = 2004,
  PAGE_DOWN = 2005,
  PAGE_UP = 2006,
  INS_KEY = 2007,

  // real map key
  BACKSPACE = 127,
  ENTER = '\r',

  LEFT = 'h',
  RIGHT = 'l',
  UP = 'k',
  DOWN = 'j',
  LINE_START = '0',
  LINE_END = '$',

  INSERT_MODE_KEY = 'i',
  NORMAL_MODE_KEY = '\x1b'
};

// remain last 5 bits
#define CTRL_KEY(k) ((k)&0x1f)
#define TEXT_START (editor.rownum_width + 1)
#define CURRENT_ROW (editor.cy + editor.row_offset)
#define MAX_ROW_OFFSET ((editor.numrows / editor.winrows) * editor.winrows)
#define MIN_ROW_OFFSET 0
#define TAB_SIZE 8  // todo set in setting file .viprc
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
  // read 1 byte and return;
  while ((nread = read(STDIN_FILENO, &c, 1)) != 1) {
    if (nread == -1 && errno != EAGAIN) die("read");
  }
  // todo read F1 F2 ..., ignore in normal mode, show as <F1>, <F2> in insert
  // mode
  // read arrow key(\x1b[A, \x1b[B, \x1b[C, \x1b[D), Home, page up down,
  // end key
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

int println(const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  vprintf(fmt, args);
  va_end(args);
  return printf("\r\n");
}

void ed_progress_move(int key) {
  TextRow *row = editor.numrows == 0 ? NULL : &editor.row[CURRENT_ROW];
  int text_start = row ? TEXT_START : 0;
  // todo fix tab
  // editor.cy won't >= editor.numrows
  // if row->size == 0, text_end = rownum_width
  int text_end = row ? TEXT_START + row->rsize - 1 : 0;
  switch (key) {
    case LEFT:
    case ARROW_LEFT:
      if (editor.cx != text_start) editor.cx--;
      editor.prev_cx = editor.cx;
      break;
    case RIGHT:
    case ARROW_RIGHT:
      if (editor.cx != /*editor.wincols - 1*/ text_end) editor.cx++;
      editor.prev_cx = editor.cx;
      break;
    case DOWN:
    case ARROW_DOWN:
      if (editor.numrows <= editor.winrows) {
        if (editor.cy != editor.numrows - 1) editor.cy++;
      } else {
        if (editor.cy != editor.winrows - 1)
          editor.cy++;
        else if (editor.row_offset < editor.numrows - editor.winrows)
          editor.row_offset++;
      }
      break;
    case UP:
    case ARROW_UP:
      if (editor.row_offset == 0) {
        if (editor.cy != 0) editor.cy--;
      } else {
        if (editor.cy != 0)
          editor.cy--;
        else if (editor.row_offset > 0)
          editor.row_offset--;
      }
      break;
    default:
      break;
  }

  // snap cursor to end of line or prev position
  row = editor.numrows == 0 ? NULL : &editor.row[CURRENT_ROW];
  // minus 1 only if row->size != 0,
  text_end = row ? TEXT_START + row->rsize : 0;
  // from small line to large line, and reposition to prev
  if (editor.prev_cx < text_end) {
    editor.cx = editor.prev_cx;
  } else {  // down from a large line to a small line
    editor.cx = row->rsize == 0 ? text_end : text_end - 1;
  }
}

void ed_normal_progress(int c) {
  switch (c) {
    case NORMAL_MODE_KEY:
    case CTRL_KEY('l'):
      break;
    case ENTER:
      break;
    case BACKSPACE:
    // 8 same as BACKSPACE
    case CTRL_KEY('h'):
      // todo
      break;
    case DEL_KEY:
      // todo
      break;
    case INS_KEY:
    case INSERT_MODE_KEY:
      editor.mode = INSERT_MODE;
      break;
    case CTRL_KEY('q'):
      ed_clear();
      exit(0);
      break;
    case LINE_START:
    case HOME_KEY:
      editor.cx = editor.numrows != 0 ? TEXT_START : 0;
      break;
    case LINE_END:
    case END_KEY:
      // todo fix tab
      editor.cx = editor.numrows != 0
                      ? TEXT_START + editor.row[editor.cy].rsize - 1
                      : 0;  // editor.wincols - 1;
      break;
    case PAGE_DOWN:
      if ((editor.row_offset += (int)editor.winrows) > editor.numrows) {
        editor.row_offset = MAX_ROW_OFFSET;
      }
      break;
    case PAGE_UP:
      if ((editor.row_offset -= (int)editor.winrows) < 0) {
        editor.row_offset = MIN_ROW_OFFSET;
      }
      break;
    case ARROW_DOWN:
    case DOWN:
    case ARROW_UP:
    case UP:
    case ARROW_LEFT:
    case LEFT:
    case ARROW_RIGHT:
    case RIGHT:
      ed_progress_move(c);
      break;
    default:
      break;
  }
}

void ed_insert_progress(int c) {
  switch (c) {
    case NORMAL_MODE_KEY:
      editor.mode = NORMAL_MODE;
      break;
    case ARROW_DOWN:
    case ARROW_UP:
    case ARROW_LEFT:
    case ARROW_RIGHT:
      ed_progress_move(c);
      break;
    default:
      ed_insert_char(c);
      break;
  }
}

void ed_progress_keypress() {
  int key = ed_read_key();
  if (editor.mode == INSERT_MODE) {
    ed_insert_progress(key);
  } else if (editor.mode == NORMAL_MODE) {
    ed_normal_progress(key);
  }
}

/* output */

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

void ed_draw_rows(struct abuf *ab) {
  int y;
  // char line_num[10] = {'\0'};
  for (y = 0; y < editor.winrows; y++) {
    int filerow = y + editor.row_offset;
    if (filerow >= editor.numrows) {
      ab_append(ab, "~", 1);
      if (editor.numrows == 0) {
        if (y == editor.winrows / 3) {
          char buf[80];
          int welcomelen =
              snprintf(buf, sizeof(buf), "VIP Editor - Vi Poor - version %s",
                       VIP_VERSION);
          // if (welcomelen > editor.wincols) welcomelen = editor.wincols;
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
      }
    } else {
      char linenum[10];
      int numlen = snprintf(linenum, sizeof(linenum), "%*d ",
                            editor.rownum_width, filerow + 1);
      ab_append(ab, linenum, numlen);

      int len = editor.row[filerow].rsize;
      if (len > editor.wincols) len = editor.wincols;
      ab_append(ab, editor.row[filerow].render, len);
    }
    //  0 erases the part of the line to the right of the cursor.
    // 0 is the  default argument,
    // so we leave out the argument and just use <esc>[K.
    ab_append(ab, "\x1b[K", 3);
    // if (y < editor.winrows - 1)
    ab_append(ab, "\r\n", 2);
  }
}

void ed_draw_statusbar(struct abuf *ab) {
  // inverted colors
  ab_append(ab, "\x1b[7m", 4);
  if (editor.filename == NULL) {
    editor.filename = calloc(10, sizeof(char));
    editor.filename = "[No Name]";
  }
  int statuslen = strlen(editor.filename);
  ab_append(ab, editor.filename, statuslen);

  char buf1[20];
  int linelen = snprintf(buf1, sizeof(buf1), "Ln%hu,Col%hu", editor.cy + 1,
                         editor.cx + 1 - TEXT_START);
  int margin = editor.wincols - statuslen - linelen;  //- 1;
  while (margin--) {
    ab_append(ab, " ", 1);
  }
  ab_append(ab, buf1, linelen);

  // back to normal color
  ab_append(ab, "\x1b[m", 3);
  ab_append(ab, "\r\n", 2);
}

void ed_draw_commandbar(struct abuf *ab) {
  char buf[20];
  int modelen =
      snprintf(buf, sizeof(buf), "%s",
               editor.mode == NORMAL_MODE ? "-- NORMAL --" : "-- INSERT --");
  ab_append(ab, buf, modelen);
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

  // ab_append(&ab, "\x1b[2J", 4); //clear entire screen`;
  // reposition cursor
  ab_append(&ab, "\x1b[H", 3);

  ed_draw_rows(&ab);
  ed_draw_statusbar(&ab);
  ed_draw_commandbar(&ab);

  // move the cursor
  ed_move_cursor2(&ab, editor.cx, editor.cy);

  // show cursor
  ab_append(&ab, "\x1b[?25h", 6);

  write(STDOUT_FILENO, ab.b, ab.len);
  ab_free(&ab);
}

/* append buf */
void ab_append(struct abuf *ab, const char *s, int len) {
  char *new = realloc(ab->b, ab->len + len);

  if (new == NULL) return;
  // appand s at end of new
  memcpy(&new[ab->len], s, len);
  ab->b = new;
  ab->len += len;
}

void ab_free(struct abuf *ab) { free(ab->b); }

/* row ops */

// renders tabs as multiple spaces
void ed_render_row(TextRow *row) {
  int tabs = 0;
  for (int i = 0; i < row->size; i++) {
    if (row->string[i] == '\t') tabs++;
  }

  free(row->render);
  row->render = malloc(row->size + tabs * (TAB_SIZE - 1) + 1);

  int cnt = 0;
  for (int i = 0; i < row->size; i++) {
    if (row->string[i] == '\t') {
      for (int j = 0; j < TAB_SIZE; j++) {
        row->render[cnt++] = ' ';
      }
    } else {
      row->render[cnt++] = row->string[i];
    }
  }
  row->render[cnt] = '\0';
  row->rsize = cnt;
}

void ed_appand_row(char *s, size_t len) {
  editor.row = realloc(editor.row, sizeof(TextRow) * (editor.numrows + 1));

  int this = editor.numrows;
  editor.row[this].size = len;
  editor.row[this].string = malloc(len + 1);
  memcpy(editor.row[this].string, s, len);
  editor.row[this].string[len] = '\0';

  editor.row[this].rsize = 0;
  editor.row[this].render = NULL;
  ed_render_row(&editor.row[this]);

  editor.numrows++;
}

// insert c into pos
void ed_row_insert(TextRow *row, int pos, int c) {
  if (pos < 0 || pos > row->size) pos = row->size;
  row->string = realloc(row->string, row->size + 2);
  memmove(&row->string[pos + 1], &row->string[pos], row->size - pos + 1);
  row->size++;
  row->string[pos] = c;
  ed_render_row(row);
}

/* edit ops, called from ed_progress_keyprogress() */

void ed_insert_char(int c) {
  // insert before cursor, just like vim
  ed_row_insert(&editor.row[CURRENT_ROW], editor.cx - TEXT_START, c);
  editor.cx++;
}

/* file I/O */

void ed_open(const char *filename) {
  FILE *fp = fopen(filename, "r");
  if (!fp) die("fopen");

  free(editor.filename);
  editor.filename = strdup(filename);

  char *line = NULL;
  size_t linecap = 0;
  ssize_t linelen;
  // read line by line
  while ((linelen = getline(&line, &linecap, fp)) != -1) {
    // remove \n and \r
    while (linelen > 0 &&
           (line[linelen - 1] == '\n' || line[linelen - 1] == '\r'))
      linelen--;
    ed_appand_row(line, linelen);
  }

  char numbuf[10];
  editor.rownum_width = snprintf(numbuf, 10, "%d", editor.numrows);

  // move cursor after line number
  editor.cy = 0;
  editor.cx = editor.prev_cx = TEXT_START;

  free(line);
  fclose(fp);
}

/* init */

void init_editor() {
  enable_raw_mode();

  editor.cx = editor.cy = 0;
  editor.mode = NORMAL_MODE;
  editor.numrows = 0;
  editor.row = NULL;
  editor.row_offset = 0;
  editor.filename = NULL;
  editor.rownum_width = 0;

  if (get_winsize(&editor.winrows, &editor.wincols) == -1) die("get_winsize");

  // last 2 row draw as status bar
  editor.winrows -= 2;
}

int main(int argc, char const *argv[]) {
  init_editor();

  if (argc == 1) {
    // show welcome message
  } else if (argc == 2) {
    ed_open(argv[1]);
  } else {
    // todo show help
  }

  while (1) {
    ed_refresh();
    ed_progress_keypress();
  }

  return 0;
}