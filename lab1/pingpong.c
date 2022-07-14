#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

// pingpong
// interaction between parent-child processes

int main() {
    int p_c_fds[2], c_p_fds[2];
    pipe(p_c_fds);
    pipe(c_p_fds);

    char buf[16];

    int pid = fork();
    if (pid < 0) {
        fprintf(2, "fork failed!\n");
        exit(1);
    } else if (pid == 0) {
        close(p_c_fds[1]);
        close(c_p_fds[0]);

        // child read 
        read (p_c_fds[0], buf, sizeof(buf));
        close(p_c_fds[0]);
        printf("%d: received %s\n", getpid(), buf); // 他必须按照格式罢了

        // child write
        write(c_p_fds[1], "pong", 4);
        close(c_p_fds[1]);
    } else {
        close(p_c_fds[0]);
        close(c_p_fds[1]);

        // parent write
        write(p_c_fds[1], "ping", 4);
        close(p_c_fds[1]);

        // parent read 
        read (c_p_fds[0], buf, sizeof(buf));
        close(c_p_fds[0]);
        printf("%d: received %s\n", getpid(), buf);
    }

    exit(0);
}