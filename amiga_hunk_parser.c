#include "amiga_hunk_parser.h"
#include "doshunks.h"
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#if defined (__AROS__)
    #include <aros/cpu.h>
    #if AROS_BIG_ENDIAN
        #define AHP_BIG_ENDIAN
        #define AHP_BYTE_ORDER 4321
    #else
        #define AHP_LITTLE_ENDIAN
        #define AHP_BYTE_ORDER 1234
    #endif
#else
#if defined (__GLIBC__)
#include <endian.h>
#if (__BYTE_ORDER == __LITTLE_ENDIAN)
	#define AHP_LITTLE_ENDIAN
#elif (__BYTE_ORDER == __BIG_ENDIAN)
	#define AHP_BIG_ENDIAN
#elif (__BYTE_ORDER == __PDP_ENDIAN)
	#define AHP_BIG_ENDIAN
#else
	#error "Unable to detect endian for your target."
#endif
	#define AHP_BYTE_ORDER __BYTE_ORDER
#elif defined(_BIG_ENDIAN)
	#define AHP_BIG_ENDIAN
	#define AHP_BYTE_ORDER 4321
#elif defined(_LITTLE_ENDIAN)
	#define AHP_LITTLE_ENDIAN
	#define AHP_BYTE_ORDER 1234
#elif defined(__sparc) || defined(__sparc__) \
   || defined(_POWER) || defined(__powerpc__) \
   || defined(__ppc__) || defined(__hpux) \
   || defined(_MIPSEB) || defined(_POWER) \
   || defined(__s390__)
	#define AHP_BIG_ENDIAN
	#define AHP_BYTE_ORDER 4321
#elif defined(__i386__) || defined(__alpha__) \
   || defined(__ia64) || defined(__ia64__) \
   || defined(_M_IX86) || defined(_M_IA64) \
   || defined(_M_ALPHA) || defined(__amd64) \
   || defined(__amd64__) || defined(_M_AMD64) \
   || defined(__x86_64) || defined(__x86_64__) \
   || defined(_M_X64)
	#define AHP_LITTLE_ENDIAN
	#define AHP_BYTE_ORDER 1234
#else
	#error "Unable to detect endian for your target."
#endif
#endif

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

