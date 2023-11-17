#include "pager.h"
#include "mmu.h"

#include <stdlib.h>
#include <unistd.h>
#include <sys/mman.h>

// Dados do tamanho de paginas e numero delas na regiao de memoria definida
#define PAGE_SIZE sysconf(_SC_PAGESIZE)
#define NUM_PAGES (UVM_MAXADDR - UVM_BASEADDR + 1) / PAGE_SIZE
#define NO_ALLOC 0

//Conversao de um endenreco virtual para um index de um vector (Utilizado para falicitar os acessos)
#define VIRTAL_ADDR_TO_INDEX(vaddr) (int) (((int)vaddr - UVM_BASEADDR)/PAGE_SIZE)
#define INDEX_TO_VIRTUAL_ADDR(idx) (void*) (UVM_BASEADDR + (idx * PAGE_SIZE))

//Aqui deve-se adicionar possiveis bits e sinais uteis para as paginacoes
typedef struct{
    short page_fault;
    short in_frame;
    short write;
} bytes_frames;

typedef struct{
    int alloc;
} virtual_memory_map;

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
virtual_memory_map* v_map;

void init_table(resource_t *table){
    
    //Inicializo cada tabela com um pid -1 inacessivel e atraves do index do for linko um endereco virtual aquela posicao
    // isto facilitara os nossos acessos nos algoritmos do page_fault
    for(int i = 0 ;i < table->size; i++){
        table->resource[i].pid = -1;
        table->resource[i].vaddr = NO_ALLOC;
        table->resource[i].features.page_fault = 0;
        table->resource[i].features.in_frame = 0;
        table->resource[i].features.write = 0;
    }
}

void init_memory_map(){
    v_map = (virtual_memory_map*) malloc(sizeof(virtual_memory_map) * NUM_PAGES);
    for(int i = 0; i < NUM_PAGES; i++){
        v_map[i].alloc = 0;
    }
}

void* alloc_pid(resource_t *table,pid_t pid){
    void* addr;
    for(int i = 0; i < NUM_PAGES; i++){
        if(!v_map[i].alloc){
            v_map[i].alloc = 1;
            addr = INDEX_TO_VIRTUAL_ADDR(i);
            break;
        }
   }

    for(int i = 0; i < table->size; i++){
        if(table->resource[i].pid == -1){
            table->resource[i].pid = pid;
            table->resource[i].vaddr = addr;
        }
    }

    return addr;

}

void disalloc_pid(resource_t *table, pid_t pid){
    for(int i = 0; i < table->size;i++){
        if(table->resource[i].pid == pid){
            void* addr = table->resource[i].vaddr;
            int idx = VIRTAL_ADDR_TO_INDEX(addr);
            v_map[idx].alloc = 0;
            table->resource[i].pid = -1;
            table->free_cell++;
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

    init_memory_map();
}

void pager_create(pid_t pid){

    // Ta basicao por equanto mas talvez precise de mais coisas no futuro
    // if(alloc_pid(&frame_t,pid) == 0){
    //     exit(EXIT_FAILURE);
    // }

}

void *pager_extend(pid_t pid){

    //Verifica celulas livres para alocar blocos de memoria assim como pede o TP
    if(block_t.free_cell == 0){
        return NULL;
    }
    void* addr = alloc_pid(&block_t,pid);
    block_t.free_cell--;
    return addr;
}

void pager_fault(pid_t pid, void *addr){  
    
    int idx = VIRTAL_ADDR_TO_INDEX(addr);
    //Primeiro acesso deve-se zerar os bits e a alocar espaco com permissao de leitura
    if(block_t.resource[idx].features.page_fault == 0){
        block_t.resource[idx].features.page_fault = 1;
        block_t.resource[idx].features.in_frame = 1;
        
        frame_t.free_cell--;
        frame_t.resource[frame_t.free_cell] = block_t.resource[idx];
        
        mmu_zero_fill(idx);
        mmu_resident(pid,addr,idx,PROT_READ);
    }
    //Proximos acessos necessitam ter a sua permissao alterada para escrita e leitura
    else{ 
        //Evita trocas desnecessarias de permissao
        if(block_t.resource[idx].features.write == 0 && block_t.resource[idx].features.in_frame == 1){
            block_t.resource[idx].features.write = 1;
            mmu_chprot(pid,addr,PROT_WRITE | PROT_READ);
        }
    }
}

int pager_syslog(pid_t pid, void *addr, size_t len){
    
    if(addr < UVM_BASEADDR || addr > UVM_MAXADDR){
        return -1;
    }
    
    int index = VIRTAL_ADDR_TO_INDEX(addr);
    if(block_t.resource[index].pid != pid){
        return -1;
    }
    
    void* vaddr = INDEX_TO_VIRTUAL_ADDR(index);
    
    unsigned char* buf = (unsigned char*) malloc(sizeof(unsigned char)*len);
    buf = pmem + (index*PAGE_SIZE) + (addr - vaddr);
    for(int i = 0; i < len;i++){
        printf("%02x", (unsigned)buf[i]);
    }
    printf("\n");
    return 0;
}

void pager_destroy(pid_t pid){

    // Ate o momento precisamos apenas desalocar as posicoes das tabelas
    disalloc_pid(&frame_t,pid);
    disalloc_pid(&block_t,pid);
}
