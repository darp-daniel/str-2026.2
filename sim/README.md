# Simulador HIL — carro elétrico no CoppeliaSim + verificação de prazos

Esta pasta faz o papel do **carro e do painel físico** (chaves, potenciômetro,
relé e LEDs do relatório). Ela também **mede se a ESP32 cumpre os prazos da
Tabela I**.

```
 CoppeliaSim (cena_carro.lua)          PC                                  ESP32
 ┌──────────────────────────┐   ┌────────────────────────────────┐   ┌──────────────────────┐
 │ chassi + rodas + física  │   │ sim_bus (C++)                  │   │ firmware (src/)      │
 │ painel simUI: ignição,   │◄─►│  • modelo de bateria e motor   │◄─►│  CONFIG_STR_MODO_SIM │
 │ violação, acelerador,    │   │  • cenários C1..C6             │   │  T1/T2 50 ms         │
 │ botões de comando        │   │  • verificador de prazos       │   │  T4 por fila         │
 │ LEDs no teto do carro    │   │  • CSV + gráficos              │   │  Alarm + Block reais │
 └──────────────────────────┘   └────────────────────────────────┘   └──────────────────────┘
        ZeroMQ via                        quadros com CRC                 UART0 (USB)
     ponte_coppelia.py                  (include/sim_protocol.h)        ou ESP virtual
```

Cada peça pode ser trocada por uma versão de teste. Por isso dá para avançar
sem a placa e sem o CoppeliaSim:

| Peça | Versão real | Versão de teste |
|---|---|---|
| Carro | CoppeliaSim (`--planta coppelia`) | dinâmica 1D em C++ (`--planta interna`, padrão) |
| ESP32 | placa pela USB (`--esp serial:COM5`) | ESP virtual com o `sensors/alarm.cpp` real (`--esp virtual`, padrão) |

## 1. Compilar e rodar os testes automáticos

Precisa de CMake e de um compilador C++17 (MinGW, MSVC ou GCC). Python e
matplotlib são opcionais (testes em tempo real e gráficos).

```bash
cmake -S sim -B sim/build -G Ninja
cmake --build sim/build
ctest --test-dir sim/build --output-on-failure
```

São 18 testes:

| Teste | O que prova |
|---|---|
| `protocolo` | CRC, quadros e ressincronização com texto de log no meio da UART |
| `modelos` | bateria (2,5 V em SOC 20 %, 1,5 V em 5 %), regeneração, motor, 0–100 km/h |
| `cenario` | leitura dos roteiros e erros com número da linha |
| `esp_virtual` | lógica real do alarme: alarme no próximo ciclo, bloqueio em 5 s e não antes |
| `verificador` | aprova no prazo; reprova atraso, antecipação, silêncio e bloqueio sem causa |
| `cenario_C1..C6` | cada cenário de ponta a ponta sai **APROVADO** |
| `falha_*_reprova` | com falha injetada na ESP virtual, o verificador **tem que** reprovar |
| `enlace_udp_dois_processos` | ponte e ESP virtual em processos separados, sockets reais, tempo real |
| `ponte_coppelia_simulador_falso` | cadeia até o adaptador do CoppeliaSim (cena falsa); torque = 0 sempre que bloqueado |
| `cenario_C7_percurso_completo` | corrida de 45 s com todas as situações, aprovada nas 8 regras |
| `teste_extenso_cena_falsa` | a ferramenta de conferência física (seção 3.1) roda e aprova com a cena falsa |

## 2. Rodar os cenários e ver os gráficos

```bash
python sim/ferramentas/rodar_cenarios.py
```

Em `sim/resultados/<data-hora>/` ficam, para cada cenário, o CSV passo a
passo, o CSV de eventos e um PNG com a linha do tempo (velocidade, torque,
tensão no pino com os limiares e estados digitais) e a latência de cada regra
em % do prazo.

Para ver uma reprovação de propósito:

```bash
sim/build/sim_bus --cenario sim/cenarios/C4_comando_usuario.txt --falha-atraso-comando 50
```

## 3. Teste visual no CoppeliaSim (ainda sem a placa)

1. Instale o CoppeliaSim 4.6 ou mais novo e o cliente Python:
   `pip install coppeliasim-zmqremoteapi-client`
2. Abra o CoppeliaSim com uma cena nova. O add-on *ZMQ remote API* já vem
   ativo e escuta na porta 23000.
3. Rode o adaptador:
   `python sim/coppelia/ponte_coppelia.py`
   Se a cena não tiver o dummy `/CarroSTR`, ele cria o carro, a pista e o
   painel. Se a criação automática falhar na sua versão, siga o passo a passo
   no topo de [coppelia/cena_carro.lua](coppelia/cena_carro.lua). Depois,
   salve a cena (ex.: `sim/coppelia/carro_str.ttt`).
4. Em outro terminal, rode a ponte:
   - roteiro automático: `sim/build/sim_bus --planta coppelia --cenario sim/cenarios/C2_violacao_painel.txt`
   - modo livre: `sim/build/sim_bus --planta coppelia` (Ctrl+C sai)

   No modo livre, você dirige pela janela **Carro STR**: ignição, violação do
   painel, acelerador, descarga rápida e os botões Bloquear, Desbloquear e
   Atualizar. O verificador continua medindo cada evento, e o último resultado
   aparece na própria janela.

O que observar no carro: as seis esferas no teto são, da frente para trás,
ignição, painel, alarme (pisca), bloqueio, bateria (verde/amarelo/vermelho) e
enlace com a ESP (azul = recebendo). No bloqueio, o torque vai a zero e o
carro perde velocidade por inércia.

