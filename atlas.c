#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#define _GNU_SOURCE

#include <time.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <sys/types.h>

#define VERSION "1.0"
#define TAB_SPACE 4
#define FORCE_QUIT 2
#define CTRL_KEY(k) ((k) & 0x1f)
#define ABUF_INIT {NULL, 0}
enum key
{
    ARROW_UP,
    ARROW_LEFT = 1000,
    ARROW_DOWN,
    ARROW_RIGHT,
    BACKSPACE = 127,
    DEL,
    HOME,
    END,
    PAGE_UP,
    PAGE_DOWN
};
typedef struct erow
{
    int size;
    int rsize;
    char *chars;
    char *render;
} erow;
struct config 
{
    int x, y;
    int rx;
    int rows;
    int cols;
    int numrows;
    int rowoffset;
    int columnoffset;
    erow *row;
    int modified;
    char *filename;
    char statmssg[100];
    time_t statmssg_time;
    struct termios orig_termios;
};
struct config C;

//prototypes
void setStatMssg(const char *fmt, ...);


void kill(const char *s)
{
    write(STDOUT_FILENO, "\x1b[2J", 4);
    write(STDOUT_FILENO, "\x1b[H", 3);
    perror(s);
    exit(1);
}
void disableRaw()
{
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &C.orig_termios) == -1)
        kill("tcsetattr");
}
void allowRaw()
{
    if(tcgetattr(STDIN_FILENO, &C.orig_termios) == -1)
        kill("tcgetattr");
    atexit(disableRaw);
    struct termios raw = C.orig_termios;
    raw.c_iflag &= ~(IXON | ICRNL | BRKINT | INPCK | ISTRIP);
    raw.c_oflag &= ~(OPOST);
    raw.c_cflag |= (CS8);
    raw.c_lflag &= ~(ECHO | ICANON | ISIG | IEXTEN);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 1;
    if(tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1)
        kill("tcsetattr");
}
int readKey()
{
    int nread;
    char c;
    while((nread = read(STDIN_FILENO, &c, 1)) != 1)
    {
        if(nread == -1 && errno != EAGAIN)
            kill("read");
    }
    if(c == '\x1b')
    {
        char seq[3];

        if(read(STDIN_FILENO, &seq[0], 1) != 1)
            return '\x1b';
        if(read(STDIN_FILENO, &seq[1], 1) != 1)
            return '\x1b';
        if(seq[0] == '[')
        {
            if(seq[1] >= '0' && seq[1] <= '9')
            {
                if(read(STDIN_FILENO, &seq[2], 1) != 1)
                    return '\x1b';
                if(seq[2] == '~')
                {
                    switch(seq[1])
                    {
                        case '1': return HOME;
                        case '3': return DEL;
                        case '4': return END;
                        case '5': return PAGE_UP;
                        case '6': return PAGE_DOWN;
                        case '7': return HOME;
                        case '8': return END;
                    }
                }
            }
            else
            {
                switch (seq[1])
                {
                    case 'A': return ARROW_UP;
                    case 'B': return ARROW_DOWN;
                    case 'C': return ARROW_RIGHT;
                    case 'D': return ARROW_LEFT;
                    case 'H': return HOME;
                    case 'F': return END;
                }
            }
        }
        else if (seq[0] == '0')
        {
            switch(seq[1])
            {
                case 'H': return HOME;
                case 'F': return END;
            }
        }
        return '\x1b';
    }
    else
        return c;
}
int getCursorLoc(int *rows, int *cols)
{
    char buf[32];
    unsigned int i = 0;
    if(write(STDOUT_FILENO, "\x1b[6n", 4) != 4)
        return -1;
    while(i < sizeof(buf) - 1)
    {
        if(read(STDIN_FILENO, &buf[i], 1) != 1)
            break;
        if(buf[i] == 'R')
            break;
        i++;
    }
    buf[i] = '\0';
    if (buf[0] != '\x1b' || buf[1] != '[')
        return -1;
    if (sscanf(&buf[2], "%d;%d", rows, cols) != 2)
        return -1;
    return 0;
}
int getWindowSize(int *rows, int *cols)
{
    struct winsize ws;
    if(ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0)
    {    
        if (write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12)
            return -1;
        return getCursorLoc(rows, cols);
    }
    else
    {
        *rows = ws.ws_row;
        *cols = ws.ws_col;
        return 0;
    }
}
int rowXtoRx(erow *row, int cx)
{
    int i, rx = 0;
    for(i = 0; i < cx; i++)
    {
        if(row->chars[i] == '\t')
            rx += (TAB_SPACE - 1) - (rx % TAB_SPACE);
        rx++;
    }
    return rx;
}
void updateRow(erow *row)
{
    int tabs = 0;
    int i;
    for(i = 0; i < row->size; i++)
        if(row->chars[i] == '\t')
            tabs++;
    free(row->render);
    row->render = malloc(row->size + tabs*(TAB_SPACE - 1) + 1);

    int idx = 0;
    for(i = 0; i < row->size; i++)
        if(row->chars[i] == '\t')
        {
            row->render[idx++] = ' ';
            while(idx % TAB_SPACE != 0)
                row->render[idx++] = ' ';
        }
        else
            row->render[idx++] = row->chars[i];
    row->render[idx] = '\0';
    row->rsize = idx;
}
void insertRow(int at, char *s, size_t len)
{
    if(at < 0 || at > C.numrows)
        return;
    C.row = realloc(C.row, sizeof(erow) * (C.numrows + 1));
    memmove(&C.row[at + 1], &C.row[at], sizeof(erow) * (C.numrows - at));
    C.row[at].size = len;
    C.row[at].chars = malloc(len + 1);
    memcpy(C.row[at].chars, s, len);
    C.row[at].chars[len] = '\0';
    


    C.row[at].rsize = 0;
    C.row[at].render = NULL;
    updateRow(&C.row[at]);

    C.numrows++;
    C.modified++;
}
void freeRow(erow *row)
{
    free(row->render);
    free(row->chars);
}
void deleteRow(int at)
{
    if(at < 0 || at >= C.numrows)
        return;
    freeRow(&C.row[at]);
    memmove(&C.row[at], &C.row[at + 1], sizeof(erow) * (C.numrows - at + 1));
    C.numrows--;
    C.modified++;
}
void rowInsertChar(erow *row, int at, int c)
{
    if (at < 0 || at > row->size)
        at = row->size;
    row->chars = realloc(row->chars, row->size + 2);
    memmove(&row->chars[at + 1], &row->chars[at], row->size - at + 1);
    row->size++;
    row->chars[at] = c;
    updateRow(row);
    C.modified++;
}
void rowAppendString(erow *row, char *s, size_t l)
{
    row->chars = realloc(row->chars, row->size + l + 1);
    memcpy(&row->chars[row->size], s, l);
    row->size += l;
    row->chars[row->size] = '\0';
    updateRow(row);
    C.modified++;
}
void rowDeleteChar(erow *row, int at)
{
    if(at < 0 || at >= row->size)
        return;
    memmove(&row->chars[at], &row->chars[at + 1], row->size - at);
    row->size--;
    updateRow(row);
    C.modified++;
}
void insertChar(int c)
{
    if(C.y == C.numrows)
       insertRow(C.numrows, "", 0);
    rowInsertChar(&C.row[C.y], C.x, c);
    C.x++;
}
void insertNewLine()
{
    if(C.x == 0)
        insertRow(C.y, "", 0);
    else
    {
        erow *row = &C.row[C.y];
        insertRow(C.y + 1, &row->chars[C.x], row->size - C.x);
        row = &C.row[C.y];
        row->size = C.x;
        row->chars[row->size] = '\0';
        updateRow(row);
    }
    C.y++;
    C.x = 0;
}
void deleteChar()
{
    if(C.y == C.numrows)
        return;
    if(C.x == 0 && C.y == 0)
        return;
    erow *row = &C.row[C.y];
    if (C.x > 0)
    {
        rowDeleteChar(row, C.x - 1);
        C.x--;
    }
    else
    {
        C.x = C.row[C.y - 1].size;
        rowAppendString(&C.row[C.y - 1], row->chars, row->size);
        deleteRow(C.y);
        C.y--;
    }
}
char *rowsToString(int *buffer_len)
{
    int i, total_len = 0;
    for(i = 0; i < C.numrows; i++)
        total_len += C.row[i].size + 1;
    *buffer_len = total_len;

    char *buffer = malloc(total_len);
    char *p = buffer;
    for(i = 0; i < C.numrows; i++)
    {
        memcpy(p, C.row[i].chars, C.row[i].size);
        p += C.row[i].size;
        *p = '\n';
        p++;
    }
    return buffer;
}
void fileOpen(char *filename)
{
    free(C.filename);
    C.filename = strdup(filename);
    FILE *fp = fopen(filename, "r");
    if(!fp)
        kill("fopen");
    char *line = NULL;
    size_t linecap = 0;
    ssize_t linelen;
    while ((linelen = getline(&line, &linecap, fp)) != -1)
    {
        while(linelen > 0 && (line[linelen - 1] == '\n' || line[linelen - 1] == '\r'))
            linelen--;
        insertRow(C.numrows, line, linelen);
    }
    free(line);
    fclose(fp);
    C.modified = 0;
}   
void fileSave()
{
    if(C.filename == NULL)
        return;
    int l;
    char *buffer = rowsToString(&l);
    int fd = open(C.filename, O_RDWR | O_CREAT, 0644);
    if(fd != -1)
    {
        if(ftruncate(fd, l) != -1)
            if(write(fd, buffer, l) == l)
            {
                close(fd);
                C.modified = 0;
                free(buffer);
                setStatMssg("%d bytes written to disk", l);
                return;
            }
        close(fd);
    }
    free(buffer);
    setStatMssg("Can't save! I/O error: %s", strerror(errno));
}
struct abuf
{
    char *b;
    int len;
};

