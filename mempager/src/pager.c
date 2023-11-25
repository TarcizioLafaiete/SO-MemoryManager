#include "pager.h"
#include "mmu.h"

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/mman.h>
#include <pthread.h>

#define PAGE_SIZE sysconf(_SC_PAGESIZE)
#define NUM_PAGES (UVM_MAXADDR - UVM_BASEADDR + 1) / PAGE_SIZE
#define NO_ALLOC 0


#define VIRTUAL_ADDR_TO_INDEX(vaddr) (long) (((long) vaddr - UVM_BASEADDR) / PAGE_SIZE)
#define INDEX_TO_VIRTUAL_ADDR(idx) (void*) (UVM_BASEADDR + (idx * PAGE_SIZE))
#define NORM_VIRTUAL_ADDR(vaddr) INDEX_TO_VIRTUAL_ADDR(VIRTUAL_ADDR_TO_INDEX(vaddr))

/**
 * @brief Mutex utilizado para evitar condições de corrida entre acessos de processos diferentes
 * 
 */
pthread_mutex_t lock;
//----------------------------- PAGE CENTRAL----------------------------------------------------------

/**
 * @brief Estrutura utilizada para guardar os bits de informações de acesso da página, sendo úteis para o algoritmo de segunda chance.
 * @param write_op Indica se já ocorreu uma operação de escrita na página no passado,
 * @param permission Armazena as permissões atuais da página, indicando se é possível ler e escrever nela, por exemplo.
 * @param reference_bit Bit utilizado no algoritmo de segunda chance para definir a pagina retirada da mêmoria.
 * 
 */
