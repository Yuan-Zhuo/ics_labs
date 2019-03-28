/*
 *  loginID: 517030910169
 *  name: yuanzhuo
 */

#include <stdio.h>
#include "cachelab.h"

/**
 *  basic logic:
 *      1.base on waside-blocking.pdf
 *      2.write the specific function for these cases showing below
 *      3.get the conflict message between cache for A and B
 *
 *      int x = 0x30a100;
 *      int xx = 0x34a100;
 *      for(int i = 0; i < 64 * 64; i++){
 *             long y = xx + 4 * i;
 *             if((y >> 5U) % 32U == 9)
 *                 printf("%d\t", i);
 *      }
 *      4.for the speficit conflict, store tmp in vals to avoid
 */

void trans(int M, int N, int A[N][M], int B[M][N]);
void trans_32_32(int M, int N, int A[N][M], int B[M][N]);
void trans_64_64(int M, int N, int A[N][M], int B[M][N]);
void trans_61_67(int M, int N, int A[N][M], int B[M][N]);

int is_transpose(int M, int N, int A[N][M], int B[M][N]);

/*
 * transpose_submit - three special cases and the common case
 */
char transpose_submit_desc[] = "Transpose submission";
void transpose_submit(int M, int N, int A[N][M], int B[M][N]) {
    if (M == 32 && N == 32) {
        trans_32_32(32, 32, A, B);
    } else if (M == 64 && N == 64) {
        trans_64_64(64, 64, A, B);
    } else if (M == 61 && N == 67) {
        trans_61_67(61, 67, A, B);
    } else {
        trans(M, N, A, B);
    }
}

char trans_desc[] = "Simple row-wise scan transpose";
void trans(int M, int N, int A[N][M], int B[M][N]) {
    int i, j, tmp;

    for (i = 0; i < N; i++) {
        for (j = 0; j < M; j++) {
            tmp = A[i][j];
            B[j][i] = tmp;
        }
    }
}

/*
 * transpose_32_32 -
 *      logic:
 *      Because a single block with 32 byte can store 8 int, so the
 *      max basic block is 8*8.
 *      2.For every element axel wire, A and B will locate at the same set.
 *      To avoid this, lazy the assignment to axle wire elements.
 */
char trans_32_32_desc[] = "32x32-transpose";
void trans_32_32(int M, int N, int A[N][M], int B[M][N]) {
    int blkRow, blkCol;
    int eleRow, eleCol;
    int equalIdx;
    int tmp0;

    for (blkRow = 0; blkRow < 4; ++blkRow) {
        for (blkCol = 0; blkCol < 4; ++blkCol) {
            // basic block
            for (eleRow = 8 * blkRow; eleRow < 8 * blkRow + 8; ++eleRow) {
                for (eleCol = 8 * blkCol; eleCol < 8 * blkCol + 8; ++eleCol) {
                    // not in axel wire
                    if (eleRow != eleCol)
                        B[eleCol][eleRow] = A[eleRow][eleCol];
                    // record index and tmp
                    else {
                        equalIdx = eleRow;
                        tmp0 = A[eleRow][eleCol];
                    }
                }
                // assign to axel wire element
                if (blkRow == blkCol)
                    B[equalIdx][equalIdx] = tmp0;
            }
        }
    }
}

/*
 * transpose_64_64 -
 *      conflict:
 *      1.For every block in A, it will be evict where
 *      A[i][j] to A[i][j+7] (j mod 8==0)
 *      2.For every element axel wire, A and B will locate at the same set.
 */
