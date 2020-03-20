#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int main(int argc, char** argv)
{
    int cid;
    close(0);
    close(1);
    close(2);
    cid = fork();
    if(cid != 0)
    {
        printf("Create child %d and quit.\n",cid);
        return 0;
    }
    setsid();
    system(argv[1]);
    return 0;
}