Sem a ponte, a cena também anda sozinha pelo acelerador, sem ESP e sem
bloqueio. Serve para conferir a cena isoladamente.

### 3.1 Teste extenso: o que o carro fez de verdade

O relatório do `sim_bus` só fala dos prazos da ESP. Para conferir também a
física, com o CoppeliaSim aberto e **sem** outra ponte rodando:

```bash
python sim/ferramentas/teste_extenso.py            # use --recriar na primeira vez
```

Ele abre a ponte sozinho numa porta livre e roda o C7 (45 s, todas as
situações). Antes, roda o mesmo cenário com o carro interno, como
referência. Depois confere:

| | Conferência |
|---|---|
| A | regras de prazo da ESP e enlace sem erro de CRC |
| B | bloqueado ou sem ignição, o torque que **chegou ao CoppeliaSim** é zero |
| C | tabela fase a fase: com torque acelera, sem torque não ganha velocidade, regeneração freia |
| D | velocidade do CoppeliaSim × modelo interno (RMS e maior diferença) |
| E | chassi em pé, dentro da pista, sem marcha a ré |
| F | posição coerente com a velocidade integrada; patinagem das rodas motrizes |
| G | passos atrasados, deriva entre o relógio da física e o real, e quem é o gargalo |

Em `sim/resultados/<data>_extenso/` ficam o `relatorio.txt`, o
`teste_extenso.png`, os CSVs do `sim_bus` e o `ponte_coppelia.csv`. Este
último registra o lado do CoppeliaSim passo a passo: torque recebido,
velocidade, posição e tempo de cada passo. Com `--esp serial:COM5`, faz o
mesmo com a placa.

## 4. Tudo junto: CoppeliaSim + ESP32 física

1. No firmware, ative o modo HIL:
   `idf.py menuconfig` → **STR Telemetria** → *Modo HIL* → salvar.
2. `idf.py build flash`. **Não** deixe o `idf.py monitor` aberto: a porta COM
   só aceita um programa por vez.
3. Rode o adaptador do CoppeliaSim (seção 3) e então:
   ```bash
   sim/build/sim_bus --planta coppelia --esp serial:COM5
   ```
   Os logs da ESP (`ESP_LOGx`) aparecem na tela como `[ESP] ...`. Os quadros
   com CRC são separados deles automaticamente.
4. Bateria completa de cenários, com gráficos:
   ```bash
   python sim/ferramentas/rodar_cenarios.py --planta coppelia --esp serial:COM5
   ```
   Para testar só a placa, sem o CoppeliaSim, tire o `--planta coppelia`.

No modo HIL a ESP lê ignição, painel e tensão da bateria do simulador. Os
comandos do usuário viram a tarefa T4, disparada por fila. A ESP devolve
bloqueio, alarme, motivo, nível da bateria e eco do comando. O relé do GPIO26
continua sendo acionado de verdade.

## 5. Regras verificadas (prazos da Tabela I)

| Estímulo (aplicado pelo simulador) | Resposta esperada da ESP | Prazo |
|---|---|---|
| painel violado | `alarme = 1` | ≤ 100 ms (T3) |
| painel violado | `bloqueio = 1`, motivo = violação | entre 5000 e 5300 ms |
| V_pino ≤ 2,5 V | `nivel_bat ≥ BAIXA` | ≤ 100 ms (T3) |
| V_pino ≤ 1,5 V | `nivel_bat = CRITICA` | ≤ 100 ms (T3) |
| comando bloquear/desbloquear/atualizar | eco do comando, com o estado certo | ≤ 30 ms (T4) |
| ignição muda | `ignicao_eco` atualizado | ≤ 200 ms (T5) |
| — | bloqueio sem comando nem alarme | sempre falha |

As latências são medidas **no PC, com um único relógio**, do estímulo até a
resposta chegar. Incluem o enlace nos dois sentidos e até um passo da ponte
(10 ms). Por isso são um limite superior conservador do tempo de resposta
dentro da ESP.

## 6. Escrevendo um cenário

```
duracao 20                 # segundos
0.0  ignicao 1             # 0/1
1.0  pedal 0.3             # -1..1 (negativo = regeneração)
10.0 painel 0              # 0 = violado
0.0  soc 0.25              # estado de carga
0.0  descarga 40           # fator de descarga acelerada
0.0  dreno 30              # consumo parasita (A)
5.0  comando bloquear      # bloquear | desbloquear | atualizar
```

## 7. Limitações conhecidas

- **Não testado aqui:** a cena do CoppeliaSim (`cena_carro.lua`), o adaptador
  com o CoppeliaSim de verdade e o firmware em modo HIL na placa. O
  CoppeliaSim e o ESP-IDF não estavam instalados na máquina onde isto foi
  feito. O firmware passou por checagem de sintaxe com cabeçalhos-esqueleto
  do ESP-IDF, e o adaptador foi testado com uma cena falsa. A API Lua do
  CoppeliaSim mudou entre 4.6, 4.7 e 4.8; os pontos sensíveis têm
  alternativas no código.
- O PC e o CoppeliaSim não são de tempo real. O HIL verifica as garantias
  **da ESP32**, não do sistema inteiro.
- Em aceleração forte, a queda de tensão R·I sozinha leva o pino abaixo dos
  limiares (veja o C5). Isso é realista, e o firmware precisa decidir se
  filtra ou confirma a leitura.
- No modo HIL, o nível da bateria é calculado a partir da tensão recebida do
  simulador. Na placa sem simulador ainda falta a leitura do ADC calibrado
  (`adc_cali`) com o divisor resistivo.
