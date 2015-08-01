#include "amiga_hunk_parser.h"
#include <stdio.h>

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

int main(int argc, const char** argv)
{
    if (argc < 2)
    {
        printf("Usage <tool> <amiga executable>\n");
        return 0;
    }

    ahp_parse_file(argv[1]);

    return 0;
}
