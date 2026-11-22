// cenario.hpp — roteiro de teste com instantes fixos.
//
// Formato do arquivo (uma ação por linha, '#' inicia comentário):
//
//   duracao 20                 # tempo total (s)
//   0.0  ignicao 1             # 0/1
//   1.0  pedal 0.3             # -1..1 (negativo = regeneração)
//   10.0 painel 0              # 1 = conforme, 0 = violado
//   0.0  soc 0.25              # estado de carga inicial
//   0.0  descarga 40           # fator de descarga acelerada
//   0.0  dreno 30              # consumo parasita (A)
//   5.0  comando bloquear      # bloquear | desbloquear | atualizar
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace str_sim {

enum class TipoAcao { Ignicao, Painel, Pedal, Soc, Descarga, Dreno, Comando };

struct AcaoCenario {
  double t_s = 0;
  TipoAcao tipo = TipoAcao::Pedal;
  double valor = 0; // para Comando: SIMP_CMD_*
  int linha = 0;
};

struct Cenario {
  std::string nome;
  double duracao_s = 0; // 0 = sem fim (modo interativo)
  std::vector<AcaoCenario> acoes; // ordenadas por t_s (estável)
};

// Lança std::runtime_error com o número da linha em caso de erro.
Cenario lerCenario(const std::string &texto, const std::string &nome = "cenario");
Cenario carregarCenario(const std::string &caminho);

const char *nomeAcao(TipoAcao t);
const char *nomeComando(int cmd);

// Entrega as ações em ordem conforme o tempo avança.
class ExecutorCenario {
public:
  explicit ExecutorCenario(const Cenario &c) : c_(c) {}
  // Retorna as ações com t_s <= t (que ainda não foram entregues).
  std::vector<AcaoCenario> acoesAte(double t_s);
  bool terminou(double t_s) const { return c_.duracao_s > 0 && t_s >= c_.duracao_s; }

private:
  const Cenario &c_;
  size_t prox_ = 0;
};

} // namespace str_sim
