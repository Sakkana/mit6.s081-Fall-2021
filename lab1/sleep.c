#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"   // int atoi(const char*);

#define SDTIN_DILENO 0
#define SDTOUT_DILENO 1
#define SDTERR_DILENO 2

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(SDTOUT_DILENO, "ERROR: please follow the format: sleep <number>\n");
        exit(1);
    }
    int duration = atoi(argv[1]);
    sleep(duration);
    exit(0); 
}