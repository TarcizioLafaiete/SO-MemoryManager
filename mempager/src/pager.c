#include "pager.h"
#include "mmu.h"

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/mman.h>

#define PAGE_SIZE sysconf(_SC_PAGESIZE)
#define NUM_PAGES (UVM_MAXADDR - UVM_BASEADDR + 1) / PAGE_SIZE
#define NO_ALLOC 0


#define VIRTUAL_ADDR_TO_INDEX(vaddr) (long) (((long) vaddr - UVM_BASEADDR) / PAGE_SIZE)
#define INDEX_TO_VIRTUAL_ADDR(idx) (void*) (UVM_BASEADDR + (idx * PAGE_SIZE))

//----------------------------- PAGE CENTRAL----------------------------------------------------------

/**
 * @brief Estrutura utilizada para guardar informações úteis para o algoritmo de segunda chance.
 * @param in_frame Define se a página está presente na memória física
 * @param permission Define se a página permite leitura e/ou escrita por parte dos processos.
 * 
 */
typedef struct{
    short in_frame;
    short permission;
} bits_array;

/**
 * @brief Representa uma página da memória virtual.
 * @param pid Especifica o processo que originou essa página.
 * @param options Define as opções da página (permissões, etc...)
 * @param vaddr Endereço virtual inicial da página
 * 
 */
typedef struct{
    pid_t pid;
    bits_array options;
    void* vaddr;
} page;

/**
 * @brief Estrutura que abriga várias páginas para a tradução pela MMU (Memory Management Unit).
 * @param page_t Conjunto de páginas pertencentes à tabela de página.
 * @param size Quantidade de páginas detidas pela tabela.
 * @param free Determina quantas páginas estão livres.
 * 
 */
typedef struct{
    page* page_t;
    int size;
    int free;
} page_central;

/**
 * @brief A tabela de páginas que estão presentes na memória principal (RAM)
 * 
 */
page_central frame;
/**
 * @brief A tabela de páginas que estão presentes no disco (HDD/SSD)
 * 
 */
page_central block;

/**
 * @brief Inicializa as páginas presentes em "page_t" com valores iniciais quaisquer
 * 
 * @param central A tabela de páginas que se quer inicializar
 */
void init_page_central(page_central *central){
    for(int i = 0 ;i < central->size; i++){
        central->page_t[i].pid = -1;
        central->page_t[i].vaddr = NO_ALLOC;
        central->page_t[i].options.in_frame = 0;
        central->page_t[i].options.permission = PROT_NONE;
    }
}

/**
 * @brief Verifica se, dado um "pid" e um "vaddr", a página de memória relativa a essas informações está presente na tabela informada.
 * 
 * @param central Tabela de páginas que se está buscando a página relativa ao processo.
 * @param pid Identificador do processo que detém a página de memória
 * @param vaddr Endereço virtual inicial relativo à página de memória
 * @param pos Ponteiro que armazenará a posição relativa à página de memória na tabela de páginas
 * @return int 0 - Indica que a página buscada não está presente na tabela. 1 - Indica que a página está na tabela.
 */
