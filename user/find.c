#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"

void find(char *path, const char *filename) {
    char buf[512], *p;
    int fd;
    struct dirent de;
    struct stat st;

    // 打开路径，获取文件描述符
    if ((fd = open(path, 0)) < 0) {
        fprintf(2, "find: cannot open %s\n", path);
        return;
    }

    // 通过文件描述符，获取文件/目录的状态信息
    if (fstat(fd, &st) < 0) {
        fprintf(2, "find: cannot stat %s\n", path);
        close(fd);
        return;
    }

    // 只处理目录类型
    if (st.type != T_DIR) {
        fprintf(2, "find: %s is not a directory\n", path);
        close(fd);
        return;
    }

    // 确保路径拼接不会溢出
    if (strlen(path) + 1 + DIRSIZ + 1 > sizeof(buf)) {
        printf("find: path too long\n");
        close(fd);
        return;
    }
  
    // 遍历目录中的所有条目
    strcpy(buf, path);
    p = buf + strlen(buf);
    *p++ = '/';  // 在路径末尾加上'/'，为拼接文件名做准备

    while (read(fd, &de, sizeof(de)) == sizeof(de)) {
        // 无效/空闲的目录项
        if (de.inum == 0) continue;

        // 将当前目录项的文件名拼接到路径末尾
        memmove(p, de.name, DIRSIZ);
        p[DIRSIZ] = 0; 

        // 获取拼接后完整路径的状态信息
        if (stat(buf, &st) < 0) {
            printf("find: cannot stat %s\n", buf);
            continue;
        }

        // 根据文件类型进行处理
        switch (st.type) {
            case T_FILE:
                // 如果是文件类型，检查文件名是否匹配
                if (strcmp(de.name, filename) == 0) {
                    printf("%s\n", buf);
                }
                break;

            case T_DIR:
                // 如果是目录类型，首先检查文件名是否匹配
                if (strcmp(de.name, filename) == 0) {
                    printf("%s\n", buf);
                }
                
                // 递归查找，但要跳过 '.' 和 '..' 目录，防止无限循环
                if (strcmp(de.name, ".") != 0 && strcmp(de.name, "..") != 0) {
                    find(buf, filename);
                }
                break;
        }
    }

    close(fd);
}

int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(2, "Usage: find <directory> <filename>\n");
        exit(1);
    }
    find(argv[1], argv[2]);
    exit(0);
}