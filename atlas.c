#include <stdio.h>
#include <unistd.h>
void main()
{
    char c;
    while(read(STDIN_FILENO, &c, 1) == 1);
}
