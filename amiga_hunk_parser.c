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
#define HUNK_DEBUG_LINE 0x4C494E45


typedef struct SymbolInfo
{
	const char* name;
	uint32_t address;
} SymbolInfo;

typedef struct LineInfo
{
	const char* filename;
	int count;
	uint32_t baseOffset;
	uint32_t* addresses;
	int* lines;
	struct LineInfo* next;
} LineInfo;

typedef struct HunkInfo
{
    uint32_t type;        // HUNK_<type>
    uint32_t flags;       // HUNKF_<flag>

    int memsize, datasize; // longwords
    int datastart;        // longword index in file

    int relocstart;       // longword index in file
    int relocentries;     // no. of entries

    int symbolCount;
    int debugLineCount;

    SymbolInfo* symbols;
    LineInfo* debugLines;
    LineInfo* lastDebugInfo;

} HunkInfo;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static inline uint16_t swap_uint16(uint16_t val)
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

uint16_t get_u16_inc (const void* t, int* index)
{
    uint16_t* mem = (uint16_t*)(((uint8_t*)t) + *index);
    *index += 2;
#if defined(AHP_LITTLE_ENDIAN)
    return swap_uint16(*mem);
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

void* alloc_zero(size_t size)
{
	void* t = malloc(size);
	memset(t, 0, size); 
	return t;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#define xalloc_zero(type, count) (type*)alloc_zero(sizeof(type) * count);
#define xalloc(type, count) (type*)malloc(sizeof(type) * count);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void parseSymbols(HunkInfo* hunk, const void* data, int* currIndex)
{
	int oldIndex, index, i = 0;
	int symCount = 0;

	index = oldIndex = *currIndex;

	// count symbols

	int symlen = get_u32_inc(data, &index) * 4;

	while (symlen > 0)
	{
		symCount++;
		index += symlen + 4;
		symlen = get_u32_inc(data, &index) * 4;
	}

	hunk->symbolCount = symCount;
	hunk->symbols = xalloc(SymbolInfo, symCount);

	index = oldIndex;

	symlen = get_u32_inc(data, &index) * 4;

	while (symlen > 0)
	{
		SymbolInfo* info = &hunk->symbols[i++];
		info->name = ((const char*)data) + index;
		index += symlen;
		info->address = get_u32_inc(data, &index) * 4;
		symlen = get_u32_inc(data, &index) * 4;
	}

	// symbols for hunk
	
	*currIndex = index;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void parseDebug(HunkInfo* hunk, const void* data, int* currIndex)
{
	int index = *currIndex;

	const uint32_t hunkLength = get_u32_inc(data, &index) * 4;
	const uint32_t baseOffset = get_u32_inc(data, &index) * 4;
	const uint32_t debugId = get_u32_inc(data, &index);

	if (debugId != HUNK_DEBUG_LINE)
	{
		*currIndex += hunkLength;
		return;
	}

	LineInfo* lineInfo = xalloc_zero(LineInfo, 1);

	const uint32_t stringLength = get_u32_inc(data, &index) * 4;

	lineInfo->baseOffset = baseOffset;
	lineInfo->filename = ((const char*)data) + index;

	index += stringLength;

	// M = ((N - 3) - number_of_string_longwords) / 2

	const int lineCount = ((hunkLength - (3 * 4)) - stringLength) / 8;

	lineInfo->addresses = xalloc(uint32_t, lineCount); 
	lineInfo->lines = xalloc(int, lineCount); 

	for (int i = 0; i < lineCount; ++i)
	{
		lineInfo->lines[i] = (int)get_u32_inc(data, &index); 
		lineInfo->addresses[i] = (uint32_t)get_u32_inc(data, &index); 
	}

	if (!hunk->debugLines)
		hunk->debugLines = lineInfo;

	if (hunk->lastDebugInfo)
		hunk->lastDebugInfo->next = lineInfo;

	hunk->lastDebugInfo = lineInfo;

	*currIndex += hunkLength + 4;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void parseCodeDataBss(HunkInfo* hunk, int type, const void* data, int* currIndex)
{
	int index = *currIndex;

	hunk->type = type;
	hunk->datasize = get_u32_inc(data, &index) * 4;

	printf("%4s%10x %10x", hunktype[type - HUNK_UNIT], hunk->memsize, hunk->datasize);

	if (type != HUNK_BSS)
	{
		hunk->datastart = index;
		index += hunk->datasize;

		if (hunk->datasize > 0)
		{
			int sum = 0;

			for (int pos = hunk->datastart; pos < hunk->datastart + hunk->datasize; pos += 4)
				sum += get_u32(data, pos);

			printf("  %08x", sum);
		}
		else
		{
			printf("          ");
		}
	}

	*currIndex = index;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void parseReloc32(HunkInfo* hunk, const void* data, int* currIndex)
{
	int index = *currIndex;

	hunk->relocstart = index;
	int n, tot = 0;

	while ((n = get_u32_inc(data, &index)) != 0)
	{
		uint32_t t = get_u32_inc(data, &index);
		(void)t;

		tot += n;

		while (n--)
		{
			if ((get_u32_inc(data, &index)) > hunk->memsize - 4)
			{
				printf("\nError in reloc table!\n");
				exit(1);
			}
		}
	}

    hunk->relocentries = tot;

	*currIndex = index;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void parseDreloc32(HunkInfo* hunk, const void* data, int* currIndex)
{
	int index = *currIndex;

	hunk->relocstart = index;

	int n, tot = 0;

	while ((n = get_u16_inc(data, &index)) != 0)
	{
		uint16_t t = get_u16_inc(data, &index);
		(void)t;

		tot += n;

		while (n--)
		{
			uint16_t t = get_u16_inc(data, &index);

			if (t > hunk->memsize - 4)
			{
				printf("\nError in reloc table!\n");
				exit(1);
			}
		}
	}

	if (index & 2)
		index += 2;

	hunk->relocentries = tot;

	*currIndex = index;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static int parseHunk(HunkInfo* hunk, const void* data, int hunkId, int size, int* currIndex)
{
	int type;
	int index = *currIndex;
	const uint32_t flags = hunk->flags;

	printf("%4d  %s", hunkId, flags == HUNKF_CHIP ? "CHIP" : flags == HUNKF_FAST ? "FAST" : "ANY ");

	for (;;)
	{
		if (index >= size)
		{
			printf("\nUnexpected end of file!\n");
			return 0;
		}

		type = get_u32_inc(data, &index) & 0x0fffffff;

		if (index >= size && type != HUNK_END)
		{
			printf("\nUnexpected end of file!\n");
			return 0;
		}

		switch (type)
		{
			case HUNK_DEBUG: parseDebug(hunk, data, &index); break;
			case HUNK_SYMBOL: parseSymbols(hunk, data, &index); break;

			case HUNK_CODE:
			case HUNK_DATA:
			case HUNK_BSS: parseCodeDataBss(hunk, type, data, &index); break;
			case HUNK_RELOC32: parseReloc32(hunk, data, &index); break;

			case HUNK_DREL32:
			case HUNK_RELOC32SHORT: parseDreloc32(hunk, data, &index); break;

			case HUNK_UNIT:
			case HUNK_NAME:
			case HUNK_RELOC16:
			case HUNK_RELOC8:
			case HUNK_EXT:
			case HUNK_HEADER:
			case HUNK_OVERLAY:
			case HUNK_BREAK:
			case HUNK_DREL16:
			case HUNK_DREL8:
			case HUNK_LIB:
			case HUNK_INDEX:
			case HUNK_RELRELOC32:
			case HUNK_ABSRELOC16:
			{
				printf("%s (unsupported) at %d\n", hunktype[type - HUNK_UNIT], index);
				return 0;
			}

			case HUNK_END: 
			{
				*currIndex = index;
				printf("\n"); return 1; 
			}

			default:
			{
				printf("Unknown (%08X)\n", type);
				return 0;
			}
		}
	}

	return 1;
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
    for (int h = 0; h < numhunks; ++h)
    {
    	if (!parseHunk(&hunks[h], data, h, size, &index)) 
    		return 0; 
    }

    if (index < size)
    {
        printf("Warning: %d bytes of extra data at the end of the file!\n", (int)(size - index) * 4);
    }

    printf("\n");

    return 0;
}


