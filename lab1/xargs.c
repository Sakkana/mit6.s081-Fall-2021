#include "kernel/types.h"
#include "kernel/param.h"
#include "user/user.h"

#define STDIN 0
#define STDOUT 1
#define STDERR 2

/*
管道实现的是将前面的stdout作为后面的stdin
但是有些命令不接受管道的传递方式，最常见的就是ls命令
有些时候命令希望管道传递的是参数，但是直接用管道有时无法传递到命令的参数位
这时候需要xargs，xargs实现的是将管道传输过来的stdin进行处理然后传递到命令的参数位上
也就是说xargs完成了两个行为：处理管道传输过来的stdin；将处理后的传递到正确的位置上。

such as :
$ ls
这会在 stdout 打印当前目录下的文件和目录
$ echo .. | ls
这仍然打印当前目录，虽然我们的理想是打印 ls ..，即上层目录
但是管道将 echo 的 stdout 作为 ls 的 stdin，然而 ls 不接受管道的传递方式
$ echo .. | xargs ls ..
这时候接可以看到上层目录的东西了
因为 xargs 将关东传输过来的 stdin 转换为了命令行参数 argv 传递给 ls
*/

int main(int argc, char **argv) {

    char *exec_argv[MAXARG];

    // 首先获取 xargs 自己的参数
    int idx = 0;
    for (int i = 1; i < argc; ++ i) {
        exec_argv[idx ++] = argv[i];    // 下标 0 ~ argc-2
    }

    /*  这里必须要循环读取
        因为有个案例是 find . b | xargs grep hello
    */
    while (1) {
        
        // 再从管道中获取上一级的 stdout
        char buf[128];
        int len = -1;
        // 逐字节读取
        idx = 0;
        while ((len = read(STDIN, buf + idx, 1)) > 0) {
            if (buf[idx] == '\n') {
                buf[idx] = '\0';
                break;
            }
            ++ idx;
        }

        if(idx == 0 && len == 0) {
            break;
        }
        
        // 将 stdin 参数拼接到 exec_argv 后面
        exec_argv[argc - 1] = buf;

        int pid = fork();
        if(pid < 0) {
            printf("xargs_fork failed!\n");
            exit(1);
        } else if (pid == 0) {
            exec(argv[1], exec_argv);
            printf("xargs_exec failed!\n");
        } else {
            wait(0);
        }
    }
    
    exit(0);
}