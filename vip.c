#include "vip.h"
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

enum EditorMode { NORMAL_MODE = 0, INSERT_MODE };

struct motion {
  int n;  // default is 1, means do motion once. But for some motion(gg) is 0
  char motion[3];  // examples: h,j,k,l,G,gg,x,dd,yy
};

struct op_motion {
  char op;          // support operators: c,d,y;
  struct motion m;  // support motion: w, e, $, 0
};

struct text_row {
  int size;
  int rsize;  // render size
  char *string;
  char *render;  // render tab as multiple spaces
};

typedef struct editor_config {
  struct termios origin_termios;
  win_size_t cx, cy;  // cursor position
  // todo render tab
  win_size_t rx;       // index for render tab
  win_size_t prev_cx;  // previous cursor's x coordinate
  int row_offset;
  int col_offset;
  win_size_t winrows;
  win_size_t wincols;
  enum EditorMode mode;  // NORMAL_MODE, INSERT_MODE

  TextRow *row;
  int numrows;
  int rownum_width;  // line number width for printf("%*d", width, data)

  char *filename;  // opend file name, if argc == 1, display as [No Name]
  char file_opened;
  char commandmsg[100];
  time_t commandmsg_time;
} Editor;

enum EditorKey {
  // virtual map key in large number
  ARROW_LEFT = 1000,
  ARROW_RIGHT = 1001,
  ARROW_UP = 1002,
  ARROW_DOWN = 1003,

  HOME_KEY = 2001,
  INS_KEY = 2002,
  DEL_KEY = 2003,
  END_KEY = 2004,
  PAGE_DOWN = 2005,
  PAGE_UP = 2006,

  // real map key
  BACKSPACE = 127,
  ENTER = '\r',

  LEFT = 'h',
  RIGHT = 'l',
  UP = 'k',
  DOWN = 'j',
  LINE_START = '0',
  LINE_END = '$',

  NEWLINE_BEFORE_KEY = 'O',
  NEWLINE_AFTER_KEY = 'o',

  APPAND_CHAR_KEY = 'a',
  APPAND_LINE_KEY = 'A',

  JOIN_LINE_KEY = 'J',

  INSERT_MODE_KEY = 'i',
  NORMAL_MODE_KEY = '\x1b'
};

// remain last 5 bits
#define CTRL_KEY(k) ((k)&0x1f)
#define TEXT_START (editor.rownum_width + 1)
#define WIN_MAX_LENGTH ((int)editor.wincols + TEXT_START)
// row and col for text
#define CURRENT_COL ((int)editor.cx - TEXT_START)
#define CURRENT_ROW ((int)editor.cy)
#define MAX_CX(ROW) ((ROW).rsize + TEXT_START - 1)
#define MIN_CX TEXT_START
#define TAB_SIZE 8  // todo set in setting file .viprc
#define NEWLINE_AFTER 1
#define NEWLINE_INSERT 1
#define NEWLINE_BEFORE 0
static Editor editor;

/* terminal*/

