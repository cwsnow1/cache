#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#define ROWS            (256)
#define COLUMNS         (ROWS)
#define NUM_MATRICES    (2)

static double matrices[NUM_MATRICES][ROWS][COLUMNS];
static double result[ROWS][COLUMNS];

int main(int argc, char** argv) {
    if (argc < 2) {
        fprintf(stderr, "Give an argument, gen or calc\n");
        exit(1);
    }
    if (!strcmp(argv[1], "gen")) {
        FILE* f = fopen("matrices.bin", "wb");
        assert(f);
        for (int i = 0; i < NUM_MATRICES; i++) {
            for (int j = 0; j < ROWS; j++) {
                for (int k = 0; k < COLUMNS; k++) {
                    matrices[i][j][k] = (double) ((rand() % 200) - 100);
                }
            }
        }
        if (fwrite(matrices, sizeof(double), NUM_MATRICES * ROWS * COLUMNS, f) < 0){
            fprintf(stderr, "Write failed!\n");
            if (f) fclose(f);
            exit(1);
        }
        fclose(f);
    } else if (!strcmp(argv[1], "calc")) {
        FILE* f= fopen("matrices.bin", "rb");
        if (f == NULL) {
            fprintf(stderr, "Error opening matrices.bin. Does it exist?\n");
            exit(1);
        }
        int ret = fread(matrices, sizeof(double), NUM_MATRICES * ROWS * COLUMNS, f);
        if (ret < 0) {
            fprintf(stderr, "Read failed!\n");
            if (f) fclose(f);
            exit(1);
        }
        for (int i = 0; i < ROWS; i++) {
            for (int j = 0; j < COLUMNS; j++) {
                result[i][j] = 0.0f;
                for (int k = 0; k < COLUMNS; k++) {
                    result[i][j] += matrices[0][i][k] * matrices[1][k][j];
                }
            }
        }
        printf("result[%u][%u] = %.4lf\n", ROWS, COLUMNS, result[ROWS - 1][COLUMNS - 1]);
    } else {
        fprintf(stderr, "Please provide an appropriate arg\n");
        exit(1);
    }
}