#include "endian.h"
#include "doshunks.h"
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

const char* hunktype[HUNK_ABSRELOC16 - HUNK_UNIT + 1] = {
    "UNIT", "NAME", "CODE", "DATA", "BSS ", "RELOC32", "RELOC16", "RELOC8",
    "EXT", "SYMBOL", "DEBUG", "END", "HEADER", "", "OVERLAY", "BREAK",
    "DREL32", "DREL16", "DREL8", "LIB", "INDEX",
    "RELOC32SHORT", "RELRELOC32", "ABSRELOC16"
};

#define HUNKF_MASK (HUNKF_FAST | HUNKF_CHIP)
#define NUM_RELOC_CONTEXTS 256

typedef struct HunkInfo
{
    unsigned type;        // HUNK_<type>
    unsigned flags;       // HUNKF_<flag>
    int memsize, datasize; // longwords
    int datastart;        // longword index in file
    int relocstart;       // longword index in file
    int relocentries;     // no. of entries
} HunkInfo;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

inline uint16_t swap_uint16(uint16_t val)
{
    return (val << 8) | (val >> 8);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static inline uint32_t swap_uint32(uint32_t val)
{
    return ((val & 0x000000ff) << 24) |
           ((val & 0x0000ff00) << 8) |
           ((val & 0x00ff0000) >> 8) |
           ((val & 0xff000000) >> 24);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

uint32_t get_u32 (const void* t, int index)
{
    uint32_t* mem = (uint32_t*)(((uint8_t*)t) + index);
#if defined(AHP_LITTLE_ENDIAN)
    return swap_uint32(*mem);
#endif
    return *mem;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

uint32_t get_u32_inc (const void* t, int* index)
{
    uint32_t* mem = (uint32_t*)(((uint8_t*)t) + *index);
    *index += 4;
#if defined(AHP_LITTLE_ENDIAN)
    return swap_uint32(*mem);
#endif
    return *mem;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void* loadToMemory(const char* filename, size_t* size)
{
    FILE* f = fopen(filename, "rb");
    void* data = 0;
    size_t s = 0, t = 0;

    *size = 0;

    if (!f)
        return 0;

    fseek(f, 0, SEEK_END);
    long ts = ftell(f);

    if (ts < 0)
        goto end;

    s = (size_t)ts;

    data = malloc(s);

    if (!data)
        goto end;

    fseek(f, 0, SEEK_SET);

    t = fread(data, s, 1, f);
    (void)t;

    *size = s;

    end:

    fclose(f);

    return data;
}


///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

int aph_parse_file(const char* filename)
{
    size_t size = 0;
    void* data = loadToMemory(filename, &size);
    uint32_t header = 0;
    int index = 0;
    HunkInfo* hunks = 0;

    if (!data)
    {
        printf("Unable to open %s\n", filename);
        return 1;
    }

    if (get_u32_inc(data, &index) != HUNK_HEADER)
    {
        printf("HunkHeader is incorrect (should be 0x%08x but is 0x%08x)\n", HUNK_HEADER, header);
        return 1;
    }

    while (get_u32_inc(data, &index))
    {
        index += get_u32(data, index) * 4;
        if (index >= size)
        {
            printf("Bad hunk header!\n");
            return 1;
        }
    }

    int numhunks = get_u32_inc(data, &index);
    if (numhunks == 0)
    {
        printf("No hunks!\n");
        return 1;
    }

    hunks = (HunkInfo*)malloc(numhunks * sizeof(HunkInfo));
    memset(hunks, 0, numhunks * sizeof(HunkInfo));

    if (get_u32_inc(data, &index) != 0 || get_u32_inc(data, &index) != numhunks - 1)
    {
        printf("Unsupported hunk load limits!\n");
        return 1;
    }

    // read hunk sizess

    for (int h = 0; h < numhunks; h++)
    {
        hunks[h].memsize = (get_u32(data, index) & 0x0fffffff) * 4;
        switch (hunks[h].flags = get_u32(data, index) & 0xf0000000)
        {
            case 0:
            case HUNKF_CHIP:
            case HUNKF_FAST:
                break;
            default:
                printf("Illegal hunk flags!\n");
                return 1;
        }
        index += 4;
    }

    // Parse hunks
    printf("Hunk  Mem  Type  Mem size  Data size  Data sum  Relocs\n");
    for (int h = 0, nh = 0; h < numhunks;)
    {
        unsigned flags = hunks[h].flags, type;
        int hunk_length, symlen, n_symbols;
        int lh = h;
        printf("%4d  %s ", h, flags == HUNKF_CHIP ? "CHIP" : flags == HUNKF_FAST ? "FAST" : "ANY ");
        int missing_relocs = 0;
        const char* note = "";

        while (lh == h)
        {
            if (index >= size)
            {
                printf("\nUnexpected end of file!\n");
                return 1;
            }

            type = get_u32_inc(data, &index) & 0x0fffffff;

            if (index >= size && type != HUNK_END)
            {
                printf("\nUnexpected end of file!\n");
                return 1;
            }

            if (missing_relocs && type != HUNK_RELOC32)
            {
                printf("        %s\n", note);
                note = "";
                missing_relocs = 0;
            }

            switch (type)
            {
                case HUNK_UNIT:
                case HUNK_NAME:
                case HUNK_DEBUG:
				{
                    printf("           %s (skipped)\n", hunktype[type - HUNK_UNIT]);
                    hunk_length = get_u32_inc(data, &index) * 4;
                    index += hunk_length;
                    break;
				}

                case HUNK_SYMBOL:
				{
                    n_symbols = 0;
                    symlen = get_u32_inc(data, &index) * 4;
                    while (symlen > 0)
                    {
                        n_symbols++;
                        index += symlen + 4;
                        symlen = get_u32_inc(data, &index) * 4;
                    }
                    printf("           SYMBOL (%d entries)\n", n_symbols);
                    break;
				}
                case HUNK_CODE:
                case HUNK_DATA:
                case HUNK_BSS:
				{
                    if (nh > h)
                    {
                        h = nh;
                        index -= 4;
                        break;
                    }

                    hunks[h].type = type;
                    hunks[h].datasize = get_u32_inc(data, &index) * 4;

                    printf("%4s%10x %10x", hunktype[type - HUNK_UNIT], hunks[h].memsize, hunks[h].datasize);

                    if (type != HUNK_BSS)
                    {
                        hunks[h].datastart = index;
                        index += hunks[h].datasize;

                        if (hunks[h].datasize > 0)
                        {
                            int sum = 0;
                            for (int pos = hunks[h].datastart; pos < hunks[h].datastart + hunks[h].datasize; pos++)
                            {
                                sum += get_u32(data, pos);
                            }

                            printf("  %08x", sum);
                        }
                        else
                        {
                            printf("          ");
                        }
                    }
                    if (hunks[h].datasize > hunks[h].memsize)
                    {
                        note = "  Hunk size overflow corrected!";
                        hunks[h].memsize = hunks[h].datasize;
                    }
                    nh = h + 1;
                    missing_relocs = 1;
                    break;
				}

                case HUNK_RELOC32:
				{
                    hunks[h].relocstart = index;
                    {
                        int n, tot = 0;
                        while ((n = get_u32_inc(data, &index)) != 0)
                        {
                            uint32_t t = get_u32_inc(data, &index);
                            if (n < 0 || index + n + 2 >= size || t >= numhunks)
                            {
                                printf("\nError in reloc table!, %d - %d, %d - %d \n", index + n + 2, (int)size, t, numhunks);
                                return 1;
                            }
                            tot += n;
                            while (n--)
                            {
                                if ((get_u32_inc(data, &index)) > hunks[h].memsize - 4)
                                {
                                    printf("\nError in reloc table!\n");
                                    return 1;
                                }
                            }
                        }

                        hunks[h].relocentries = tot;
                        printf("  %6d%s\n", tot, note);
                        note = "";
                        missing_relocs = 0;
                    }
                    break;
				}
                case HUNK_END:
				{
                    if (hunks[h].type == 0)
                    {
                        printf("Empty%9d\n", hunks[h].memsize);
                        return 1;
                    }
                    h = h + 1; nh = h;
                    break;
				}

                case HUNK_RELOC16:
                case HUNK_RELOC8:
                case HUNK_EXT:
                case HUNK_HEADER:
                case HUNK_OVERLAY:
                case HUNK_BREAK:
                case HUNK_DREL32:
                case HUNK_DREL16:
                case HUNK_DREL8:
                case HUNK_LIB:
                case HUNK_INDEX:
                case HUNK_RELOC32SHORT:
                case HUNK_RELRELOC32:
                case HUNK_ABSRELOC16:
				{
                    printf("%s (unsupported)\n", hunktype[type - HUNK_UNIT]);
                    return 1;
				}
                default:
				{
                    printf("Unknown (%08X)\n", type);
                    return 1;
				}
            }
        }
    }

    if (index < size)
    {
        printf("Warning: %d bytes of extra data at the end of the file!\n", (int)(size - index) * 4);
    }

    printf("\n");

    return 0;
}


