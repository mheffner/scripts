#include <stdio.h>
#include <ctype.h>

void
do_run(const char *name, int (*func)(int))
{
	int i;

	printf("=== BEGIN: %s ====\n", name);

	for (i = 0; i < 255; i++)
		if (func(i))
			printf("%c", (char)i);

	printf("\n=== END: %s ====\n", name);

}

int main()
{
#define X(n)	do_run(#n, n)

	X(isalnum);
	X(isalpha);
	//X(isascii);
	X(isdigit);
	X(isgraph);
	X(islower);
	X(isprint);
	X(ispunct);
	X(isspace);
	X(isupper);

#undef X

	return 0;
}
