#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <termios.h>
#include <sys/ioctl.h>

#define CTRL_KEY(k) ((k) & 0x1f)
struct config 
{
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
char readKey()
{
    int nread;
    char c;
    while((nread = read(STDIN_FILENO, &c, 1)) != 1)
    {
        if(nread == -1 && errno != EAGAIN)
            kill("read");
    }
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
void processKey()
{
    char c = readKey();
    switch(c)
    {
        case CTRL_KEY('q'): write(STDOUT_FILENO, "\x1b[2J", 4);
                            write(STDOUT_FILENO, "\x1b[H", 3);
                            exit(0);
                            break;
    }
}
void drawRows()
{
    int i;
    for (i = 0; i < C.rows; i++)
    {    
        write(STDOUT_FILENO, "~", 1);
        if (i < C.rows - 1)
            write(STDOUT_FILENO, "\r\n", 2);
    }
}
void refresh()
{
    write(STDOUT_FILENO, "\x1b[2J", 4);
    write(STDOUT_FILENO, "\x1b[H", 3);
    drawRows();
    write(STDOUT_FILENO, "\x1b[H", 3);
}
void init()
{
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
