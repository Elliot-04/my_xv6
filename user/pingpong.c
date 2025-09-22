#include "kernel/types.h"
#include "user/user.h"

int main(int argc, char *argv[]) {
    // 定义两个管道的文件描述符
    int f2c[2];
    int c2f[2];
  
    // 创建管道
    pipe(f2c);
    pipe(c2f);

    // 创建子进程
    int pid = fork();

    if (pid < 0) {
        fprintf(2, "fork failed\n");
        exit(1);
    }

    // 子进程
    if (pid == 0) {
        close(f2c[1]); 
        close(c2f[0]);

        int parent_pid;
        int my_pid = getpid();

        // 从管道读取数据
        if (read(f2c[0], &parent_pid, sizeof(parent_pid)) != sizeof(parent_pid)) {
            fprintf(2, "child: read from parent failed\n");
            exit(1);
        }
        
        printf("%d: received ping from pid %d\n", my_pid, parent_pid); 

        // 向管道写入数据
        if (write(c2f[1], &my_pid, sizeof(my_pid)) != sizeof(my_pid)) { 
            fprintf(2, "child: write to parent failed\n");
            exit(1);
        }

        close(f2c[0]);
        close(c2f[1]);
        exit(0);

    } else {
        // 父进程
        close(f2c[0]);
        close(c2f[1]); 

        int child_pid_from_pipe;
        int my_pid = getpid();

        // 向管道写入数据
        if (write(f2c[1], &my_pid, sizeof(my_pid)) != sizeof(my_pid)) { 
            fprintf(2, "parent: write to child failed\n");
            exit(1);
        }
        
        // 从管道读取数据
        if (read(c2f[0], &child_pid_from_pipe, sizeof(child_pid_from_pipe)) != sizeof(child_pid_from_pipe)) {
            fprintf(2, "parent: read from child failed\n");
            exit(1);
        }

        printf("%d: received pong from pid %d\n", my_pid, child_pid_from_pipe);

        // 等待子进程结束并清理
        wait(0);

        close(f2c[1]);
        close(c2f[0]);
        exit(0);
    }
}