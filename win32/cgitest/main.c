#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int
main(int argc, char **argv)
{
    int i;
    char buf[8192];

    if (strstr(argv[0], "nph-") != NULL) {
	printf("HTTP/1.0 200 OK\r\nServer: %s\r\n", argv[0]);
    }
    printf("Content-type: text/plain\r\n\r\n");
    puts("\nArgs:");
    for (i = 0; i < argc; ++i) {
	puts(argv[i]);
    }

    puts("\nEnvironment:");
    for (i = 0; _environ[i] != NULL; ++i) {
	printf("%s\n", _environ[i]);
    }

    puts("\nContent:");
    while ((i = fread(buf, 1, sizeof(buf), stdin)) > 0) {
	fwrite(buf, 1, i, stdout);
    }
    return 0;
}