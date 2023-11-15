#include "pager.h"
#include <stdlib.h>

typedef struct{
    int* frames;
    int* blocks;
} pager_table;

pager_table pages;

void pager_init(int nframes, int nblocks){
    pages.frames = (int*) malloc(sizeof(int) * nframes);
    pages.blocks = (int*) malloc(sizeof(int) * nblocks);
    for(int i = 0; i < nframes;i++){
        pages.frames[i] = 0;
    }
    for(int i = 0; i < nblocks; i++){
        pages.blocks[i] = 0;
    }
}

void *pager_extend(pid_t pid){

    return NULL;
}

void pager_create(pid_t pid){

}

void pager_fault(pid_t pid, void *addr){

}

int pager_syslog(pid_t pid, void *addr, size_t len){

    return 0;
}

void pager_destroy(pid_t pid){

}
