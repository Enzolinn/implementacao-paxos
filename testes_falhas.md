# Casos de Teste de Falha - Paxos

Este documento descreve cenários de falha para validar a robustez do sistema Paxos implementado. Os mecanismos para lidar com as falhas ainda precisam ser implementados

## 1. Client envia valor inválido

- **Descrição:** O client envia um valor que não está no array de estados conhecidos dos nós (ex: 888, -1, 0).
- **Como testar:**  
  Altere o array de valores no client:
  ```c
  int valores[] = {42, 888, 7, -1, 56};
  ```
- **Esperado:**  
  O líder (e os nós) detectam o valor inválido, ignoram ou logam erro, e nenhum consenso é atingido para esse valor.

---

## 2. Nó com estados diferentes

- **Descrição:** Um ou mais nós possuem arrays `known_states` diferentes dos demais.
- **Como testar:**  
  Altere manualmente o array `known_states` em um dos nodes, por exemplo:
  ```c
  static int known_states[KNOWN_STATES] = {1, 2, 3, 4, 5};
  ```
- **Esperado:**  
  Se o valor proposto não estiver presente na maioria dos nós, não há consenso.

---

## 3. Nó rejeita propositalmente todos os valores

- **Descrição:** Um nó é programado para nunca aceitar nenhum valor (simula comportamento bizantino).
- **Como testar:**  
  No trecho de verificação do valor proposto, force `aceito = 0;` em um node.
- **Esperado:**  
  O sistema ainda atinge consenso se houver maioria honesta.

---

## 4. Client repete o mesmo valor várias vezes

- **Descrição:** O client envia o mesmo valor repetidamente.
- **Como testar:**  
  Altere o array de valores no client para repetir valores:
  ```c
  int valores[] = {42, 42, 42, 42, 42};
  ```
- **Esperado:**  
  O sistema aceita múltiplos consensos para o mesmo valor, ou ignora duplicatas.

---

## 5. Líder propõe valor diferente do recebido

- **Descrição:** O líder, por bug ou malícia, envia um valor diferente do recebido do client para os outros nós.
- **Como testar:**  
  No líder, altere `proposal_value` para um valor diferente do recebido do client.
- **Esperado:**  
  Os nós rejeitam se o valor não estiver em seus arrays.

---

## 6. Nó trava (não responde PROMISE ou ACCEPTED)

- **Descrição:** Um nó simplesmente não responde a mensagens Paxos.
- **Como testar:**  
  Em um node, faça o processo dormir ou entrar em loop infinito ao receber PREPARE/ACCEPT.
- **Esperado:**  
  O consenso é atingido se houver maioria funcional.

---

## 7. Dois líderes simultâneos (split brain)

- **Descrição:** Simule dois líderes ativos ao mesmo tempo (forçando eleição dupla).
- **Como testar:**  
  Force dois nodes a se declararem líderes (altere a lógica de eleição para fins de teste).
- **Esperado:**  
  Pode haver disputa, mas o protocolo deve garantir segurança (não há dois valores diferentes aceitos por maioria).

---

## 8. Falhas já implementadas via FAIL_CASE

- **FAIL_CASE=2:** Líder cai após receber a primeira proposta.
- **FAIL_CASE=3:** Nó não-líder cai durante execução.