int check_page_allocation(page_central* central, pid_t pid, void* vaddr, int* pos){
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

/**
 * @brief Estrutura para identificar quais páginas um determinado processo solicitou a alocação para ele, sem necessariamente utilizá-las.
 * Essa é uma estrutura intermediária, de forma que quando um processo faz a chamada de "pager_extend()", a "virtual_memory" armazena a solicitação
 * de extensão daquele processo, evitando um "page_fault" em uma possível tentativa de acesso futura.
 * Vale ressaltar que é *memória virtual*, então diversos processos podem compartilhar a mesma posição de página.
 * @param pid Identificador do processo que detem essa memória virtual
 * @param pages Vetor de posições cuja alocação das páginas foi solicitada pelo processo
 * @param page_ptr Posição do vetor "pages", que contém a última página que teve a alocação solicitada pelo processo
 * 
 */
typedef struct{
    pid_t pid;
    int* pages;
    int page_ptr;
} virtual_memory;

/**
 * @brief Lista encadeada de memórias virtuais dos processos.
 * @param head Célula inicial da lista encadeada, contendo o primeiro processo que solicitou alocação.
 * @param tail Célula final da lista encadeada, contendo o último processo que solicitou alocação.
 * @param size Tamanho da lista
 * 
 */
typedef struct{
    struct vm_node* head;
    struct vm_node* tail;
    int size;
} vm_list;

/**
 * @brief Célula da lista encadeada.
 * @param data O conteúdo da célula, que abriga as informações de memória virtual do processo que solicitou a alocação de alguma página.
 * @param next A célula seguinte da lista encadeada.
 * 
 */
struct vm_node{
    virtual_memory data;
    struct vm_node* next;
};

/**
 * @brief Variável única de gerenciamento das solicitações de alocação de página
 * 
 */
vm_list* manager;

/**
 * @brief Construtor da lista encadeada de memória virtual dos processos.
 * Cada memória virtual possui 1024 páginas.
 * 
 * @return vm_list* 
 */
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

/**
 * @brief Cria uma nova instância de memória virtual para um processo, inicializada com valores padrão, que mostram que 
 * ainda não houve nenhuma tentativa de alocamento.
 * 
 * @param list A lista encadeada de memórias virtuais utilizadas para gerenciar o alocamento de páginas pelos processos
 * @param pid Indentificador do processo que alocará a nova instânica de memória virtual.
 */
void vm_list_insert_pid(vm_list* list, pid_t pid){
    virtual_memory mem;
    mem.pid = pid;
    mem.pages = (int*) calloc(NUM_PAGES, sizeof(int));
    mem.page_ptr = -1;

    struct vm_node* node = malloc(sizeof(struct vm_node));
    node->data = mem;
    list->tail->next = node;
    list->tail = node;
    list->size++;
}

/**
 * @brief Percorre a lista encadeada de memórias virtuais buscando aquela que corresponde ao processo que deseja alocar
 * mais páginas de memória. Ao encontrar essa memória virtual, incrementa o ponteiro da última página solicitada para alocação
 * e converte esse ponteiro para um índice de memória virtual, com base nos endereços pré-estabelecidos (0x0000600000, ...)
 * 
 * @param list Parâmetro global de gerenciamento de memória virtual "manager"
 * @param pid Identificador do processo ao qual será aumentada a quantidade de páginas na memória virtual
 * @return void* 
 */
void* vm_list_increase_pages(vm_list* list, pid_t pid){
    struct vm_node* curr = list->head;

    while(curr != NULL && curr->data.pid != pid){
        curr = curr->next;
    }
    curr->data.page_ptr++;
    curr->data.pages[curr->data.page_ptr] = 1;
    return INDEX_TO_VIRTUAL_ADDR(curr->data.page_ptr);
}

/**
 * @brief Essa função busca a instância de memória virtual associada ao processo requisitante, na estrutura de gerenciamento "manager".
 * Caso o processo exista e possua uma memória virtual, ela é retornada. Caso contŕario, a cabeça da lista encadeada é retornada.
 * 
 * @param list Parâmetro global de gerenciamento de memória virtual "manager"
 * @param pid Identificador do processo que se quer obter a memória virtual.
 * @return virtual_memory 
 */
virtual_memory vm_list_get(vm_list* list, pid_t pid){
    struct vm_node* curr = list->head;

    while(curr != NULL && curr->data.pid != pid){
        curr = curr->next;
    }

    return curr == NULL ? list->head->data : curr->data;
}

/**
 * @brief Verifica se a memória virtual relativa ao processo informado, possui o endereço recebido por parâmetro solicitado 
 * para alocação de página.
 * 
 * @param list Parâmetro global de gerenciamento de memória virtual "manager"
 * @param pid Identificador do processo que se quer verificar a alocação do endereço
 * @param vaddr Endereço a ser convertido em índice do vetor de páginas alocadas, para a verificação proposta pela função.
 * @return int 1 - Quando a página já foi solicitada para alocação. 0 - Caso contrário.
 */
int vm_list_check_extended_page(vm_list* list, pid_t pid, void* vaddr){
    struct vm_node* curr = list->head;

    while(curr != NULL && curr->data.pid != pid){
        curr = curr->next;
    }
    int alloc = VIRTUAL_ADDR_TO_INDEX(vaddr);
    if(curr->data.pages[alloc] == 1){
        curr->data.pages[alloc] = 2;
        return 1;
    }
    return 0;
}

/**
 * @brief Remove a instância de memória virtual associada ao processo da lista encadeada.
 * 
 * @param list Parâmetro global de gerenciamento de memória virtual "manager"
 * @param pid Identificador do processo que será removido da lista
 */
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

/**
 * @brief Define o tamanho da tabela de páginas na memória (frame) e no disco (block), aloca a quantidade de páginas relativas
 * a esse tamanho para ambas e as inicializa com valores padrão, retornando ao fim o gerenciador de alicação de páginas.
 * 
 * @param nframes Número total de quadros em memória principal
 * @param nblocks Número total de quadros em memória secundária
 */
void pager_init(int nframes, int nblocks){
    frame.size = frame.free = nframes;
    block.size = block.free = nblocks;

    frame.page_t = (page*) malloc(sizeof(page) * nframes);
    block.page_t = (page*) malloc(sizeof(page) * nblocks);

    init_page_central(&frame);
    init_page_central(&block);

    manager = vm_list_create();
}

/**
 * @brief Essa função cria um novo paginador de memória virtual para o processo informado.
 * 
 * @param pid Identificador do processo que se quer criar um paginador.
 */
void pager_create(pid_t pid){
    vm_list_insert_pid(manager, pid);
}

/**
 * @brief Verifica se a memória secundária está disponível, de forma que se não estiver, não ocorre a extensão de páginas da 
 * memória virtual do processo e é retornado nulo.
 * Vale lembrar que, para cada página alocada na memória principal pelo processo, também é definida uma página na memória secundária 
 * para transferência futura, se necessário. Dessa forma, antes do aumento no número de páginas, a quantidade de endereços libres da 
 * memória secundária "bloc" é decrementada.
 * 
 * @param pid Identificador do processo que alocará mais uma página na memória virtual
 * @return void* Endereço virtual convertido com base na alocação da página.
 */
void* pager_extend(pid_t pid){
    if(block.free == 0){
        return NULL;
    }

    block.free--;

    return vm_list_increase_pages(manager, pid);
}

/**
 * @brief Função para tratamento de falhas de página. É verificado se o endereço virtual que se quer acessar, está presente
 * na memória princial "in_frame" ou na secundária "in_block", ou se já houve ao menos a solicitação de alocação desse endereço
 * "exist".  
 * 
 * Quando o endereço não está em nenhuma das memórias, quer dizer que aquele é o primeiro acesso a ele. Dessa forma, é verificado
 * se ele já foi previamente alocado pelo gerenciador de memória virtual "manager", retornando sem realizar nenhuma ação em caso negativo.  
 * 
 * Dessa forma, se houver espaço o suficiente na memória principal, o endereço é alocado a ela. Caso contrário, é executado o algoritmo 
 * de segunda chance, buscando um elemento da memória principal a ser movido para a secundária e, dessa forma, permitir ao programa a
 * utilização do atual endereço.
 * 
 * Quando o endereço acessado já está na memória principal, as permissões dele são alteradas para leitura e escrita.
 * 
 * Quando o endereço acessado já está na memória secundária, ...
 * 
 * @param pid Identificadro do processo ao qual será tratada a falha de página ao acessar o endereço, se necessário.
 * @param addr Endereço relativo ao processo que se quer acessar.
 */
void pager_fault(pid_t pid, void *addr){
    int frame_pos, block_pos;
    int in_frame = check_page_allocation(&frame, pid, addr, &frame_pos);
    int in_block = check_page_allocation(&block, pid, addr, &block_pos);
    int exist = vm_list_check_extended_page(manager, pid, addr);

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
        mmu_chprot(pid, addr, PROT_READ | PROT_WRITE);
    }
}