const char* hunktype[HUNK_ABSRELOC16 - HUNK_UNIT + 1] = 
{
    "UNIT", "NAME", "CODE", "DATA", "BSS ", "RELOC32", "RELOC16", "RELOC8",
    "EXT", "SYMBOL", "DEBUG", "END", "HEADER", "", "OVERLAY", "BREAK",
    "DREL32", "DREL16", "DREL8", "LIB", "INDEX",
    "RELOC32SHORT", "RELRELOC32", "ABSRELOC16"
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#define HUNK_DEBUG_LINE 0x4C494E45

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/*
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
*/

/*
typedef struct AHPSection
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

} AHPSection;
*/

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

static void parseSymbols(AHPSection* section, const void* data, int* currIndex)
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

	section->symbolCount = symCount;
	section->symbols = xalloc(AHPSymbolInfo, symCount);

	index = oldIndex;

	symlen = get_u32_inc(data, &index) * 4;

	while (symlen > 0)
	{
		AHPSymbolInfo* info = &section->symbols[i++];
		info->name = ((const char*)data) + index;
		index += symlen;
		info->address = get_u32_inc(data, &index) * 4;
		symlen = get_u32_inc(data, &index) * 4;
	}

	*currIndex = index;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void parseDebug(AHPSection* section, const void* data, int* currIndex)
{
	int index = *currIndex;
	AHPLineInfo* lineInfo = 0;

	const uint32_t hunkLength = get_u32_inc(data, &index) * 4;
	const uint32_t baseOffset = get_u32_inc(data, &index) * 4;
	const uint32_t debugId = get_u32_inc(data, &index);

	if (debugId != HUNK_DEBUG_LINE)
	{
		*currIndex += hunkLength;
		return;
	}

	if (!section->debugLines)
	{
		lineInfo = xalloc_zero(AHPLineInfo, 1);
		section->debugLines = lineInfo;
		section->debugLineCount = 1;
	}
	else
	{
		// Kinda sucky impl but should do
		int lineCount = section->debugLineCount++;
		section->debugLines = realloc(section->debugLines, lineCount * 2 * sizeof(AHPLineInfo));
		lineInfo = &section->debugLines[lineCount];
		memset(lineInfo, 0, sizeof(AHPLineInfo));
	}

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

	lineInfo->count = lineCount;
	
	*currIndex += hunkLength + 4;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void parseCodeDataBss(AHPSection* section, int type, const void* data, int* currIndex)
{
	int index = *currIndex;

	switch (type)
	{
		case HUNK_CODE: section->type = AHPSectionType_Code; break;
		case HUNK_DATA: section->type = AHPSectionType_Data; break;
		case HUNK_BSS: section->type = AHPSectionType_Bss; break;
	}

	section->dataSize = get_u32_inc(data, &index) * 4;

	if (type != HUNK_BSS)
	{
		section->dataStart = index;
		index += section->dataSize;

		if (section->dataSize > 0)
		{
			int sum = 0;

			for (int pos = section->dataStart; pos < section->dataStart + section->dataSize; pos += 4)
				sum += get_u32(data, pos);

			(void)sum;
		}
	}

	*currIndex = index;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void parseReloc32(AHPSection* section, const void* data, int* currIndex)
{
	int index = *currIndex;

	section->relocStart = index;
	int n, tot = 0;

	while ((n = get_u32_inc(data, &index)) != 0)
	{
		uint32_t t = get_u32_inc(data, &index);
		(void)t;

		tot += n;

		while (n--)
		{
			if ((get_u32_inc(data, &index)) > section->memSize - 4)
			{
				printf("\nError in reloc table!\n");
				exit(1);
			}
		}
	}

    section->relocCount = tot;

	*currIndex = index;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void parseDreloc32(AHPSection* section, const void* data, int* currIndex)
{
	int index = *currIndex;

	section->relocStart = index;

	int n, tot = 0;

	while ((n = get_u16_inc(data, &index)) != 0)
	{
		uint16_t t = get_u16_inc(data, &index);
		(void)t;

		tot += n;

		while (n--)
		{
			uint16_t t = get_u16_inc(data, &index);

			if (t > section->memSize - 4)
			{
				printf("\nError in reloc table!\n");
				exit(1);
			}
		}
	}

	if (index & 2)
		index += 2;

	section->relocCount = tot;

	*currIndex = index;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static int parseSection(AHPSection* section, const void* data, int hunkId, int size, int* currIndex)
{
	int type;
	int index = *currIndex;

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
			case HUNK_DEBUG: parseDebug(section, data, &index); break;
			case HUNK_SYMBOL: parseSymbols(section, data, &index); break;

			case HUNK_CODE:
			case HUNK_DATA:
			case HUNK_BSS: parseCodeDataBss(section, type, data, &index); break;
			case HUNK_RELOC32: parseReloc32(section, data, &index); break;

			case HUNK_DREL32:
			case HUNK_RELOC32SHORT: parseDreloc32(section, data, &index); break;

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
				return 1; 
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

AHPInfo* ahp_parse_file(const char* filename)
{
    size_t size = 0;
    void* data = loadToMemory(filename, &size);
    uint32_t header = 0;
    int h, index = 0;
    int sectionCount = 0;
    AHPSection* sections = 0;

    if (!data)
    {
        printf("Unable to open %s\n", filename);
        return 0;
    }

    AHPInfo* info = xalloc_zero(AHPInfo, 1);

    if (get_u32_inc(data, &index) != HUNK_HEADER)
    {
        printf("HunkHeader is incorrect (should be 0x%08x but is 0x%08x)\n", HUNK_HEADER, header);
        ahp_free(info);
        return 0;
    }

    while (get_u32_inc(data, &index))
    {
        index += get_u32(data, index) * 4;
        if (index >= size)
        {
            printf("Bad hunk header!\n");
        	ahp_free(info);
            return 0;
        }
    }

    sectionCount = get_u32_inc(data, &index);

    if (sectionCount == 0)
    {
        printf("No sections!\n");
		ahp_free(info);
        return 0;
    }

	info->sections = sections = xalloc_zero(AHPSection, sectionCount); 
	info->sectionCount = sectionCount;

    if (get_u32_inc(data, &index) != 0 || get_u32_inc(data, &index) != sectionCount - 1)
    {
        printf("Unsupported hunk load limits!\n");
        ahp_free(info);
        return 0;
    }

    // read hunk sizes and target

    for (h = 0; h < sectionCount; ++h)
    {
    	AHPSectionTarget target = AHPSectionTarget_Any;

        sections[h].memSize = (get_u32(data, index) & 0x0fffffff) * 4;
        uint32_t flags = get_u32(data, index) & 0xf0000000;

        switch (flags)
		{
			case 0 : target = AHPSectionTarget_Any; break; 
			case HUNKF_CHIP : target = AHPSectionTarget_Chip; break; 
			case HUNKF_FAST : target = AHPSectionTarget_Fast; break; 
		}

		sections[h].target = target;

        index += 4;
    }

    for (h = 0; h < sectionCount; ++h)
    {
    	if (!parseSection(&sections[h], data, h, size, &index)) 
		{
			ahp_free(info);
    		return 0; 
		}
    }

    if (index < size)
    {
        printf("Warning: %d bytes of extra data at the end of the file!\n", (int)(size - index) * 4);
    }

    return info;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static const char* getTypeName(AHPSectionType type)
{
	switch (type)
	{
		case AHPSectionType_Code : return "CODE";
		case AHPSectionType_Data : return "DATA";
		case AHPSectionType_Bss : return "BSS ";
	}

	return "UNKN";
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static const char* getTargetName(AHPSectionTarget target)
{
	switch (target)
	{
		case AHPSectionTarget_Any : return "ANY ";
		case AHPSectionTarget_Fast : return "FAST";
		case AHPSectionTarget_Chip : return "CHIP";
	}

	return "UNKN";
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void ahp_print_info(AHPInfo* info, int verbose)
{	
	int dli, syi, si, i;

	printf("Sec Type  Target  memSize    relocCount  symCount   debugLineCount\n");

	for (i = 0; i < info->sectionCount; ++i)
	{
		AHPSection* section = &info->sections[i];

	    //printf("Section Type Target memSize relocCount symCount debugLineCount\n");
		printf("%02d  %s  %s   %8d      %8d  %8d   %8d\n", i, 
				getTypeName(section->type), 
				getTargetName(section->target), 
				section->memSize,
				section->relocCount,
				section->symbolCount,
				section->debugLineCount);
	}

	if (!verbose)
		return;

	for (si = 0; si < info->sectionCount; ++si)
	{
		AHPSection* section = &info->sections[si];

		printf("Section %d ------------------------------------------------------\n", si);

		if (section->symbolCount > 0)
			printf("  Symbols ------------------------------------------------------\n");

		for (syi = 0; syi < section->symbolCount; ++syi)
		{
    		AHPSymbolInfo* symbol = &section->symbols[syi];
    		printf("  %08x - %s\n", symbol->address, symbol->name);
		}

		if (section->debugLineCount > 0)
			printf("  DebugLineCount -----------------------------------------\n");

		for (dli = 0; dli < section->debugLineCount; ++dli)
		{
    		AHPLineInfo* debugLines = &section->debugLines[dli];

    		printf("  File %s\n", debugLines->filename);

    		for (i = 0; i < debugLines->count; ++i)
    			printf("    %08x - %d\n", debugLines->addresses[i], debugLines->lines[i]);
		}
	}
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void ahp_free(AHPInfo* info)
{
	free(info->sections);
	free(info->fileData);
	free(info);
}

