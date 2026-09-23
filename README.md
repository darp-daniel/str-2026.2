# Projeto STR - Desenvolvimento de Aplicações em Sistemas em Tempo Real

**Sistema de telemetria veicular com dashboard**

## 1. Definição do sistema

O sistema consiste em um sistema em tempo real para monitoramento periódico de sensores de um veículo, interpretar alterações relevantes e disponibilizar os resultados dessas interpretações em um dashboard. Além disso, o programa é capaz de processar eventos e comandos externos. Trazendo segurança e confiabilidade para o usuário do veículo.

***

## 2. Tarefas

* Coleta e Análise de dados dos sensores
* Eventos
* Comandos
* Mensagens
* Dashboard

### Definições das Tarefas

* **Sensores**
  * **Tipo:** Periódico
  * **Função:** Coleta de dados dos sensores do veículo
  * **Prioridade:** Alta

* **Interpretação**
  * **Tipo:** Periódico
  * **Função:** Transformação dos dados em informação para o próximo passo do sistema, com condições de interpretação e geração de eventos.
  * **Prioridade:** Alta

* **Eventos**
  * **Tipo:** Esporádica
  * **Função:** Geração de eventos por meio da interpretação dos dados dos sensores, gerando interrupções no sistema e acionando comandos e envio de mensagens para o usuário.
  * **Prioridade:** Alta (Muito)

* **Mensagem**
  * **Tipo:** Aperiódica -> Cria + Transmite
  * **Função:** A depender do evento gerado, o usuário recebe uma mensagem pelo dashboard
  * **Prioridade:** Média

* **Comandos**
  * **Tipo:** Aperiódica
  * **Função:** O usuário comanda algo para o veículo que altera seu estado e gera eventos.
  * **Prioridade:** Alta (Muito)

* **Dashboard**
  * **Tipo:** Periódica
  * **Função:** Atualização do dashboard com novos dados coletados dos sensores
  * **Prioridade:** Baixa

---

### 2.1 Coleta de dados dos sensores

* **Ignição:** ON / OFF (DIGITAL IO)
* **Bloqueio:** ON / OFF (DIGITAL IO)
* **Alarme:** ON / OFF (DIGITAL IO)
* **Bateria:** Valor analógico (Medido por sensor de tensão)
* **Painel:** ON / OFF (DIGITAL IO)

### 2.2 Interpretação de dados dos sensores

* **Ignição ON** $\rightarrow$ Veículo ligado
* **Ignição OFF** $\rightarrow$ Veículo desligado
* **Bloqueio ON** $\rightarrow$ Bloqueado *
* **Bloqueio OFF** $\rightarrow$ Desbloqueado
* **Painel ON** $\rightarrow$ Veículo conforme
* **Painel OFF** $\rightarrow$ Veículo violado *
* **Alarme ON** $\rightarrow$ Alarme acionado
* **Alarme OFF** $\rightarrow$ Alarme desligado

### 2.3 Eventos de resultado das interpretações

| Evento | Condição | Criticidade | Ação |
| :--- | :--- | :--- | :--- |
| Veículo ligado | $Ig_{OFF} \rightarrow ON$ | Baixa | Atualiza dash |
| Veículo desligado | $Ig_{ON} \rightarrow OFF$ | Baixa | Atualiza dash |
| Bloqueado | $B_{OFF} \rightarrow ON$ | Alta | Alarme + Mens + Dash |
| Bloqueado | $Painel_{ON} \rightarrow OFF$ | Alta | Alarme + Mens + Dash |
| Alarme atv | $Al_{OFF} \rightarrow ON$ | Média | Mensagem + Dash |
| Alarme des | $Al_{ON} \rightarrow OFF$ | Baixa | Atualiza Dash |
| Bateria Baixa | $Bt \le 2.5V$ | Média | Mens. + Atualiza Dash |
| Bateria Crítica | $Bt \le 1.5V$ | Alta | Mens. + Atualiza Dash |

### 2.4 Mensagens

| Mensagem | Evento | Criticidade |
| :--- | :--- | :--- |
| "Aviso: Veículo bloqueado, desbloqueio em X segundos" | $B_{OFF} \rightarrow ON$ <br> $P_{ON} \rightarrow OFF$ | Alta |
| "Bateria baixa: Integrar alimentação externa" | $Bt \le 2.5V$ | Média |

### 2.5 Comandos

* **Bloqueio:**
  * Usuário comanda o bloqueio da peça e este deve ser atendido imediatamente.
* **Atualização de dashboard:**
  * Usuário comanda um novo dashboard com dados mais recentes dos sensores.

---
***

## Materiais para hardware

* Switch (ignição)
* LED (Bloqueio, estado do painel, ignição)
* Relé
* Sensor de Tensão (Bateria)
* ESP32 -> Controle do sistema

## Observações Importantes do Sistema

* Organizar bem o gerenciamento de prioridades das tarefas, pois o sistema é crítico e deve atender a todas as interrupções de eventos.
* Saber como tarefas esporádicas e aperiódicas podem implementadas em um sistema de provavél execução cíclica.
* Lógica de interpretação de dados dos sensores deve ser bem definida, pois é a base para geração de eventos e mensagens.
