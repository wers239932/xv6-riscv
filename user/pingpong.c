#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int main(int argc, char *argv[]) {
    int pipe_fd[2];
    char buffer[16];
    int pid;
    
    if (pipe(pipe_fd) < 0) {
        fprintf(2, "pipe failed\n");
        exit(1);
    }
    
    pid = fork();
    if (pid < 0) {
        fprintf(2, "fork failed\n");
        exit(1);
    }
    
    if (pid == 0) { // Дочерний процесс
        // Читаем от родителя
        read(pipe_fd[0], buffer, sizeof(buffer));
        printf("%d: got %s\n", getpid(), buffer);
        
        // Пишем ответ родителю
        write(pipe_fd[1], "pong", 4);
        
        close(pipe_fd[0]);
        close(pipe_fd[1]);
        exit(0);
        
    } else { // Родительский процесс
        // Пишем ребенку
        write(pipe_fd[1], "ping", 4);
        
        // Ждем завершения записи ребенком
        wait(0);
        
        // Читаем ответ от ребенка
        read(pipe_fd[0], buffer, sizeof(buffer));
        printf("%d: got %s\n", getpid(), buffer);
        
        close(pipe_fd[0]);
        close(pipe_fd[1]);
        exit(0);
    }
}