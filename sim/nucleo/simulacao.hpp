// simulacao.hpp — laço da ponte (SIM-BUS): planta <-> enlace <-> verificador.
//
// A cada passo dt:
//   1. lê as respostas da ESP que chegaram (verificador mede latências)
//   2. aplica as ações do cenário que venceram
//   3. lê a planta e envia o quadro SimParaEsp (com o comando da vez)
//   4. aplica o bloqueio vindo da ESP e avança a física
//   5. grava uma linha no CSV
#pragma once

#include "cenario.hpp"
#include "enlace.hpp"
#include "planta.hpp"
#include "verificador.hpp"

#include <atomic>
#include <string>
#include <vector>

namespace str_sim {

struct OpcoesLaco {
  double dt_s = 0.01;
  bool tempo_real = false;   // obrigatório com placa física ou CoppeliaSim
  double duracao_max_s = 0;  // usada quando não há cenário (0 = até Ctrl+C)
  std::string csv;           // série temporal (vazio = não grava)
  bool verbose = true;
  std::atomic<bool> *parar = nullptr;
};

struct ResumoExecucao {
  int regras = 0;
  int aprovadas = 0;
  int falhas = 0;
  uint64_t passos = 0;
  uint64_t passos_atrasados = 0; // só em tempo real
  std::vector<double> rtt_ms;    // ida e volta por seq ecoado
  EstatisticaEnlace enlace;
  bool ok() const { return falhas == 0; }
};

ResumoExecucao executar(Planta &planta, Enlace &enlace, const Cenario *cenario, Verificador &verif,
                        const OpcoesLaco &op);

void imprimirResumo(const ResumoExecucao &r, std::ostream &o);

} // namespace str_sim
