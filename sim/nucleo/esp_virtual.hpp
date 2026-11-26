// esp_virtual.hpp — imita o firmware da ESP32 no PC (Software-in-the-Loop).
//
// Usa o código REAL do projeto (sensors/alarm.cpp + Block) para a lógica de
// alarme e bloqueio. Só o acionamento de hardware do Block é trocado
// (sim/nucleo/block_host.cpp). Serve para:
//   - testar a ponte e o CoppeliaSim sem a placa;
//   - testar o verificador (com falhas injetadas de propósito).
#pragma once

#include "alarm.hpp"
#include "block.hpp"
#include "sim_protocol.h"

#include <chrono>
#include <cstdint>
#include <deque>

namespace str_sim {

struct ParametrosEspVirtual {
  uint32_t periodo_us = 50000;       // período de T1/T2 (Tabela I: 50 ms)
  uint32_t atraso_resposta_us = 1000; // tempo até a resposta "sair" da ESP
  // Injeção de falhas (para provar que o verificador pega erros):
  uint32_t atraso_comando_us = 0; // atraso extra no tratamento de T4
  bool ignora_painel = false;     // nunca detecta a violação do painel
  bool implementa_bateria = true; // calcula nivel_bat (T2)
};

class EspVirtual {
public:
  explicit EspVirtual(ParametrosEspVirtual p = {});

  // Chegou um quadro do simulador no instante t_us.
  void receber(const SimParaEsp &m, uint64_t t_us);
  // Executa o que estiver vencido até t_us (T1/T2 periódicas, T4 atrasada).
  void servico(uint64_t t_us);
  // Retira uma resposta pronta até t_us. false se não houver.
  bool proximaResposta(uint64_t t_us, EspParaSim &out);

  const Block &bloqueio() const { return block_; }
  bool alarmeAtivo() { return alarme_.isActive(); }
  uint8_t nivelBateria() const { return nivel_bat_; }

private:
  void cicloT1T2(uint64_t t_us);
  void executarComando(uint8_t cmd, uint8_t id, uint64_t t_us);
  void enfileirarResposta(uint64_t t_us);

  ParametrosEspVirtual p_;
  Block block_;
  Alarm alarme_;

  bool tem_dados_ = false;
  SimParaEsp ultimo_{};
  bool primeiro_ = true;
  bool painel_anterior_ = true;
  uint8_t nivel_bat_ = SIMP_BAT_NORMAL;
  uint8_t ultimo_cmd_id_ = 0;
  uint8_t cmd_eco_ = 0;
  uint64_t proximo_ciclo_us_ = 0;

  // O Alarm real marca o instante com steady_clock::now(). Para rodar mais
  // rápido que o tempo real (testes), convertemos o tempo simulado em um
  // time_point equivalente, ancorado no instante real da ativação.
  std::chrono::steady_clock::time_point ancora_real_{};
  uint64_t ancora_sim_us_ = 0;

  struct Pendente { uint64_t pronto_us; uint8_t cmd; uint8_t id; };
  std::deque<Pendente> comandos_;
  struct Resposta { uint64_t pronto_us; EspParaSim msg; };
  std::deque<Resposta> respostas_;
};

} // namespace str_sim
