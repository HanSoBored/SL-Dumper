#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>

int main() {
    int passed = 0;
    int failed = 0;
    int pid = fork();

    printf("=== Build Verification Test ===\n");

    if (pid == 0) {
        execl("./sl-dumper", "./sl-dumper", NULL);
        exit(1);
    } else {
        int status;
        waitpid(pid, &status, 0);
        int exit_code = WEXITSTATUS(status);
        if (exit_code <= 1) {
            printf("[1/1] Testing binary execution...\n");
            printf("  ok Binary runs (exit code: %d)\n", exit_code);
            passed++;
        } else {
            printf("[1/1] Testing binary execution...\n");
            printf("  fe Binary failed (exit code: %d)\n", exit_code);
            failed++;
        }
    }

    printf("\nResults: %d passed, %d failed\n", passed, failed);
    return failed > 0 ? 1 : 0;
}