# implementacao-paxos
trabalho final da materia de Ubiquous computing 2025-1. O projeto implementa o algoritmo de consenso Paxos distribuído em C, simulando 5 nós, um cliente e um monitor de eventos. Cada nó executa threads para comunicação, eleição de líder, consenso e monitoramento de falhas.

# para rodar
gcc -o main main.c
./main

# .sh's
    limpar.sh limpa os compilados
    para rodar  tem que tornalo executavel usando o comando:
    chmod +x limpar.sh

    depois apenas ./limpar.sh

---

## Estruturas e Funções

### Estruturas

- `msg`: Estrutura de mensagem trocada entre nós, contendo tipo, origem, número e valor da proposta.
- `msg_queue`: Fila de mensagens thread-safe para comunicação interna entre threads.

### Funções de Fila

- `queue_init`: Inicializa a fila de mensagens e seus mutexes/condições.
- `enqueue`: Adiciona mensagem à fila de forma thread-safe.
- `dequeue`: Remove mensagem da fila, bloqueando se estiver vazia.

### Comunicação

- `send_msg`: Envia uma mensagem TCP para outro nó.
- `send_monitor`: Envia evento UDP para o monitor.
- `inform_client`: Informa ao cliente qual nó foi eleito líder.
- `send_client_ok`: Envia confirmação ao cliente após consenso.

---

## Lógica de Threads

Cada nó executa múltiplas threads para paralelizar as tarefas:

### 1. **listener**
- Aceita conexões TCP de outros nós.
- Recebe mensagens e as coloca na fila interna (`inbox`).
- Atualiza timestamp do último heartbeat recebido.

### 2. **election**
- Inicia eleição de líder.
- Envia candidatura, coleta respostas, determina o líder e informa o cliente.
- Se for líder, inicia thread para escutar propostas do cliente.

### 3. **paxos**
- Implementa o protocolo Paxos: prepara, coleta promessas, aceita valores e coleta confirmações.
- Só o líder processa propostas do cliente.
- Nós não-líderes respondem a PREPARE/ACCEPT.

### 4. **heartbeat_sender**
- O líder envia periodicamente mensagens de heartbeat para os outros nós.

### 5. **leader_monitor**
- Nós não-líderes monitoram o heartbeat do líder.
- Se o líder falhar, inicia nova eleição.

### 6. **client_listener** (apenas no líder)
- Escuta conexões do cliente para receber propostas de valores.

---

## Fluxo Resumido

1. **Monitor** inicia e escuta eventos.
2. **Nós** iniciam, fazem eleição de líder.
3. **Cliente** recebe o líder, envia propostas.
4. **Líder** executa Paxos para cada proposta.
5. **Monitor** registra todos os eventos.
6. **Se o líder falhar**, nova eleição é disparada automaticamente.

---

No projeto, cada **nó** (node1.c ... node5.c), o **cliente** (client.c) e o **monitor** (monitor.c) são executados como **processos separados** pelo sistema operacional, criados via `fork` em `main.c`. Ou seja, cada um roda de forma independente.

**Dentro de cada processo de nó**, são criadas **múltiplas threads** usando a biblioteca POSIX Threads (`pthread_create`). Cada thread executa uma função diferente (por exemplo, `listener`, `election`, `paxos`, etc.), permitindo que o nó realize várias tarefas simultaneamente, como escutar mensagens, monitorar o líder e executar o consenso.

