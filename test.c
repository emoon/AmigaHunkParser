#include "amiga_hunk_parser.h"
#include <stdio.h>

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

int main(int argc, const char** argv)
{
	AHPInfo* info;

    if (argc < 2)
    {
        printf("Usage: %s <amiga executable>\n\n", argv[0]);
        return 0;
    }

    if (!(info = ahp_parse_file(argv[1])))
    	return 0;

    ahp_print_info(info, 1);

    ahp_free(info);

    return 0;
}