void abAppend(struct abuf *ab, const char *s, int len)
{
    char *new = realloc(ab -> b, ab -> len + len);
    if(new == NULL)
        return;
    memcpy(&new[ab -> len], s, len);
    ab -> b = new;
    ab -> len += len;
}

void abFree(struct abuf *ab)
{
    free(ab -> b);
}

void moveCursor(int key)
{
    erow *row = (C.y >= C.numrows) ? NULL : &C.row[C.y];
    switch (key)
    {
        case ARROW_UP:      if(C.y != 0)
                                C.y--;
                            break;
        case ARROW_LEFT:    if(C.x != 0)
                                C.x--;
                            else if(C.y > 0)
                            {
                                C.y--;
                                C.x = C.row[C.y].size;
                            }
                            break;
        case ARROW_DOWN:    if(C.y < C.numrows)
                                C.y++;
                            break;
        case ARROW_RIGHT:   if(row && C.x < row->size)
                                C.x++;
                            else if(row && C.x == row->size)
                            {
                                C.y++;
                                C.x = 0;
                            }
                            break;
    }
    row = (C.y >= C.numrows) ? NULL : &C.row[C.y];
    int rowlen = row ? row->size : 0;
    if(C.x > rowlen)
        C.x = rowlen;
}
void processKey()
{
    static int quit_times = FORCE_QUIT;
    int c = readKey();
    switch(c)
    {
        case '\r':          insertNewLine();
                            break;
        case CTRL_KEY('q'): if(C.modified && quit_times > 0)
                            {
                                setStatMssg("!!File has UNSAVED changes!! Press Ctrl-Q %d more times to force quit without saving changes.", quit_times);
                                quit_times--;
                                return;
                            }
                            write(STDOUT_FILENO, "\x1b[2J", 4);
                            write(STDOUT_FILENO, "\x1b[H", 3);
                            exit(0);
                            break;
        case CTRL_KEY('s'): fileSave();
                            break;
        case HOME:          C.x = 0;
                            break;
        case END:           if(C.y < C.numrows)
                                C.x = C.row[C.y].size;
                            break;
        case BACKSPACE:
        case CTRL_KEY('h'):
        case DEL:           if(c == DEL)
                                moveCursor(ARROW_RIGHT);
                            deleteChar();
                            break;
        case PAGE_UP:
        case PAGE_DOWN:     {
                                if(c == PAGE_UP)
                                    C.y = C.rowoffset;
                                else if(c == PAGE_DOWN)
                                {    
                                    C.y = C.rowoffset + C.rows - 1;
                                    if(C.y > C.numrows)
                                        C.y = C.numrows;    
                                }
                                int times = C.rows;
                                while(times--)
                                    moveCursor(c == PAGE_UP ? ARROW_UP : ARROW_DOWN);
                                break;
                            }
        case ARROW_UP:
        case ARROW_LEFT:
        case ARROW_DOWN:
        case ARROW_RIGHT:   moveCursor(c);
                            break;
        case CTRL_KEY('l'):
        case '\x1b':        break;
        default:            insertChar(c);
                            break;
    }
    quit_times = FORCE_QUIT;
}
void scroll()
{
    C.rx = 0;
    if(C.y < C.numrows)
        C.rx = rowXtoRx(&C.row[C.y], C.x);
    //if cursor is above visible window, scroll up to cursor location
    if(C.y < C.rowoffset)
        C.rowoffset = C.y;
    //if cursor is past the bottom of visible window, add a new line to the screen from the bottom
    if(C.y >= C.rowoffset + C.rows)
        C.rowoffset = C.y - C.rows + 1;
    
    //if cursor is within visible window, scroll to cursor location
    if(C.rx < C.columnoffset)
        C.columnoffset = C.rx;
    //if cursor is past the visible window to the right, add a new column to the screen from the right
    if(C.rx >= C.columnoffset + C.cols)
        C.columnoffset = C.rx - C.cols + 1;
}
void drawRows(struct abuf *ab)
{
    int i;
    for (i = 0; i < C.rows; i++)
    {
        int filerow = i + C.rowoffset;
        if (filerow >= C.numrows)
        {
            if(C.numrows == 0 && i == C.rows/3)
            {
                char welcome[100];
                    int welcomelen = snprintf(welcome, sizeof(welcome), "Atlas editor -- version %s", VERSION);
                if(welcomelen > C.cols)
                    welcomelen = C.cols;
                int padding = (C.cols - welcomelen)/2;
                if (padding)
                {
                    abAppend(ab, "~", 1);
                    padding--;
                }
                while (padding--)
                    abAppend(ab, " ", 1);
                abAppend(ab, welcome, welcomelen);
            }
            else
                abAppend(ab, "~", 1);
        }
        else
        {
            int len = C.row[filerow].rsize - C.columnoffset;
            if(len < 0)
                len = 0;
            abAppend(ab, &C.row[filerow].render[C.columnoffset], len);
        }
        abAppend(ab, "\x1b[K", 3);
        abAppend(ab, "\r\n", 2);
    }
}
void drawStatusBar(struct abuf *ab)
{
    abAppend(ab, "\x1b[7m", 4);
    char status[100], rstatus[100];
    int len = snprintf(status, sizeof(status), "%.20s - %d lines %s", C.filename ? C.filename : "[No Name]", C.numrows, C.modified ? "(modified)" : "");
    int rlen = snprintf(rstatus, sizeof(rstatus), "%d/%d", C.y + 1, C.numrows);
    if(len > C.cols)
        len = C.cols;
    abAppend(ab, status, len);
    while(len < C.cols)
    {
        if(C.cols - len == rlen)
        {
            abAppend(ab, status, len);
            break;
        }
        else
        {
            abAppend(ab, " ", 1);
            len++;
        }
    }
    abAppend(ab, "\x1b[m", 3);
    abAppend(ab, "\r\n", 2);
}
void drawMssgBar(struct abuf *ab)
{
    abAppend(ab, "\x1b[K", 3);
    int msglen = strlen(C.statmssg);
    if(msglen > C.cols)
        msglen = C.cols;
    if(msglen && time(NULL) - C.statmssg_time < 5)
        abAppend(ab, C.statmssg, msglen);
}
void refresh()
{
    scroll();
    struct abuf ab = ABUF_INIT;
    abAppend(&ab, "\x1b[?25l", 6); //hide cursor when repainting
    abAppend(&ab, "\x1b[H", 3);
    drawRows(&ab);
    drawStatusBar(&ab);
    drawMssgBar(&ab);

    char buf[32];
    snprintf(buf, sizeof(buf), "\x1b[%d;%dH", (C.y - C.rowoffset) + 1, (C.rx - C.columnoffset) + 1);
    abAppend(&ab, buf, strlen(buf));

    //abAppend(&ab, "\x1b[H", 3);
    abAppend(&ab, "\x1b[?25h", 6); //show cursor
    write(STDOUT_FILENO, ab.b, ab.len);
    abFree(&ab);
}
void setStatMssg(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(C.statmssg, sizeof(C.statmssg), fmt, ap);
    va_end(ap);
    C.statmssg_time = time(NULL);
}
void init()
{
    C.x = 0;
    C.y = 0;
    C.rx = 0;
    C.numrows = 0;
    C.rowoffset = 0;
    C.columnoffset = 0;
    C.row = NULL;
    C.modified = 0;
    C.filename = NULL;
    C.statmssg[0] = '\0';
    C.statmssg_time = 0;
    if(getWindowSize(&C.rows, &C.cols) == -1)
        kill("getWindowSize");
    C.rows -= 2;
}
void main(int argc, char *argv[])
{
    allowRaw();
    init();
    if(argc >= 2)
        fileOpen(argv[1]);
    setStatMssg("HELP: Ctrl-S | Ctrl-Q = quit");
    while (1)
    {    
        refresh();
        processKey();
    }
}
