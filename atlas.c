#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <termios.h>
#include <sys/ioctl.h>

#define VERSION "1.0"
#define CTRL_KEY(k) ((k) & 0x1f)
#define ABUF_INIT {NULL, 0}
enum key
{
    ARROW_UP,
    ARROW_LEFT = 1000,
    ARROW_DOWN,
    ARROW_RIGHT
};
struct config 
{
    int x, y;
    int rows;
    int cols;
    struct termios orig_termios;
};
struct config C;
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
            switch (seq[1])
            {
                case 'A': return ARROW_UP;
                case 'B': return ARROW_DOWN;
                case 'C': return ARROW_RIGHT;
                case 'D': return ARROW_LEFT;
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
    switch (key)
    {
        case ARROW_UP:      C.y--;
                            break;
        case ARROW_LEFT:    C.x--;
                            break;
        case ARROW_DOWN:    C.y++;
                            break;
        case ARROW_RIGHT:   C.x++;
                            break;
    }
}
void processKey()
{
    int c = readKey();
    switch(c)
    {
        case CTRL_KEY('q'): write(STDOUT_FILENO, "\x1b[2J", 4);
                            write(STDOUT_FILENO, "\x1b[H", 3);
                            exit(0);
                            break;
        case ARROW_UP:
        case ARROW_LEFT:
        case ARROW_DOWN:
        case ARROW_RIGHT:   moveCursor(c);
                            break;
    }
}
void drawRows(struct abuf *ab)
{
    int i;
    for (i = 0; i < C.rows; i++)
    {
        if(i == C.rows/3)
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
        abAppend(ab, "\x2b[K", 3);
        if (i < C.rows - 1)
            abAppend(ab, "\r\n", 2);
    }
}
void refresh()
{
    struct abuf ab = ABUF_INIT;
    abAppend(&ab, "\x1b[?25l", 6); //hide cursor when repainting
    abAppend(&ab, "\x1b[H", 3);
    drawRows(&ab);

    char buf[32];
    snprintf(buf, sizeof(buf), "\x1b[%d;%dH", C.y + 1, C.x + 1);
    abAppend(&ab, buf, strlen(buf));

    //abAppend(&ab, "\x1b[H", 3);
    abAppend(&ab, "\x1b[?25h", 6); //show cursor
    write(STDOUT_FILENO, ab.b, ab.len);
    abFree(&ab);
}
void init()
{
    C.x = 0;
    C.y = 0;
    if(getWindowSize(&C.rows, &C.cols) == -1)
        kill("getWindowSize");
}
void main()
{
    allowRaw();
    while (1)
    {    
        refresh();
        processKey();
    }
}
