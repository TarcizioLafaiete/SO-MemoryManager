#include "pager.h"
#include "mmu.h"

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/mman.h>

#define PAGE_SIZE sysconf(_SC_PAGESIZE)
#define NUM_PAGES (UVM_MAXADDR - UVM_BASEADDR + 1) / PAGE_SIZE
#define NO_ALLOC 0


#define VIRTAL_ADDR_TO_INDEX(vaddr) (int) (((int)vaddr - UVM_BASEADDR)/PAGE_SIZE)
#define INDEX_TO_VIRTUAL_ADDR(idx) (void*) (UVM_BASEADDR + (idx * PAGE_SIZE))

//----------------------------- PAGE CENTRAL----------------------------------------------------------

typedef struct{
    short in_frame;
    short permission;
} bits_array;

typedef struct{
    pid_t pid;
    bits_array options;
    void* vaddr;
} page;


typedef struct{
    page* page_t;
    int size;
    int free;
}page_central;

page_central frame;
page_central block;

void init_page_central(page_central *central){
    
    for(int i = 0 ;i < central->size; i++){
        central->page_t[i].pid = -1;
        central->page_t[i].vaddr = NO_ALLOC;
        central->page_t[i].options.in_frame = 0;
        central->page_t[i].options.permission = 0;
    }
}

int check_page_allocation(page_central* central, pid_t pid, void* vaddr,int* pos){

    if(central->free == central->size){
        return 0;
    }

    for(int i = 0; i < central->size; i++){
        if(central->page_t[i].pid == pid && central->page_t[i].vaddr == vaddr){
            *pos = i;
            return 1;
        }
    }
    *pos = -1; 
    return 0;
}

//-------------------------- VIRTUAL MEMORY ---------------------------------------------------------------------

typedef struct{
    pid_t pid;
    int* pages;
    int page_ptr;
} virtual_memory;

typedef struct{
    struct vm_node* head;
    struct vm_node* tail;
    int size;
} vm_list;

struct vm_node{
    virtual_memory data;
    struct vm_node* next;
};

vm_list* manager;


vm_list* vm_list_create(){
    vm_list* list = malloc(sizeof(vm_list));
    struct vm_node* new_node = malloc(sizeof(struct vm_node));
    new_node->data.pid = -1;
    new_node->data.pages = (int*) calloc(NUM_PAGES, sizeof(int));
    new_node->data.page_ptr = -1;
    new_node->next = NULL;

    list->head = new_node;
    list->tail = list->head;
    list->size = 0;

    return list;
}

void vm_list_insert_pid(vm_list* list,pid_t pid){
    virtual_memory mem;
    mem.pid = pid;
    mem.pages = (int*) calloc(NUM_PAGES,sizeof(int));
    mem.page_ptr = -1;
    
    struct vm_node* node = malloc(sizeof(struct vm_node));
    node->data = mem;
    list->tail->next =  node;
    list->tail = node;
    list->size++;

}

void* vm_list_increase_pages(vm_list* list, pid_t pid){
    struct vm_node* curr = list->head;

    while(curr != NULL && curr->data.pid != pid){
        curr = curr->next;
    }
    curr->data.page_ptr++;
    curr->data.pages[curr->data.page_ptr] = 1;
    return INDEX_TO_VIRTUAL_ADDR(curr->data.page_ptr);
}

virtual_memory vm_list_get(vm_list* list, pid_t pid){
    struct vm_node* curr = list->head;

    while(curr != NULL && curr->data.pid != pid){
        curr = curr->next;
    }

    return curr->data;

}

int vm_list_page_fault(vm_list* list, pid_t pid, void* vaddr){
    struct vm_node* curr = list->head;

    while(curr != NULL && curr->data.pid != pid){
        curr = curr->next;
    }
    int alloc = VIRTAL_ADDR_TO_INDEX(vaddr);
    if(curr->data.pages[alloc] == 1){
        curr->data.pages[alloc] = 2;
        return 1;
    }
    return 0;
}

void vm_list_remove_pid(vm_list* list, pid_t pid){
    
    if(list->size == 0){
        return;
    }

    struct vm_node* prev = list->head;
    struct vm_node* curr = list->head->next;

    while(curr != NULL && curr->data.pid != pid){
        prev = curr;
        curr = curr->next;
    }

    struct vm_node* removeable = curr;
    prev->next = curr->next;
    free(removeable);
    
}
//------------------------------------ SENCOND CHANCE ALGORITHM --------------------------------------------------------

int second_chance(){

};

//-------------------------- PAGER CORE --------------------------------------------------------------------------------

void pager_init(int nframes, int nblocks){
    frame.size = frame.free = nframes;
    block.size = block.free = nblocks;

    frame.page_t = (page*) malloc(sizeof(page) * nframes);
    block.page_t = (page*) malloc(sizeof(page) * nblocks);

    init_page_central(&frame);
    init_page_central(&block);

    manager = vm_list_create();
}

void pager_create(pid_t pid){

    vm_list_insert_pid(manager,pid);

}

void *pager_extend(pid_t pid){

    if(block.free == 0){
        return NULL;
    }
    return vm_list_increase_pages(manager,pid);
}

void pager_fault(pid_t pid, void *addr){  

    int frame_pos,block_pos;
    int in_frame = check_page_allocation(&frame,pid,addr,&frame_pos);
    int in_block = check_page_allocation(&frame,pid,addr,&block_pos);
    int exist = vm_list_page_fault(manager,pid,addr);

    if(!in_frame && !in_block){
        if(!exist){
            return;
        }
        if(frame.free > 0){
            int alloc_pos = frame.size - frame.free;
            frame.page_t[alloc_pos].vaddr = addr;
            frame.page_t[alloc_pos].pid = pid;
            frame.page_t[alloc_pos].options.in_frame = 1;
            frame.page_t[alloc_pos].options.permission = PROT_READ;
            frame.free--;
            mmu_zero_fill(alloc_pos);
            mmu_resident(pid,addr,alloc_pos,PROT_READ);
        }
        else{
            second_chance();
        }
    }
    else if(in_frame){
        frame.page_t[frame_pos].options.permission = PROT_READ | PROT_WRITE;
        mmu_chprot(pid,addr,PROT_READ | PROT_WRITE);
    }
   
}

int pager_syslog(pid_t pid, void *addr, size_t len){
    
    if(addr < UVM_BASEADDR || addr > UVM_MAXADDR){
        return -1;
    }
    
    int index = VIRTAL_ADDR_TO_INDEX(addr);
    virtual_memory mem = vm_list_get(manager,pid);
    if(index > mem.page_ptr){
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

    for(int i = 0; i < frame.size; i++){
        if(frame.page_t[i].pid == pid){
            frame.page_t[i].pid = -1;
            frame.page_t[i].vaddr = NO_ALLOC;
            frame.page_t[i].options.permission = 0;
            frame.page_t[i].options.in_frame = 0;
            frame.free++;
        }
    }

    for(int i = 0; i < block.size; i++){
        if(block.page_t[i].pid == pid){
            block.page_t[i].pid = -1;
            block.page_t[i].vaddr = NO_ALLOC;
            block.page_t[i].options.permission = 0;
            block.page_t[i].options.in_frame = 0;
            block.free++;
        }
    }

    vm_list_remove_pid(manager,pid);
}
