#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"

char *fmtname(char *path)
{
  static char buf[DIRSIZ+1];
  char *p;

  // Find first character after last slash.
  for(p=path+strlen(path); p >= path && *p != '/'; p--)
    ;
  p++;

  // Return blank-padded name.
  if(strlen(p) >= DIRSIZ)
    return p;
  memmove(buf, p, strlen(p));
  memset(buf+strlen(p), ' ', DIRSIZ-strlen(p));
  return buf;
}

//printf("%s %d %d %l\n", fmtname(path), st.type, st.ino, st.size);

int find(char *path, char *filename)
{
    int cur_success = 0;
    char buf[512], *p;
    int fd;
    struct dirent de;
    struct stat st;

    if((fd = open(path, 0)) < 0){
        fprintf(2, "find: cannot open %s\n", path);
        return 0;
    }

    if(fstat(fd, &st) < 0){
        fprintf(2, "find: cannot stat %s\n", path);
        close(fd);
        return 0;
    }

    if(strlen(path) + 1 + DIRSIZ + 1 > sizeof buf){
        printf("find: path too long\n");
    }

    // 如果当前目录是 dir_a，那么下面三句话后 p = "dir_a/"
    strcpy(buf, path);
    p = buf+strlen(buf);
    *p++ = '/';

    // 遍历当前目录
    while(read(fd, &de, sizeof(de)) == sizeof(de)){
        if(de.inum == 0)
            continue;
        
        // 不递归遍历本目录和上级目录
        if (!strcmp(de.name, ".") || !strcmp(de.name, "..")) {
            continue;
        }

        // 拼接当前路径
        memmove(p, de.name, DIRSIZ);
        *(p + DIRSIZ) = 0;

        // 获得当前文件状态
        if(stat(buf, &st) < 0){
            printf("find: cannot stat %s\n", buf);
            continue;
        }

        // 判断当前是目录还是文件
        if (st.type == T_DIR) {
            find(buf, filename);
        } else if (st.type == T_FILE) {
            if (!strcmp(de.name, filename)) {
                cur_success = 1;
                printf("%s\n", buf);
            }
        }
    }

    close(fd);
    return cur_success;
}

int main(int argc, char *argv[])
{
    // 无参数，报错
    if (argc == 1 || argc > 3) {
        printf("ERROR: please follow the format find (DIR) <FILE>  ...\n");
        exit(1);
    }

    // 一个参数，当前目录下寻找
    int success = 0;
    if (argc == 2) {
        success = find(".", argv[1]);
    } else if (argc == 3) {
        success = find(argv[1], argv[2]);
    }

    if (!success) {
        printf("No such file...\n");
    }

    exit(0);
}
