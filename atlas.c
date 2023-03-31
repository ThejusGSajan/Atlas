#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <termios.h>

struct termios orig_termios;
void disableRaw()
{
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
}
void allowRaw()
{
    tcgetattr(STDIN_FILENO, &orig_termios);
    atexit(disableRaw);
    struct termios raw = orig_termios;
    raw.c_lflag &= ~(ECHO);
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
}
void main()
{
    allowRaw();
    char c;
    while(read(STDIN_FILENO, &c, 1) == 1 && c != 'q');
}
