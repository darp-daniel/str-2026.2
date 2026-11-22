// verificador.hpp — confere os requisitos temporais (Tabela I do relatório)
// observando, do lado do PC, os estímulos aplicados e as respostas da ESP.
//
// Regras:
//   T3  violação do painel     -> alarme = 1                     <= 100 ms
//   T2  alarme ativo           -> bloqueio (motivo alarme)       5 s (+ tolerância)
//   T3  V_pino <= 2,5 V        -> nivel_bat >= BAIXA             <= 100 ms
//   T3  V_pino <= 1,5 V        -> nivel_bat == CRITICA           <= 100 ms
//   T4  comando do usuário     -> eco do comando + estado certo  <= 30 ms
//   T5  ignição muda           -> ignicao_eco atualizado         <= 200 ms
//   --  bloqueio sem causa (nenhum comando nem alarme pendente)   falha
//
// Todas as latências são medidas com um único relógio (o do PC), do instante
// em que o estímulo é aplicado até a resposta chegar. Incluem, portanto, o
// enlace nos dois sentidos e até um passo de quantização da ponte.
#pragma once

#include "sim_protocol.h"

#include <cstdint>
#include <functional>
#include <ostream>
#include <string>
#include <vector>

namespace str_sim {

struct EstadoPlanta; // planta.hpp

enum class Situacao { Pendente, Ok, Falhou, NaoAplicavel, Inconclusivo };
const char *nomeSituacao(Situacao s);

struct ResultadoRegra {
  std::string regra;
  std::string tarefa;
  double t_evento_s = 0;
  double prazo_ms = 0;   // limite superior
  double minimo_ms = 0;  // limite inferior (só no bloqueio por alarme)
  double latencia_ms = -1;
  Situacao situacao = Situacao::Pendente;
  std::string detalhe;
};

struct PrazosVerificacao {
  double alarme_ms = 100;          // T3
  double bateria_ms = 100;         // T3
  double comando_ms = 30;          // T4
  double dashboard_ms = 200;       // T5
  double bloqueio_alarme_ms = 5000;
  double tolerancia_bloqueio_ms = 300;
  double espera_extra_ms = 2000;   // continua esperando depois do prazo para medir atrasos
};

class Verificador {
public:
  explicit Verificador(PrazosVerificacao p = {}) : p_(p) {}

  void observarPlanta(const EstadoPlanta &e, uint64_t t_us);
  void comandoEmitido(int cmd, uint8_t id, uint64_t t_emitido_us);
  void observarResposta(const EspParaSim &r, uint64_t t_us);
  void tick(uint64_t t_us);     // marca prazos vencidos
  void finalizar(uint64_t t_us);

  const std::vector<ResultadoRegra> &resultados() const { return res_; }
  int falhas() const;
  int aprovadas() const;
  bool tudoOk() const { return falhas() == 0; }
  const std::string &ultimoEvento() const { return ultimo_evento_; }

  void imprimirRelatorio(std::ostream &o) const;
  void salvarCsv(const std::string &caminho) const;

  // chamado a cada regra concluída (para log ao vivo)
  std::function<void(const ResultadoRegra &)> aoConcluir;

private:
  enum class Tipo { Alarme, BloqueioAlarme, BatBaixa, BatCritica, Comando, Ignicao };
  struct Espera {
    size_t idx;
    Tipo tipo;
    uint64_t t0_us;
    double min_ms, max_ms;
    int cmd = 0;
    uint8_t id = 0;
    int valor = 0;
  };

  size_t novaRegra(const std::string &regra, const std::string &tarefa, uint64_t t0, double prazo, double minimo);
  void concluir(size_t idx, Situacao s, double lat_ms, const std::string &det);
  bool satisfeita(const Espera &e, const EspParaSim &r) const;

  PrazosVerificacao p_;
  std::vector<ResultadoRegra> res_;
  std::vector<Espera> pend_;

  bool primeira_obs_ = true;
  bool ign_ant_ = false, painel_ant_ = true;
  bool armado_baixa_ = true, armado_crit_ = true;
  bool tem_rx_ = false;
  uint8_t bloqueio_ant_ = 0;
  std::string ultimo_evento_;
};

} // namespace str_sim