typedef struct{
    short write_op;
    short permission;
    short reference_bit;
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
 * @brief Ponteiro de Segunda chance, sempre aponta para uma posição de mêmoria e procura por uma vítima.
 * 
 */
int sc_ptr;
/**
 * @brief Inicializa as páginas presentes em "page_t" com valores iniciais quaisquer
 * 
 * @param central A tabela de páginas que se quer inicializar
 */
void init_page_central(page_central *central){
    for(int i = 0 ;i < central->size; i++){
        central->page_t[i].pid = -1;
        central->page_t[i].vaddr = NO_ALLOC;
        central->page_t[i].options.write_op = 0;
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

void clean_page(page_central* central,int block_pos){
    central->page_t[block_pos].pid = -1;
    central->page_t[block_pos].vaddr = NO_ALLOC;
    central->page_t[block_pos].options.write_op = 0;
    central->page_t[block_pos].options.permission = 0;
    central->page_t[block_pos].options.reference_bit = 0;
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
    int alloced = curr->data.pages[alloc] & 0x03;
    if(alloced){
        // curr->data.pages[alloc] |= 0x02;
        return 1;
    }
    return 0;
}

/**
 * @brief Recebe uma página de mêmoria e salva seus dados no gerenciador "manager". Salvo a permissão de page nos bits 3 e 4 do inteiro respectivo
 * aquela pagina
 * 
 * @param list - Parâmetro global de gerenciamento de memória virtual "manager" 
 * @param to_save - Pagina que se deseja salvar os dados na mêmoria
 */
void vm_list_save_page(vm_list* list, page to_save){
    struct vm_node* curr = list->head;

    while(curr != NULL && curr->data.pid != to_save.pid){
        curr = curr->next;
    }

    int idx = VIRTUAL_ADDR_TO_INDEX(to_save.vaddr);
    short permission = to_save.options.permission << 2;
    curr->data.pages[idx] |= permission;
    curr->data.pages[idx] &= 0xFD;

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
    list->size--;
    free(removeable);
    
}

//------------------------------------ SENCOND CHANCE ALGORITHM --------------------------------------------------------
/**
 * @brief Procura na tabela de paginas presentes na mémoria principal por algum frame que possui o bit de referência como 0 para ser a proxima vitima
 * do do paginador e ser retirado da memoria. A cada pagina que possui um bit 1 é dada uma segunda chance e seu bit é colocado como 0
 * 
 * @return int - Posicao relativa da pagína de mêmoria que deverá ser retirada da mêmoria.
 */
int second_chance(){
    while(1){
        if(sc_ptr == frame.size){
            sc_ptr = 0;
        }

        if(frame.page_t[sc_ptr].options.reference_bit){
            mmu_chprot(frame.page_t[sc_ptr].pid,frame.page_t[sc_ptr].vaddr,PROT_NONE);
            frame.page_t[sc_ptr].options.permission = PROT_NONE;
            frame.page_t[sc_ptr].options.reference_bit = 0;
            sc_ptr++;
        }
        else{
            sc_ptr++;
            return sc_ptr - 1;
        }
    }
};
/**
 * @brief Recebe a posição relativa a pagina que deve ser retirada da mêmoria e a nova pagína que deve ser escrita na mèmoria principal, caso neste
 * processo todas as páginas sejam "novas" na mêmoria, realizamos a troca de permissão destas para PROT_NONE. Em seguida retiramos a pagina desejada 
 * da mêmoria e caso ela não possua permissão de escrita, salvamos ela através de vm_list_save_page no "manager" e em seguida inicializamos a nova 
 * página no espaço da mêmoria principal
 * 
 * @param remove_pos  - Posição relativa na mêmoria ao frame que será retirado
 * @param new_page - Pagina que irá ocupar o espaço de mêmoria da pagina removida
 * @param new_page_origin - Se 1 ela foi originada do disco, caso 0 sua origem é do "manager".
 */
void realloc_pages(int remove_pos,page new_page,int new_page_origin, int block_pos){
    
    page removed_page = frame.page_t[remove_pos]; 

    mmu_nonresident(removed_page.pid,removed_page.vaddr);
    removed_page.options.permission = PROT_READ;

    if(removed_page.options.write_op == 0){
        vm_list_save_page(manager,removed_page);
    }
    else{
        block.page_t[remove_pos] = removed_page;
        mmu_disk_write(remove_pos,remove_pos);
    }

    frame.page_t[remove_pos] = new_page;
    if(new_page_origin == 1){
        clean_page(&block,block_pos);
        mmu_disk_read(block_pos,remove_pos);
        mmu_resident(new_page.pid,new_page.vaddr,remove_pos,PROT_READ);   
    }
    else{
        mmu_zero_fill(remove_pos);
        mmu_resident(new_page.pid,new_page.vaddr,remove_pos,new_page.options.permission);
    }
}


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
    sc_ptr = 0;


    init_page_central(&frame);
    init_page_central(&block);

    manager = vm_list_create();
    pthread_mutex_init(&lock,NULL);
}

/**
 * @brief Essa função cria um novo paginador de memória virtual para o processo informado.
 * 
 * @param pid Identificador do processo que se quer criar um paginador.
 */
void pager_create(pid_t pid){
    pthread_mutex_trylock(&lock);
    vm_list_insert_pid(manager, pid);
    pthread_mutex_unlock(&lock);
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
    pthread_mutex_trylock(&lock);
    if(block.free == 0){
        pthread_mutex_unlock(&lock);
        return NULL;
    }

    block.free--;
    void* addr = vm_list_increase_pages(manager,pid);
    pthread_mutex_unlock(&lock);
    return addr;
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
 * Quando o endereço acessado já está na memória principal, as permissões dele são alteradas gradualmente a cada acesso. Seguindo a ordem
 * PROT_NONE -> PROT_READ -> PROT_READ | PROT_WRITE
 * 
 * Quando o endereço acessado já está na memória secundária, executamos o algoritmo de segunda chance, buscando o elemento a ser removido. Em seguida
 * transferimos a pagina do disco para o espaço de frame definido, e a pagina removida recebe seu devido tratamento.
 * 
 * @param pid Identificadro do processo ao qual será tratada a falha de página ao acessar o endereço, se necessário.
 * @param addr Endereço relativo ao processo que se quer acessar.
 */
void pager_fault(pid_t pid, void *addr){
    pthread_mutex_trylock(&lock);
    int frame_pos,block_pos;
    int remove_pos;
    short page_permission;
    page new_page;
    
    addr = NORM_VIRTUAL_ADDR(addr);
    int in_frame = check_page_allocation(&frame,pid,addr,&frame_pos);
    int in_block = check_page_allocation(&block,pid,addr,&block_pos);
    int exist = vm_list_check_extended_page(manager,pid,addr);
    // printf("in_frame; %d, in_block: %d, exist: %d \n",in_frame,in_block,exist);


    if(!in_frame && !in_block){
        if(!exist){
            pthread_mutex_unlock(&lock);
            return;
        }

        new_page.pid = pid;
        new_page.vaddr = addr;
        new_page.options.write_op = 0;
        new_page.options.permission = PROT_READ;
        
        if(frame.free > 0){
            int alloc_pos = frame.size - frame.free;
            frame.page_t[alloc_pos] = new_page;
            frame.free--;
            mmu_zero_fill(alloc_pos);
            mmu_resident(pid,addr,alloc_pos,PROT_READ);
        }
        else{
            remove_pos = second_chance();
            realloc_pages(remove_pos,new_page,0,NULL);
            
        }
    }
    else if(in_frame){
        if(frame.page_t[frame_pos].options.permission == PROT_NONE){
            frame.page_t[frame_pos].options.permission = PROT_READ;
        }
        else if(frame.page_t[frame_pos].options.permission == PROT_READ){
            frame.page_t[frame_pos].options.write_op = 1;
            frame.page_t[frame_pos].options.permission = PROT_WRITE | PROT_READ;
        }
        new_page.options.reference_bit = 1;
        mmu_chprot(pid,addr,frame.page_t[frame_pos].options.permission);
    }

    else if(in_block){
        remove_pos = second_chance();
        new_page = block.page_t[block_pos];
        new_page.options.reference_bit = 1;
        realloc_pages(remove_pos,new_page,1,block_pos);

    }
    else{
        printf("F*deu geral, tem pagina na memoria e no disco AO MESMO TEMPO \n");  
        exit(EXIT_FAILURE);
    }
    pthread_mutex_unlock(&lock);
}

/**
 * @brief Essa função é utilizada para imprimir os dados armazenados na memória como bytes (hexadecimais), a partir de um
 * endereço inicial até o tamanho total informado, sem verificação de permissão do processo em relação à região lida.
 * 
 * Para isso, verifica se o endereço que se quer acessar está dentro do intervalo de memória disponível, retornando "-1" caso negativo.
 * Após isso, obtém a instância de memória virtual relativa àquele processo, verificando se o endereço solicitado já 
 * foi alocado.
 * 
 * @param pid Identificador do processo que contem o primeiro endereço, cujo conteúdo será exibido
 * @param addr Endereço da memória virtual contendo o início da região, cujos conteúdos serão exibidos
 * @param len Tamanho da região total que terá os conteúdos exibidos.
 * @return int -1 - Quando não foi possível realizar a leitura. 0 - Quando foi possível realizar a leitura.
 */
int pager_syslog(pid_t pid, void *addr, size_t len){
    pthread_mutex_trylock(&lock);
    int addr_under_base = ((long) addr < UVM_BASEADDR);
    int addr_above_max = ((long) addr > UVM_MAXADDR);
    if(addr_under_base || addr_above_max){
        pthread_mutex_unlock(&lock);
        return -1;
    }
    int index = VIRTUAL_ADDR_TO_INDEX(addr);
    virtual_memory mem = vm_list_get(manager, pid);
    if(index > mem.page_ptr){
        pthread_mutex_unlock(&lock);
        return -1;
    }

    void* vaddr = INDEX_TO_VIRTUAL_ADDR(index);
    const char* buf = (char*) malloc(sizeof(char) * len);
    buf = pmem + (index * PAGE_SIZE) + (addr - vaddr);
    for(int i = 0; i < len; i++){
        printf("%02x", (unsigned)buf[i]);
    }
    printf("\n");
    pthread_mutex_unlock(&lock);
    return 0;
}

/**
 * @brief Destrói todas as páginas relativas a um processo, tanto na tabela de páginas da memória principal
 * quanto da secundária, removendo a memória virtual associada a ele ao final.
 * 
 * @param pid Identificador do processo que foi finalizado, tendo a memória desalocada.
 */
void pager_destroy(pid_t pid){

    pthread_mutex_trylock(&lock);
    for(int i = 0; i < frame.size; i++){
        if(frame.page_t[i].pid == pid){
            frame.page_t[i].pid = -1;
            frame.page_t[i].vaddr = NO_ALLOC;
            frame.page_t[i].options.permission = 0;
            frame.page_t[i].options.write_op = 0;
            frame.free++;
        }
    }

    for(int i = 0; i < block.size; i++){
        if(block.page_t[i].pid == pid){
            block.page_t[i].pid = -1;
            block.page_t[i].vaddr = NO_ALLOC;
            block.page_t[i].options.permission = 0;
            block.page_t[i].options.write_op = 0;
            block.free++;
        }
    }

    vm_list_remove_pid(manager,pid);
    pthread_mutex_unlock(&lock);
}