char trans_64_64_desc[] = "64x64-transpose";
void trans_64_64(int M, int N, int A[N][M], int B[M][N]) {
    // index
    int i, j, ii, jj;
    // tmps
    int val0, val1, val2, val3, val4, val5, val6, val7;
    for (ii = 0; ii < N; ii += 8) {
        for (jj = 0; jj < M; jj += 8) {
            // For each row in the 8*4 block
            for (i = 0; i < 4; i++) {
                val0 = A[ii + i][jj + 0];
                val1 = A[ii + i][jj + 1];
                val2 = A[ii + i][jj + 2];
                val3 = A[ii + i][jj + 3];
                val4 = A[ii + i][jj + 4];
                val5 = A[ii + i][jj + 5];
                val6 = A[ii + i][jj + 6];
                val7 = A[ii + i][jj + 7];
                B[jj + 0][ii + i] = val0;
                B[jj + 1][ii + i] = val1;
                B[jj + 2][ii + i] = val2;
                B[jj + 3][ii + i] = val3;
                B[jj + 0][ii + 4 + i] = val4;
                B[jj + 1][ii + 4 + i] = val5;
                B[jj + 2][ii + 4 + i] = val6;
                B[jj + 3][ii + 4 + i] = val7;
            }
            // First copy the first 4 rows
            for (i = 0; i < 4; i++) {
                // get this row of the right-upper 4*4 block
                val0 = B[jj + i][ii + 4];
                val1 = B[jj + i][ii + 5];
                val2 = B[jj + i][ii + 6];
                val3 = B[jj + i][ii + 7];
                // update this row to its correct value
                val4 = A[ii + 4][jj + i];
                val5 = A[ii + 5][jj + i];
                val6 = A[ii + 6][jj + i];
                val7 = A[ii + 7][jj + i];

                B[jj + i][ii + 4] = val4;
                B[jj + i][ii + 5] = val5;
                B[jj + i][ii + 6] = val6;
                B[jj + i][ii + 7] = val7;

                // update the left lower 4*4 block of B
                B[jj + 4 + i][ii + 0] = val0;
                B[jj + 4 + i][ii + 1] = val1;
                B[jj + 4 + i][ii + 2] = val2;
                B[jj + 4 + i][ii + 3] = val3;
            }

            for (i = 4; i < 8; i++)
                for (j = 4; j < 8; j++)
                    B[jj + j][ii + i] = A[ii + i][jj + j];
        }
    }
}

/*
 * transpose_61_67 -
 *      conflict:
 *      1.For every block in A, it will be evict when B access
 *        B[i][j] where i mod 8==0.
 *      To avoid this, transfer the basic block to 4*4.
 *      2.For every element axel wire, A and B will locate at the same set.
 *      To avoid this, lazy the assignment to axle wire elements.
 */
char trans_61_67_desc[] = "61x67-transpose";
void trans_61_67(int M, int N, int A[N][M], int B[M][N]) {
    int blkRow, blkCol;
    int eleRow, eleCol;
    int equalIdx;
    int tmp0;

    for (blkCol = 0; blkCol < 61; blkCol += 16) {
        for (blkRow = 0; blkRow < 67; blkRow += 16) {
            // basic block
            for (eleRow = blkRow; (eleRow < 67) && (eleRow < blkRow + 16);
                 eleRow++) {
                for (eleCol = blkCol; (eleCol < 61) && (eleCol < blkCol + 16);
                     eleCol++) {
                    // not in axel wire
                    if (eleRow != eleCol) {
                        B[eleCol][eleRow] = A[eleRow][eleCol];
                    }

                    // record index and tmp
                    else {
                        tmp0 = A[eleRow][eleCol];
                        equalIdx = eleRow;
                    }
                }

                // assign to axel wire element
                if (blkRow == blkCol) {
                    B[equalIdx][equalIdx] = tmp0;
                }
            }
        }
    }
}

/*
 * registerFunctions - trans
 *                   - trans_32_32
 *                   - trans_64_64
 *                   - trans_61_67
 */
void registerFunctions() {
    /* Register your solution function */
    registerTransFunction(transpose_submit, transpose_submit_desc);

    registerTransFunction(trans, trans_desc);

    registerTransFunction(trans_32_32, trans_32_32_desc);

    registerTransFunction(trans_64_64, trans_64_64_desc);

    registerTransFunction(trans_61_67, trans_61_67_desc);
}

int is_transpose(int M, int N, int A[N][M], int B[M][N]) {
    int i, j;

    for (i = 0; i < N; i++) {
        for (j = 0; j < M; ++j) {
            if (A[i][j] != B[j][i]) {
                return 0;
            }
        }
        printf("\n");
    }
    return 1;
}
