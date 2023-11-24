# PAGINADOR DE MEMÓRIA -- RELATÓRIO

## Termo de compromisso
Ao entregar este documento preenchido, os membros do grupo afirmam que todo o código desenvolvido para este trabalho é de autoria própria.  Exceto pelo material listado no item 3 deste relatório, os membros do grupo afirmam não ter copiado material da Internet nem ter obtido código de terceiros.

## Membros do grupo e alocação de esforço
Preencha as linhas abaixo com o nome e o email dos integrantes do grupo.  Substitua marcadores `XX` pela contribuição de cada membro do grupo no desenvolvimento do trabalho (os valores devem somar 100%).
- Gabriel Bifano Freddi \<gabrielfev@outlook.com\> 33%
- Pedro de Oliveira Guedes \<pedro.og2002@gmail.com\> 33%
- Tarcizio Augusto Santos Lafaiete \<tarcizio-augusto@hotmail.com\> 34%

## Referências bibliográficas
Os seguintes recursos foram utilizados para o desenvolvimento deste trabalho:
- < Nome do site >. Disponível em: \<link do site\>

## Detalhes de implementação
Ao longo desta seção, serão discutidos detalhes de implementação, buscando tornar a compreensão do código e sua confecção mais acessível a todos. Além das informações presentes neste documento, as funções e estruturas de dados também foram amplamente documentadas ao longo do arquivo `pager.c`.

### Estruturas de dados utilizadas
Grande parte das estruturas de dados utilizadas para o desenvolvimento do trabalho foram confeccionadas pelo grupo, para que, dessa forma, fosse possível ter mais controle sobre o funcionamento e integrações do código. 

A seguir, cada uma das estruturas de dados será listada, descrita e terá o propósito justificado.
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

- **virtual_memory**
    - **Descrição:** aaaa
    - **Justificativa:** aaaa

- **bits_array**
    - **Descrição:** aaaa
    - **Justificativa:** aaaa

Descreva e justifique as estruturas de dados utilizadas em sua solução.

### Mecanismo de controle de acesso e modificação das páginas
Descreva o mecanismo utilizado para controle de acesso e modificação às páginas.
