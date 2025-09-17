// Create a zombie process that
// must be reparented at exit.

#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int
main(void)
{
  if(fork() > 0) {
    printf("papa %d is going for milk\n", getpid());
    sleep(50);  // Let child exit before parent.
    printf("papa is here\n");
  }
  else {
    for (int i=0;i<5;i++) {
      printf("im child %d\n", getpid());
    }
  }
  ps();
  exit(0);
}
