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

char* initCond(int argc, char* argv[]) {
    int opt;
    const char optStr[] = "vs:E:b:t:";
    bool showDetail = false;
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

void* cacheMalloc() {
    int setNum = getSize(setBits);
    void* CACHE_ptr[setNum][lines];
    int lineSize = sizeof(bool) + sizeof(int);

    for (int i = 0; i < setNum; ++i) {
        for (int j = 0; j < lines; ++j) {
            void* lineBlk = malloc(lineSize);
            *(bool*)lineBlk = false;
            CACHE_ptr[i][j] = lineBlk;
        }
    }

    return CACHE_ptr;
}

void simTrace(void* CACHE_ptr, bool flag, int addr) {}

bool simulator(char* filename, void* CACHE_ptr) {
    FILE* fp = NULL;
    if ((fp = fopen(filename, "r")) == NULL) {
        printf("cannot open file: %s", filename);
        return false;
    }

    char* strLine = (char*)malloc(maxLine);
    char* strAddr = (char*)malloc(maxLine);
    while (!feof(fp)) {
        fgets(strLine, maxLine, fp);

        if (strLine == NULL)
            break;
        if (strLine[0] == ' ')
            continue;

        int len = strlen(strLine);
        strncpy(strAddr, strLine + addrOff, len);

        simTrace(CACHE_ptr, boolSymbol(strLine[1]), atoi(strAddr));
    }

    free(strLine);
    free(strAddr);

    return true;
}

int main(int argc, char* argv[]) {
    char* filename = initCond(argc, argv);

    void* CACHE_ptr = cacheMalloc();

    if (!simulator(filename, CACHE_ptr))
        return 1;

    printSummary(hits, misses, evicts);

    return 0;
}
