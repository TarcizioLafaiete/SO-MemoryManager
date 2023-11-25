# PAGINADOR DE MEMÓRIA -- RELATÓRIO

## Termo de compromisso
Ao entregar este documento preenchido, os membros do grupo afirmam que todo o código desenvolvido para este trabalho é de autoria própria. Exceto pelo material listado no item 3 deste relatório, os membros do grupo afirmam não ter copiado material da Internet nem ter obtido código de terceiros.

## Membros do grupo e alocação de esforço
Preencha as linhas abaixo com o nome e o email dos integrantes do grupo.  Substitua marcadores `XX` pela contribuição de cada membro do grupo no desenvolvimento do trabalho (os valores devem somar 100%).
- Gabriel Bifano Freddi \<gabrielfev@outlook.com\> 33%
- Pedro de Oliveira Guedes \<pedro.og2002@gmail.com\> 33%
- Tarcizio Augusto Santos Lafaiete \<tarcizio-augusto@hotmail.com\> 34%

## Detalhes de implementação
Ao longo desta seção, serão discutidos detalhes de implementação, buscando tornar a compreensão do código e sua confecção mais acessível a todos. Além das informações presentes neste documento, as funções e estruturas de dados também foram amplamente documentadas ao longo do arquivo `pager.c`.

### Estruturas de dados utilizadas
Grande parte das estruturas de dados utilizadas para o desenvolvimento do trabalho foram confeccionadas pelo grupo, para que, dessa forma, fosse possível ter mais controle sobre o funcionamento e integrações do código. 

A seguir, cada uma das estruturas de dados será listada, descrita e terá o propósito justificado.

#### Tabela de páginas
- **`bits_array`**
    - **Descrição:** Estrutura de dados com informações da página, como se está presente na memória física e se permite leitura e/ou escrita.
    - **Justificativa:** Essa estrutura garante a integridade de acesso às páginas pelo paginador, além de guardar a informação `reference_bit`, que é utilizada pelo algoritmo de segunda chance ao selecionar as páginas que irão para o disco.

- **`page`**
    - **Descrição:** Estrutura para representar uma página de memória que armazena informações como o PID do processo que a criou, as opções descritas pela estrutura `bit_array` anteriormente descrita e o endereço de início da página.
    - **Justificativa:** Essa estrutura é o alvo do paginador de memória, sendo utilizada constantemente para guardar as informações de alocação.

- **`page_central`**
    - **Descrição:** Essa estrutura representa a **tabela de páginas** estudada, que armazena as páginas alocadas, bem como a quantidade total e o volume de espaços livres.
    - **Justificativa:** Essa estrutura é utilizada para gerenciar a memória principal (RAM) e secundária do sistema (ROM), de forma que toda página alocada é referenciada por uma variável deste tipo.
    - **Funções associadas:** Essa estrutura possui três funções para coordenar o acesso a ela, garantindo que ela seja utilizada da forma esperada e adicionando maior nível de abstração às operações do sistema.
        - **`init_page_central`:** Inicializa as páginas da tabela com valores iniciais quaisquer.
        - **`check_page_allocation`:** Verifica se uma página de memória específica (alocada por processo com determinado PID e com determinado endereço inicial) está presente na tabela informada.
        - **`clean_page`:** Desaloca uma página na tabela, deixando a posição livre para que outros processos possam alocar uma nova página.

#### Memória virtual
- **`virtual_memory`**
    - **Descrição:** Essa estrutura representa a memória virtual de um processo, que contem as páginas cuja alocação foi solicitada por aquele processo. Ou seja, essa estrutura atua como um intermediário entre a solicitação de páginas e a real alocação delas, de forma que a página somente será realmente alocada quando o usuário ativamente utilizá-la.
    - **Justificativa:** Essa estrutura é utilizada para gerenciar as diversas solicitações de alocação de páginas pelos diversos programas, evitando que ocorram falhas de página (`page_fault`) nos acessos durante a execução dos programas.

- **`struct vm_node`**
    - **Descrição:** Célula de uma lista **unicamente encadeada**, armazenando as memórias virtuais dos processos.
    - **Justificativa:** A decisão de implementação do gerenciador de páginas dos processos foi de implementar uma lista unicamente encadeada, dessa forma, foi necessário criar uma estrutura que armazenasse a memória virtual e apontasse a próxima estrutura da cadeia.

