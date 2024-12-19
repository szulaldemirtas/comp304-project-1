#include <stdio.h>

int main(int argc, char *argv[]) {
    printf("Hello, I am a custom command!\n");

    if (argc > 1) {
        printf("Arguments passed:\n");
        for (int i = 1; i < argc; i++) {
            printf("  Arg %d: %s\n", i, argv[i]);
        }
    }
    return 0;
}