void die(const char *msg) {
  ed_clear();
  perror(msg);
  println("");
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

void ed_process_move(int key) {
  TextRow *row = editor.numrows == 0 ? NULL : &editor.row[CURRENT_ROW];
  int text_start = row ? TEXT_START : 0;
  // todo fix tab
  // editor.cy won't >= editor.numrows
  // if row->size == 0, text_end = rownum_width
  int text_end = row ? TEXT_START + row->rsize - 1 : 0;
  switch (key) {
    case LEFT:
    case ARROW_LEFT:
      if (editor.cx != text_start) {
        editor.cx--;
      }
      editor.prev_cx = editor.cx;
      break;
    case RIGHT:
    case ARROW_RIGHT:
      if (editor.cx != text_end) {
        editor.cx++;
      }
      editor.prev_cx = editor.cx;
      break;
    case ENTER:
    case DOWN:
    case ARROW_DOWN:
      if (editor.cy < editor.numrows) {
        editor.cy++;
      }
      break;
    case UP:
    case ARROW_UP:
      if (editor.cy != 0) {
        editor.cy--;
      }
      break;
    case BACKSPACE:
    // 8 same as BACKSPACE
    case CTRL_KEY('h'):
      // same as ARROW_LEFT
      if (editor.cx != text_start) {
        editor.cx--;
      } else if (editor.cy != 0) {
        editor.cy--;
        editor.cx = editor.row[CURRENT_ROW].rsize + TEXT_START;
      }
      editor.prev_cx = editor.cx;
      break;
    default:
      break;
  }

  // snap cursor to end of line or prev position
  row = editor.numrows == 0 ? NULL : &editor.row[CURRENT_ROW];
  if (row) {
    // minus 1 only if row->size != 0,
    text_end = row ? TEXT_START + row->rsize : 0;
    // if (editor.col_offset > 0) text_end -= editor.col_offset;
    // from small line to large line, and reposition to prev
    if (editor.prev_cx < text_end) {
      editor.cx = editor.prev_cx;
    } else {  // down from a large line to a small line
      editor.cx = row->rsize == 0 ? text_end : text_end - 1;
    }
  }
}

void ed_normal_process(int c) {
  switch (c) {
    case NORMAL_MODE_KEY:
    case CTRL_KEY('l'):
      break;
      // todo use :w to save
    case CTRL_KEY('s'):
      ed_save();
      break;
    case DEL_KEY:
      ed_row_delete_char(&editor.row[CURRENT_ROW], CURRENT_COL);
      // DEL will back to normal mode and snap cursor
      to_normal_mode();
      // ed_process_move(ARROW_LEFT);
      break;
    case INS_KEY:
    case INSERT_MODE_KEY:
      // nothing in welcome screen
      if (!editor.file_opened) return;
      to_insert_mode();
      break;
    case CTRL_KEY('q'):
      ed_clear();
      exit(0);
      break;
    case LINE_START:
    case HOME_KEY:
      editor.cx = editor.numrows != 0 ? TEXT_START : 0;
      editor.prev_cx = editor.cx;
      if (editor.col_offset > 0) {
        editor.col_offset = 0;
      }
      break;
    case LINE_END:
    case END_KEY:
      editor.cx = editor.numrows != 0
                      ? TEXT_START + editor.row[CURRENT_ROW].rsize - 1
                      : 0;
      editor.prev_cx = editor.cx;
      break;
    case PAGE_DOWN:
    case PAGE_UP: {
      // move cursor to bottom
      if (c == PAGE_DOWN) {
        editor.cy = editor.row_offset + editor.winrows - 1;
        if (editor.cy > editor.numrows) editor.cy = editor.numrows;
      } else {
        // move cursor to top
        editor.cy = editor.row_offset;
      }
      int rows = editor.winrows;
      while (rows--) {
        ed_process_move(c == PAGE_UP ? ARROW_UP : ARROW_DOWN);
      }
    } break;
    case NEWLINE_AFTER_KEY:
      ed_insert_newline(NEWLINE_AFTER);
      break;
    case NEWLINE_BEFORE_KEY:
      ed_insert_newline(NEWLINE_BEFORE);
      break;
    case APPAND_CHAR_KEY:
      to_insert_mode();
      editor.cx++;
      break;
    case APPAND_LINE_KEY:
      to_insert_mode();
      editor.cx = MAX_CX(editor.row[CURRENT_ROW]) + 1;
      break;
    case JOIN_LINE_KEY: {
      if (CURRENT_ROW >= editor.numrows - 1) return;
      TextRow *next_row = &editor.row[CURRENT_ROW + 1];
      ed_joinstr2row(&editor.row[CURRENT_ROW], next_row->string,
                     next_row->size);
      ed_delete_row(CURRENT_ROW + 1);
    } break;
    case ENTER:
    case BACKSPACE:
    // 8 same as BACKSPACE
    case CTRL_KEY('h'):
    case ARROW_DOWN:
    case DOWN:
    case ARROW_UP:
    case UP:
    case ARROW_LEFT:
    case LEFT:
    case ARROW_RIGHT:
    case RIGHT:
      ed_process_move(c);
      break;
    default:
      break;
  }
}

void ed_insert_process(int c) {
  switch (c) {
    case NORMAL_MODE_KEY:
      to_normal_mode();
      break;
    case ARROW_DOWN:
    case ARROW_UP:
    case ARROW_LEFT:
    case ARROW_RIGHT:
      ed_process_move(c);
      break;
    case ENTER:
      ed_insert_newline(NEWLINE_INSERT);
      break;
    case BACKSPACE:
    case CTRL_KEY('h'):
      ed_delete_char_row(CURRENT_COL - 1);
      break;
    case DEL_KEY:
      ed_row_delete_char(&editor.row[CURRENT_ROW], CURRENT_COL);
      // DEL will back to normal mode and snap cursor
      to_normal_mode();
      break;
    default:
      ed_insert_char(c);
      break;
  }
}

void ed_process_keypress() {
  int key = ed_read_key();
  if (editor.mode == INSERT_MODE) {
    ed_insert_process(key);
  } else if (editor.mode == NORMAL_MODE) {
    ed_normal_process(key);
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
  int margin = ((int)editor.wincols - line_size) / 2;
  while (margin--) {
    ab_append(ab, " ", 1);
  }
}

// check if the cursor has moved outside of the visible window, and if so,
// adjust row_offset so that the cursor is just inside the visible window.
// called before refresh the screen.
void ed_scroll() {
  // scroll up
  if (editor.cy < editor.row_offset) {
    editor.row_offset = editor.cy;
  }
  // max cy is winrows - 1, cy index from 0
  // scroll down
  if (editor.cy >= editor.row_offset + editor.winrows) {
    editor.row_offset = editor.cy - editor.winrows + 1;
  }

  // scroll left
  // min cx is TEXT_START
  if (editor.cx - TEXT_START < editor.col_offset) {
    editor.col_offset = editor.cx - TEXT_START;
  }
  // scroll right
  if (editor.cx >= editor.col_offset + editor.wincols) {
    editor.col_offset = editor.cx - editor.wincols + 1;
  }
}

void ed_draw_rows(struct abuf *ab) {
  int y;
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

      int len = editor.row[filerow].rsize - editor.col_offset;
      // int len = editor.row[filerow].rsize;
      if (len < 0) len = 0;
      if (len > editor.wincols) len = editor.wincols;
      ab_append(ab, editor.row[filerow].render + editor.col_offset, len);
      // ab_append(ab, editor.row[filerow].render, len);
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

  char buf1[40];
  int linelen =
      snprintf(buf1, sizeof(buf1), "Ln%hu,Col%hu  %d lines", editor.cy + 1,
               editor.cx + 1 - TEXT_START, editor.numrows);
  int margin = editor.wincols + TEXT_START - statuslen - linelen;  //- 1;
  while (margin--) {
    ab_append(ab, " ", 1);
  }
  ab_append(ab, buf1, linelen);

  // back to normal color
  ab_append(ab, "\x1b[m", 3);
  ab_append(ab, "\r\n", 2);
}

void ed_set_commandmsg(const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  memset(editor.commandmsg, 0, sizeof(editor.commandmsg));
  vsnprintf(editor.commandmsg, sizeof(editor.commandmsg), fmt, args);
  va_end(args);
  editor.commandmsg_time = time(NULL);
}

void ed_draw_commandbar(struct abuf *ab) {
  // clear line
  ab_append(ab, "\x1b[K", 3);
  char buf[20];
  int modelen = snprintf(
      buf, sizeof(buf), "%s",
      editor.mode == NORMAL_MODE ? "-- NORMAL --  " : "-- INSERT --  ");
  ab_append(ab, buf, modelen);
  if (time(NULL) - editor.commandmsg_time < 5) {
    int size = sizeof(editor.commandmsg);
    ab_append(ab, editor.commandmsg,
              size > WIN_MAX_LENGTH ? WIN_MAX_LENGTH : size);
  }
}

void ed_clear() {
  // clear screen
  write(STDOUT_FILENO, "\x1b[2J", 4);
  // reposition cursor
  write(STDOUT_FILENO, "\x1b[H", 3);
}

// refresh(clear and repaint) screen after every key press,
// render text, draw bar and do many other stuffs.
// called in main loop
void ed_refresh() {
  ed_scroll();

  struct abuf ab = ABUF_INIT;
  ab.b = malloc(ab.cap);
  // hide cursor
  ab_append(&ab, "\x1b[?25l", 6);

  // ab_append(&ab, "\x1b[2J", 4); //clear entire screen`;
  // reposition cursor
  ab_append(&ab, "\x1b[H", 3);

  ed_draw_rows(&ab);
  ed_draw_statusbar(&ab);
  ed_draw_commandbar(&ab);

  // move the cursor
  ed_move_cursor2(&ab, editor.cx - editor.col_offset,
                  editor.cy - editor.row_offset);

  // show cursor
  ab_append(&ab, "\x1b[?25h", 6);

  write(STDOUT_FILENO, ab.b, ab.len);
  ab_free(&ab);
}

/* append buf */
void ab_append(struct abuf *ab, const char *s, int len) {
  char *buf = ab->b;
  if (ab->len + len >= ab->cap) {
    ab->cap = ab->cap * 2 + len;
    buf = realloc(ab->b, ab->cap);
    if (buf == NULL) return;
    ab->b = buf;
  }

  // appand s at end of new
  memcpy(&buf[ab->len], s, len);
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

// insert a new row, just like ed_row_insert
void ed_insert_row(int rpos, char *s, size_t len) {
  if (rpos < 0 || rpos > editor.numrows) return;

  editor.row = realloc(editor.row, sizeof(TextRow) * (editor.numrows + 1));
  // move all rows below rpos(included)
  // down one row that makes room for new row
  // if rpos equals numrows, it does nothing
  memmove(&editor.row[rpos + 1], &editor.row[rpos],
          sizeof(TextRow) * (editor.numrows - rpos));

  // new row
  editor.row[rpos].size = len;
  editor.row[rpos].string = malloc(len + 1);
  memcpy(editor.row[rpos].string, s, len);
  editor.row[rpos].string[len] = '\0';

  editor.row[rpos].rsize = 0;
  editor.row[rpos].render = NULL;
  ed_render_row(&editor.row[rpos]);

  editor.numrows++;
}

// insert c into pos
void ed_row_insert_char(TextRow *row, int pos, int c) {
  if (pos < 0 || pos > row->size) pos = row->size;
  row->string = realloc(row->string, row->size + 2);
  memmove(&row->string[pos + 1], &row->string[pos], row->size - pos + 1);
  row->size++;
  row->string[pos] = c;
  ed_render_row(row);
}

static inline void newline_before() { ed_insert_row(CURRENT_ROW, "", 0); }

static inline void newline_after() {
  TextRow *row = &editor.row[CURRENT_ROW];
  ed_insert_row(editor.cy + 1, &row->string[CURRENT_COL],
                row->size - CURRENT_COL);

  // reget current row
  row = &editor.row[CURRENT_ROW];
  row->size = CURRENT_COL;
  // cut strings after CURRENT_COL
  row->string[row->size] = '\0';
  ed_render_row(row);
}

static inline void newline_insert_mode() {
  if (editor.cx == TEXT_START) {
    // new line before current
    newline_before();
  } else {
    newline_after();
  }
  editor.cy++;
  editor.cx = TEXT_START;
}

static inline void newline_noraml_mode(int after) {
  if (after) {
    ed_insert_row(CURRENT_ROW + 1, "", 0);
    editor.cy++;
  } else {
    newline_before();
  }
  editor.cx = TEXT_START;
  // change mode to INSERT
  to_insert_mode();
}

// called when <ENTER> press in INSERT mode,
// or <o>, <O> pressed in NORMAL mode
void ed_insert_newline(int after) {
  if (editor.mode == NORMAL_MODE) {
    newline_noraml_mode(after);
  } else if (editor.mode == INSERT_MODE) {
    newline_insert_mode();
  }
}

void ed_row_delete_char(TextRow *row, int pos) {
  if (pos < 0 || pos >= row->size) return;
  // move a byte backwards
  memmove(&row->string[pos], &row->string[pos + 1], row->size - pos);
  // decrease size
  row->size--;
  ed_render_row(row);
}

void ed_free_row(TextRow *row) {
  free(row->render);
  free(row->string);
}

// join string s to row
void ed_joinstr2row(TextRow *row, char *s, size_t len) {
  row->string = realloc(row->string, row->size + len + 1);
  memcpy(&row->string[row->size], s, len);
  row->size += len;
  row->string[row->size] = '\0';
  ed_render_row(row);
}

// delete a row at rpos
// called from backspacing at the start of a line and
// dd operation
void ed_delete_row(int rpos) {
  if (rpos < 0 || rpos >= editor.numrows) return;
  ed_free_row(&editor.row[rpos]);
  // move up
  memmove(&editor.row[rpos], &editor.row[rpos + 1],
          sizeof(TextRow) * (editor.numrows - rpos - 1));
  editor.numrows--;
}

/* edit ops, called from ed_progress_keyprogress() */

void ed_insert_char(int c) {
  // open or create empty file, create a new line
  if (editor.numrows == editor.cy) {
    ed_insert_row(editor.numrows, "", 0);
  }
  // insert before cursor, just like vim
  ed_row_insert_char(&editor.row[CURRENT_ROW], CURRENT_COL, c);
  editor.cx++;
}

// delete char or row
void ed_delete_char_row(int pos) {
  if (editor.cy >= editor.numrows) return;
  // empty
  if (editor.cx == TEXT_START && editor.cy == 0) return;

  TextRow *row = &editor.row[CURRENT_ROW];
  if (editor.cx > TEXT_START) {
    // delete char on the cursor
    ed_row_delete_char(row, pos);
    editor.cx--;
  } else {
    // delete this row, join its string to previous line
    // editor.cx = MAX_CX(editor.row[CURRENT_ROW - 1]) + 1;
    editor.cx = editor.row[CURRENT_ROW - 1].size + TEXT_START;
    ed_joinstr2row(&editor.row[CURRENT_ROW - 1], row->string, row->size);
    ed_delete_row(CURRENT_ROW);
    editor.cy--;
  }
}

/* mode */

void to_normal_mode() {
  int max_size = MAX_CX(editor.row[CURRENT_ROW]);
  // snap cursor at the last char
  if (editor.cx > max_size) editor.cx = max_size;
  editor.mode = NORMAL_MODE;
}

void to_insert_mode() { editor.mode = INSERT_MODE; }

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
    ed_insert_row(editor.numrows, line, linelen);
  }

  char numbuf[10];
  editor.rownum_width = snprintf(numbuf, 10, "%d", editor.numrows);

  // move cursor after line number
  editor.cy = 0;
  editor.cx = editor.prev_cx = TEXT_START;

  free(line);
  fclose(fp);
  editor.file_opened = 1;
}

char *ed_rows2str(int *buflen) {
  int totallen = 0;
  for (int i = 0; i < editor.numrows; i++) {
    totallen += editor.row[i].size + 1;
  }
  *buflen = totallen;

  char *buf = malloc(totallen);
  char *p = buf;

  for (int i = 0; i < editor.numrows; i++) {
    memcpy(p, editor.row[i].string, editor.row[i].size);
    p += editor.row[i].size;
    // append \n
    *p = '\n';
    // move after to \n, just a new line
    p++;
  }
  return buf;
}

void ed_save() {
  if (!editor.file_opened) return;

  int len;
  char *buf = ed_rows2str(&len);

  int fd = open(editor.filename, O_RDWR | O_CREAT, 0644);
  // set file size
  if (fd != -1) {
    if (ftruncate(fd, len) != -1) {
      if (write(fd, buf, len) == len) {
        close(fd);
        free(buf);
        ed_set_commandmsg("%dL, %dC written", editor.numrows, len);
        return;
      }
    }
    close(fd);
  }
  free(buf);
  ed_set_commandmsg("can't save! I/O error: %s", strerror(errno));
}
/* init */

void init_editor() {
  enable_raw_mode();

  editor.cx = editor.cy = 0;
  editor.rx = 0;
  editor.mode = NORMAL_MODE;
  editor.numrows = 0;
  editor.row = NULL;
  editor.row_offset = editor.col_offset = 0;
  editor.filename = NULL;
  editor.file_opened = 0;
  editor.rownum_width = 0;

  editor.commandmsg[0] = '\0';
  editor.commandmsg_time = 0;

  if (get_winsize(&editor.winrows, &editor.wincols) == -1) die("get_winsize");

  ed_set_commandmsg("type <CTRL-Q> to quit");
}

static inline void init_rowcol() {
  // last 2 row draw as status bar
  editor.winrows -= 2;
  // first some cols display as line number
  editor.wincols -= TEXT_START;
}

int main(int argc, char const *argv[]) {
  init_editor();

  if (argc == 1) {
    // show welcome message
  } else if (argc == 2) {
    ed_open(argv[1]);
  } else {
    println("Usage: %s <filename>", argv[0]);
    exit(0);
  }

  init_rowcol();

  while (1) {
    ed_refresh();
    ed_process_keypress();
  }

  return 0;
}