// sim_link.hpp — enlace da ESP32 com o simulador (modo HIL, CONFIG_STR_MODO_SIM).
//
// Usa a UART0 (a mesma da USB/console). Os quadros de include/sim_protocol.h
// convivem com os logs ESP_LOGx: a ponte no PC separa um do outro.
//
//   tarefa sim_rx (prioridade alta) : decodifica quadros, guarda o último e
//                                     enfileira comandos novos para T4
//   sim_link::enviar()              : manda o estado da ESP para o simulador
#pragma once

#include "sim_protocol.h"

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "sdkconfig.h"
#include <stdint.h>

namespace sim_link {

struct Comando {
  uint8_t cmd; // SIMP_CMD_*
  uint8_t id;

#if !CONFIG_STR_MODO_SIM
  int64_t t_rx_us;
  int64_t t_fila_us;
#endif
};

// Instala o driver da UART0 e cria a tarefa de recepção.
void iniciar(UBaseType_t prioridade_rx);

// Cópia do último quadro recebido. false enquanto nada chegou.
bool ultimoQuadro(SimParaEsp &out);

// Fila de comandos do usuário (T4 bloqueia nela). Cada comando_id chega uma vez.
QueueHandle_t filaComandos();

// Envia o estado para o simulador. Pode ser chamada de várias tarefas.
void enviar(const EspParaSim &m);

} // namespace sim_link
