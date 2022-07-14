#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

void linear_pipe(int read_fd) {
    
    // 打印管道中的第一个质数
    int num;
    read(read_fd, &num, sizeof(int));
    printf("prime %d\n", num);

    int fds[2];
    pipe(fds);
    int new_num = -1;
    while (read(read_fd, &new_num, sizeof(int))) {
        // 筛质数
        if (new_num % num) {
            write(fds[1], &new_num, sizeof(int));   // write current prime to the pipe (assuming one)
        }
    }

    if (new_num == -1) {
        close(read_fd);
        close(fds[0]);
        close(fds[1]);
        exit(0);
    }

    int pid = fork();
    if (pid < 0) {
        printf("fork failed!\n");
        exit(-1);
    }

    if (pid == 0) {
        close(read_fd);

        close(fds[1]);
        linear_pipe(fds[0]);    // 父进程创建的pipe read 端文件描述符
        close(fds[0]);
    } else {
        // fork 之后父子进程虽然共享文件描述符表，但是大家是独立的，只不过同一个 fd 引用同一个文件罢了
        close(read_fd);

        close(fds[0]);
        close(fds[1]);
        wait(0);
    }
}

int main() {

    int fds[2]; // 主进程创建一个管道
    pipe(fds);

    for (int i = 2; i <= 35; ++ i) {
        int num = i, *num_add = &num;
        write(fds[1], num_add, sizeof(int));
    }

    close(fds[1]);  // 关闭 write 端

    linear_pipe(fds[0]);

    close(fds[0]);  // 关闭 read 端

    return 0;
}