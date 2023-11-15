#include "pager.h"
#include "mmu.h"

#include <stdlib.h>
#include <unistd.h>
#include <sys/mman.h>

// Dados do tamanho de paginas e numero delas na regiao de memoria definida
#define PAGE_SIZE sysconf(_SC_PAGESIZE)
#define NUM_PAGES (UVM_MAXADDR - UVM_BASEADDR + 1) / PAGE_SIZE

//Conversao de um endenreco virtual para um index de um vector (Utilizado para falicitar os acessos)
#define VIRTAL_ADDR_TO_INDEX(vaddr) (int) (((int)vaddr - UVM_BASEADDR)/PAGE_SIZE)
#define INDEX_TO_VIRTUAL_ADDR(idx) (void*) (UVM_BASEADDR + (idx * PAGE_SIZE))

//Aqui deve-se adicionar possiveis bits e sinais uteis para as paginacoes
typedef struct{
    short page_fault;
    short write;
} bytes_frames;

// Celula, linka um pid a um endereco virtual
typedef struct{
    pid_t pid;
    bytes_frames features;
    void* vaddr;
} cell;

// Tabela de recursos
typedef struct{
    cell* resource;
    int size;
    int free_cell;
}resource_t;

resource_t frame_t;
resource_t block_t;

void init_table(resource_t *table){
    
    //Inicializo cada tabela com um pid -1 inacessivel e atraves do index do for linko um endereco virtual aquela posicao
    // isto facilitara os nossos acessos nos algoritmos do page_fault
    for(int i = 0 ;i < table->size; i++){
        table->resource[i].pid = -1;
        table->resource[i].vaddr = INDEX_TO_VIRTUAL_ADDR(i);
        table->resource[i].features.page_fault = 0;
        table->resource[i].features.write = 0;
    }
}

int alloc_pid(resource_t *table,pid_t pid){
    for(int i = 0; i < table->size;i++){
        if(table->resource[i].pid == -1){
            table->resource[i].pid = pid;
            return 1;
        }
    }
    return 0;
}

void disalloc_pid(resource_t *table, pid_t pid){
    for(int i = 0; i < table->size;i++){
        if(table->resource[i].pid == pid){
            table->resource[i].pid = -1;
        }
    }
}


void pager_init(int nframes, int nblocks){
    frame_t.size = frame_t.free_cell = nframes;
    block_t.size = block_t.free_cell = nblocks;

    frame_t.resource = (cell*) malloc(sizeof(cell) * nframes);
    block_t.resource = (cell*) malloc(sizeof(cell) * nblocks);
    
    init_table(&frame_t);
    init_table(&block_t);
}

void pager_create(pid_t pid){

    // Ta basicao por equanto mas talvez precise de mais coisas no futuro
    if(alloc_pid(&frame_t,pid) == 0){
        exit(EXIT_FAILURE);
    }

}

void *pager_extend(pid_t pid){

    //Verifica celulas livres para alocar blocos de memoria assim como pede o TP
    if(block_t.free_cell == 0){
        return NULL;
    }
    alloc_pid(&block_t,pid);
    block_t.free_cell--;

    for(int i = 0; i < frame_t.size; i++){
        if(frame_t.resource[i].pid == pid){
            frame_t.free_cell--; //Isto aqui vai ser util na hora do pageFault confia em mim
            return frame_t.resource[i].vaddr;
        }
    }

}

void pager_fault(pid_t pid, void *addr){  
    
    int frame_id = VIRTAL_ADDR_TO_INDEX(addr);
    //Primeiro acesso deve-se zerar os bits e a alocar espaco com permissao de leitura
    if(frame_t.resource[frame_id].features.page_fault == 0){
        frame_t.resource[frame_id].features.page_fault = 1;
        mmu_zero_fill(frame_id);
        mmu_resident(pid,addr,frame_id,PROT_READ);
    }
    //Proximos acessos necessitam ter a sua permissao alterada para escrita e leitura
    else{ 
        //Evita trocas desnecessarias de permissao
        if(frame_t.resource[frame_id].features.write == 0){
            frame_t.resource[frame_id].features.write = 1;
            mmu_chprot(pid,addr,PROT_WRITE | PROT_READ);
        }
    }
}

int pager_syslog(pid_t pid, void *addr, size_t len){

    return 0;
}

void pager_destroy(pid_t pid){

    // Ate o momento precisamos apenas desalocar as posicoes das tabelas
    disalloc_pid(&frame_t,pid);
    disalloc_pid(&block_t,pid);
}
