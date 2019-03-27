/*
 *  loginID: 517030910169
 *  name: yuanzhuo
 */

#include <getopt.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "cachelab.h"

/**
 *  basic logic:
 *      1.valid bit: present by an unsigned int where 0 means invalid, and 1~inf
 *                   indicates the last time when this block accessed.
 *      2.tag bits: present by a long.
 *      3.just malloc for the above bits, so for one block, malloc 12byte.
 */

/**
 *  magic number:
 *      maxLine: the max size of a line reading from .trace
 *      addrOff: the offset of the address in a line
 *      addrBits: the address bits
 *      lineSize: malloc size
 */
#define maxLine 32
#define addrOff 3
#define addrBits 64
#define lineSize 12

/*
 *  tool function:
 *      validSpace: weither this block is valid
 *      boolSymbol: weither the data in this line need 'M'(modify)
 *      getSize: find out how much sets, lines should be malloced by bits
 *      splitAddr: split the address into tag, valid
 */
#define validSpace (sizeof(bool))
#define boolSymbol(ch) (ch == 'M')
#define getSize(bits) (1 << (bits))
#define splitAddr(addr, lowestbit, bits) \
    ((addr) >> (lowestbit)) & ~((~0) << (bits))

/*
 *  static variable:
 *      setBits: s
 *      lines: E
 *      blkBits: b
 *      tagBits: t
 *      hits: the sum of hit times
 *      misses: the sum of miss times
 *      evicts: the sum of eviction times
 *      showDetail: if -v in args
 */
static int setBits;
static int lines;
static int blkBits;
static int tagBits;
static int hits = 0;
static int misses = 0;
static int evicts = 0;
static bool showDetail = false;

/*
 *  initCond- read the args from command line and
 *            initialize the info of the cache
 */
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

/*
 *  simTrace- handle a single data
 */
void simTrace(void* CACHE_ptr, bool flag, long addr) {
    // count the times of simTrace called
    // assign to the valid space then
    // while the min nonzero valid represent least-recently used
    static unsigned cnt = 0;
    cnt++;

    // get the info of the data from addr
    int set = splitAddr(addr, blkBits, setBits);
    long tag = splitAddr(addr, blkBits + setBits, tagBits);

    void* Set_ptr = CACHE_ptr + lineSize * lines * set;
    unsigned min_valid = -1;
    int min_index = 0;
    int illegal_index = -1;

    void* cur_addr = Set_ptr - lineSize;

    // traverse the all lines in a specific set
    for (int i = 0; i < lines; ++i) {
        cur_addr += lineSize;
        unsigned cur_valid = *(unsigned int*)cur_addr;

        // invalid line
        if (cur_valid == 0) {
            if (illegal_index == -1)
                illegal_index = i;
            continue;
        }

        long cur_tag = *(long*)(cur_addr + 4);
        // hit
        if (cur_tag == tag) {
            hits++;
            // update to lastest used
            *(unsigned int*)(cur_addr) = cnt;

            // hit two times if the symbol is 'M'
            if (flag)
                hits++;

            return;
        }

        // record the index of the min valid
        if (min_valid > cur_valid) {
            min_valid = cur_valid;
            min_index = i;
        }
    }

    // not found, therefore miss
    misses++;

    int fill_index = illegal_index;
    // no invalid block, so evict LRU
    if (illegal_index == -1) {
        evicts++;

        fill_index = min_index;
    }

    // hit one times if the symbol is 'M'
    if (flag) {
        hits++;
        cnt++;
    }

    // replace
    *(unsigned int*)(Set_ptr + fill_index * lineSize) = cnt;
    *(long*)(Set_ptr + fill_index * lineSize + 4) = tag;

    return;
}

/*
 *  display- display the status of when a single data accessed
 */
void display(char* strLine, bool isHit, bool isEvict) {
    if (!showDetail)
        return;

    // copy and remove the '\n' in strLine
    char info[100];
    char* ch = info;
    while ((*ch++ = *strLine++))
        ;

    ch -= 2;
    *ch++ = '\t';
    *ch = 0;

    if (isHit)
        strcat(info, "Hit\t");
    else
        strcat(info, "Miss\t");

    if (isEvict)
        strcat(info, "Eviction\t");

    printf("%s\n", info);
}

/*
 *  simulator- read the file and handle every line
 */
bool simulator(char* filename, void* CACHE_ptr) {
    FILE* fp = NULL;
    // read exception
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

        // if it's an 'I'
        if (strLine[0] != ' ')
            continue;

        int len = strlen(strLine);
        strncpy(strAddr, strLine + addrOff, len);

        int oldHit = hits, oldEvict = evicts;
        // handle this line
        simTrace(CACHE_ptr, boolSymbol(strLine[1]), strtol(strAddr, NULL, 16));

        // call to display the status
        display(strLine, hits == oldHit, evicts == oldEvict);
    }

    free(strLine);
    free(strAddr);
    return true;
}

int main(int argc, char* argv[]) {
    char* filename = initCond(argc, argv);

    int setNum = getSize(setBits);
    // malloc the space to represent cache
    void* CACHE_ptr = (void*)malloc(lineSize * setNum * lines);
    void* cur_ptr = CACHE_ptr;

    // init every block to invalid
    for (int i = 0; i < setNum; ++i) {
        for (int j = 0; j < lines; ++j) {
            *(unsigned int*)cur_ptr = 0;
            cur_ptr += lineSize;
        }
    }

    // handle the trace file
    if (!simulator(filename, CACHE_ptr))
        return 1;

    free(CACHE_ptr);

    printSummary(hits, misses, evicts);

    return 0;
}
