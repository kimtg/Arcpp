#include "arc.h"

void print_logo() {
	printf("Arc++ %s\n", arc::VERSION);
}

int main(int argc, char **argv)
{
	if (argc == 1) { /* REPL */
		print_logo();
		arc::arc_init();
		arc::repl();
		puts("");
		return 0;
	}
	else if (argc == 2) {
		char *opt = argv[1];
		if (strcmp(opt, "-h") == 0) {
			puts("Usage: arcadia [OPTIONS...] [FILES...]");
			puts("");
			puts("OPTIONS:");
			puts("    -h    print this screen.");
			puts("    -v    print version.");
			return 0;
		}
		else if (strcmp(opt, "-v") == 0) {
			puts(arc::VERSION);
			return 0;
		}
	}

	/* execute files */
	arc::arc_init();
	int i;
	arc::error err;
	for (i = 1; i < argc; i++) {
		err = arc::arc_load_file(argv[i]);
		if (err) {
			fprintf(stderr, "In file %s:\n", argv[i]);
			print_error(err);
			break;
		}
	}
	return 0;
}
