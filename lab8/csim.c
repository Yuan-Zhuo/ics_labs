#include <getopt.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "cachelab.h"

#define maxLine 32
#define addrOff 3
#define addrBits 64
#define lineSize 12

#define validSpace (sizeof(bool))
#define boolSymbol(ch) (ch == 'M')
#define getSize(bits) (1 << (bits))
#define splitAddr(addr, lowestbit, bits) \
    ((addr) >> (lowestbit)) & ~((~0) << (bits))

static int setBits;
static int lines;
static int blkBits;
static int tagBits;
static int hits = 0;
static int misses = 0;
static int evicts = 0;
static bool showDetail = false;

char* initCond(int argc, char* argv[]) {
    int opt;
    const char optStr[] = "vs:E:b:t:";
    char* filename;

    while ((opt = getopt(argc, argv, optStr)) != -1) {
        switch (opt) {
            case 'v':
                showDetail = true;
                break;
            case 's':
                setBits = atoi(optarg);
                break;
            case 'E':
                lines = atoi(optarg);
                break;
            case 'b':
                blkBits = atoi(optarg);
                break;
            case 't':
                filename = optarg;
                break;
        }
    }
    tagBits = addrBits - setBits - blkBits;

    return filename;
}

void simTrace(void* CACHE_ptr, bool flag, long addr) {
    static unsigned cnt = 0;
    cnt++;

    int set = splitAddr(addr, blkBits, setBits);
    long tag = splitAddr(addr, blkBits + setBits, tagBits);

    void* Set_ptr = CACHE_ptr + lineSize * lines * set;
    unsigned min_valid = -1;
    int min_index = 0;
    int illegal_index = -1;

    void* cur_addr = Set_ptr - lineSize;

    for (int i = 0; i < lines; ++i) {
        cur_addr += lineSize;
        unsigned cur_valid = *(unsigned int*)cur_addr;

        if (cur_valid == 0) {
            if (illegal_index == -1)
                illegal_index = i;
            continue;
        }

        long cur_tag = *(long*)(cur_addr + 4);
        if (cur_tag == tag) {
            hits++;
            *(unsigned int*)(cur_addr) = cnt;

            if (flag)
                hits++;

            return;
        }

        if (min_valid > cur_valid) {
            min_valid = cur_valid;
            min_index = i;
        }
    }

    misses++;

    int fill_index = illegal_index;
    if (illegal_index == -1) {
        evicts++;

        fill_index = min_index;
    }

    if (flag) {
        hits++;
        cnt++;
    }

    *(unsigned int*)(Set_ptr + fill_index * lineSize) = cnt;
    *(long*)(Set_ptr + fill_index * lineSize + 4) = tag;

    return;
}

bool simulator(char* filename, void* CACHE_ptr) {
    FILE* fp = NULL;
    if ((fp = fopen(filename, "r")) == NULL) {
        printf("cannot open file: %s", filename);
        return false;
    }

    char* strLine = (char*)malloc(maxLine);
    char* strAddr = (char*)malloc(maxLine);
    while (true) {
        fgets(strLine, maxLine, fp);

        if (feof(fp))
            break;

        if (strLine[0] != ' ')
            continue;

        int len = strlen(strLine);
        strncpy(strAddr, strLine + addrOff, len);

        simTrace(CACHE_ptr, boolSymbol(strLine[1]), strtol(strAddr, NULL, 16));
    }

    free(strLine);
    free(strAddr);
    return true;
}

int main(int argc, char* argv[]) {
    char* filename = initCond(argc, argv);

    int setNum = getSize(setBits);
    void* CACHE_ptr = (void*)malloc(lineSize * setNum * lines);
    void* cur_ptr = CACHE_ptr;

    for (int i = 0; i < setNum; ++i) {
        for (int j = 0; j < lines; ++j) {
            *(unsigned int*)cur_ptr = 0;
            cur_ptr += lineSize;
        }
    }

    if (!simulator(filename, CACHE_ptr))
        return 1;

    free(CACHE_ptr);

    printSummary(hits, misses, evicts);

    return 0;
}
