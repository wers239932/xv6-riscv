// Create a zombie process that
// must be reparented at exit.

#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int
main(void)
{
int i;

    for (i = 0; i < 3; i++) {
        int pid = fork();
        if (pid < 0) {
            printf("fork failed\n");
            exit(1);
        }
        if (pid == 0) {
            // ребёнок сразу завершает работу, превращаясь в зомби
            exit(i + 1);
        }
        // родитель идёт дальше и не ждёт детей сразу
    }

    // Родитель даёт немного времени, чтобы процессы стали зомби
    sleep(10);

    // Проверяем список процессов
    printf("Parent process checking ps:\n");
    ps();

    // Можно собрать их через wait
    for (i = 0; i < 3; i++) {
        int status;
        int pid = wait(&status);
        printf("Collected child pid=%d, status=%d\n", pid, status);
    }

    exit(0);

}