/**
 * @brief Essa função é utilizada para imprimir os dados armazenados na memória como bytes (hexadecimais), a partir de um
 * endereço inicial até o tamanho total informado, sem verificação de permissão do processo em relação à região lida.
 * 
 * Para isso, verifica se o endereço que se quer acessar está dentro do intervalo de memória disponível, retornando "-1" caso negativo.
 * Após isso, obtém a instância de memória virtual relativa àquele aquele processo, verificando se o endereço solicitado já 
 * foi alocado.
 * 
 * @param pid Identificador do processo que contem o primeiro endereço, cujo conteúdo será exibido
 * @param addr Endereço da memória virtual contendo o início da região, cujos conteúdos serão exibidos
 * @param len Tamanho da região total que terá os conteúdos exibidos.
 * @return int -1 - Quando não foi possível realizar a leitura. 0 - Quando foi possível realizar a leitura.
 */
int pager_syslog(pid_t pid, void *addr, size_t len){
    int addr_under_base = ((long) addr < UVM_BASEADDR);
    int addr_above_max = ((long) addr > UVM_MAXADDR);
    if(addr_under_base || addr_above_max){
        return -1;
    }

    int index = VIRTUAL_ADDR_TO_INDEX(addr);
    virtual_memory mem = vm_list_get(manager, pid);
    if(index > mem.page_ptr){
        return -1;
    }

    void* vaddr = INDEX_TO_VIRTUAL_ADDR(index);
    const char* buf = (char*) malloc(sizeof(char) * len);
    buf = pmem + (index * PAGE_SIZE) + (addr - vaddr);
    for(int i = 0; i < len; i++){
        printf("%02x", (unsigned)buf[i]);
    }
    printf("\n");
    return 0;
}

/**
 * @brief Destrói todas as páginas relativas a um processo, tanto na tabela de páginas da memória principal
 * quanto da secundária, removendo a memória virtual associada a ele ao final.
 * 
 * @param pid Identificador do processo que foi finalizado, tendo a memória desalocada.
 */
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