- **`vm_list`**
    - **Descrição:** Lista encadeada contendo as memórias virtuais associadas aos diversos processos em execução. Na implementação feita pelo grupo, a célula `head` é inutilizada, servindo como um valor de retorno padrão quando o objetivo de alguma função não é cumprido. Cada memória virtual de processo possui 1.024 páginas.
    - **Justificativa:** A implementação de uma lista encadeada própria do grupo permite maior flexibilidade nas funções, que podem funcionar de forma simplificada.
    - **Funções associadas:** Essa estrutura possui sete funções para coordenar a criação, acesso e remoção de itens dela, garantindo que ela seja utilizada da forma esperada e adicionando maior nível de abstração às operações do sistema.
        - **`vm_list_create`:** Cria a lista encadeada de memórias virtuais, atribuindo valores inválidos padrão à célula `head`.
        - **`vm_list_insert_pid`:** Cria uma nova instância de memória virtual para o processo solicitante, inicializando-a com valores padrão e adicionando ao final da lista encadeada recebida como parâmetro.
        - **`vm_list_increase_pages`:** Solicita a alocação de uma nova página para um processo. Para isso, percorre a lista encadeada buscando pela memória virtual relativa ao processo, quando ela é encontrada, o ponteiro para a última posição alocada é incrementado e o endereço virtual relativo àquela posição é retornado.
        - **`vm_list_get`:** Busca a instância de memória virtual relativa ao processo alvo e, caso ela exista, ela é retornada, caso contrário, a célula `head` da lista de memórias é retornada como um valor padrão.
        - **`vm_list_check_extended_page`:** Verifica se a memória virtual relativa ao processo alvo possui uma solicitação de alocação de página com o endereço virtual recebido.
        - **`vm_list_save_page`:** Recebe uma página de memória e a salva na memória virtual do processo alvo, realizando operações *bitwise* para demonstrar a ocupação.
        - **`vm_list_remove_pid`:** Remove a memória virtual associada ao processo alvo, indicando a finalização da execução do mesmo.

#### Política de reposição de páginas
Quando a memória principal está cheia e um processo necessita alocar mais memória, as páginas da memória RAM são enviadas à memória secundária para disponibilizar espaço para a continuação do funcionamento dos programas. Para selecionar quais páginas da memória principal devem ser enviadas a secundária, é utilizado o *Algoritmo de segunda chance*.

O algoritmo de segunda chance foi implementado pelo grupo por ser o especificado como necessário na descrição do trabalho prático.


Para o funcionamento completo da política de reposição de páginas, foram implementadas duas funções, que serão discutidas a seguir.

- **`second_chance`:** Essa função é o algoritmo de segunda chance em si, que itera pelas páginas alocadas na memória principal, buscando uma que possua o bit de referência igual a 0, indicando que aquela página será a escolhida para ser retirada da mémoria principal. Nos casos em que o reference_bit for igual a 1, comuta este para zero e retira da página as permissões de escrita e leitura, colocando ela em PROT_NONE.

- **`realloc_pages`:** Remove uma página presente na posição informada da memória principal, e em casos em que já ocorreu  escrita da página, enviando-a para a memória secundária e substituindo-a por uma nova página alocada, ou uma que está contida no disco.

---

As demais funções implementadas no arquivo `pager.c` já tiveram as funcionalidades esperadas, objetivos e justificativas amplamente discutidas na especificação do presente trabalho, portanto, não serão mencionadas no decorrer deste documento. Caso seja necessário um entendimento melhor sobre as mesmas, todas possuem comentários extensos escritos diretamente no arquivo de implementação.

### Mecanismo de controle de acesso e modificação das páginas
O acesso e modificação de páginas é controlado em dois aspectos diferentes, o primeiro é quanto a concorrência das diversas threads nos acessos à memória, o segundo é quanto às permissões que cada página apresenta para leitura e modificação. Como a especificação do trabalho prático não é direta quanto a qual delas deve ser abordada nesta seção, ambas serão descritas em subtítulos abaixo.

#### Acesso concorrente
Como grande parte dos Sistemas Operacionais contemporâneos operam em ambientes *multithread*, é necessário que exista algum mecanismo para controlar o acesso e modificação concorrente das páginas, evitando que ocorram conflitos.

Para solucionar este problema, foi utilizado um Mutex da biblioteca `pthread`, que bloqueia o acesso à memória sempre que uma função do paginador é invocada, liberando-a antes de retornar ao final.

#### Controle de permissão das páginas
O controle de permissão das páginas é coordenado pela estrutura `bits_array`, que foi descrita anteriormente. Essa estrutura armazena variáveis que indicam o estado das opções da página no instante de acesso, armazenando as variáveis `write_op`, `permission` e `reference_bit`.

A variável `permission` em específico armazena os valores de permissão definidos no módulo `<sys/mman.h>`.

A variável `write_op` indica se esta página já passou por algum processo de escrita, isto é fundamental para o processo de realocação de páginas pois, caso o write_op seja igual 1 então a necessidade de guardar este processo em disco para preservar seus dados, caso ele não tenha sido escrito ainda, pode-se desaloca-lo sem a necessidade de salvar seus dados.

Por fim, o `reference_bit` é parte essencial do algoritmo de segunda chance, sendo que ele é o bit observado no programa para dar ou não a segunda chance ao processo na mémoria, ele é habilitado como 1 toda vez que há um novo acesso aquela página e se torna 0 quando o algoritmo permite a ele uma segunda chance.

## Referências bibliográficas
Os seguintes recursos foram utilizados para o desenvolvimento deste trabalho:
- < Educative.io >. Disponível em: \<https://www.educative.io/answers/what-is-the-second-chance-algorithm\>
- < IBM >. Disponível em: \<https://www.ibm.com/docs/pt-br/aix/7.3?topic=programming-using-mutexes\>
