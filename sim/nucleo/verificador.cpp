#include "verificador.hpp"

#include "block.hpp"
#include "cenario.hpp"
#include "planta.hpp"

#include <algorithm>
#include <cstdio>
#include <fstream>

namespace str_sim {

const char *nomeSituacao(Situacao s) {
  switch (s) {
  case Situacao::Pendente: return "PENDENTE";
  case Situacao::Ok: return "OK";
  case Situacao::Falhou: return "FALHOU";
  case Situacao::NaoAplicavel: return "N/A";
  case Situacao::Inconclusivo: return "INCONCLUSIVO";
  }
  return "?";
}

static double ms(uint64_t a, uint64_t b) { return (static_cast<double>(a) - static_cast<double>(b)) / 1000.0; }

size_t Verificador::novaRegra(const std::string &regra, const std::string &tarefa, uint64_t t0,
                              double prazo, double minimo) {
  ResultadoRegra r;
  r.regra = regra;
  r.tarefa = tarefa;
  r.t_evento_s = t0 / 1e6;
  r.prazo_ms = prazo;
  r.minimo_ms = minimo;
  res_.push_back(r);
  return res_.size() - 1;
}

void Verificador::concluir(size_t idx, Situacao s, double lat_ms, const std::string &det) {
  auto &r = res_[idx];
  r.situacao = s;
  r.latencia_ms = lat_ms;
  r.detalhe = det;
  char buf[200];
  if (lat_ms >= 0)
    std::snprintf(buf, sizeof buf, "%s %s: %.1f ms (prazo %.0f ms)", nomeSituacao(s), r.regra.c_str(), lat_ms,
                  r.prazo_ms);
  else
    std::snprintf(buf, sizeof buf, "%s %s: %s", nomeSituacao(s), r.regra.c_str(), det.c_str());
  ultimo_evento_ = buf;
  if (aoConcluir) aoConcluir(r);
}

void Verificador::observarPlanta(const EstadoPlanta &e, uint64_t t) {
  if (primeira_obs_) {
    primeira_obs_ = false;
    ign_ant_ = e.ignicao;
    painel_ant_ = e.painel_ok;
    armado_baixa_ = e.v_pino > SIMP_LIMIAR_BAT_BAIXA;
    armado_crit_ = e.v_pino > SIMP_LIMIAR_BAT_CRITICA;
    // Estado inicial também precisa chegar ao dashboard.
    size_t i = novaRegra(std::string("ignicao inicial = ") + (e.ignicao ? "ON" : "OFF") + " no dashboard", "T5", t,
                         p_.dashboard_ms, 0);
    pend_.push_back({i, Tipo::Ignicao, t, 0, p_.dashboard_ms, 0, 0, e.ignicao ? 1 : 0});
    return;
  }

  if (e.ignicao != ign_ant_) {
    size_t i = novaRegra(std::string("ignicao ") + (e.ignicao ? "ON" : "OFF") + " -> dashboard", "T5", t,
                         p_.dashboard_ms, 0);
    // uma mudança nova substitui a anterior ainda pendente
    for (auto it = pend_.begin(); it != pend_.end();) {
      if (it->tipo == Tipo::Ignicao) {
        concluir(it->idx, Situacao::NaoAplicavel, -1, "substituida por nova mudanca de ignicao");
        it = pend_.erase(it);
      } else ++it;
    }
    pend_.push_back({i, Tipo::Ignicao, t, 0, p_.dashboard_ms, 0, 0, e.ignicao ? 1 : 0});
    ign_ant_ = e.ignicao;
  }

  if (painel_ant_ && !e.painel_ok) {
    size_t i = novaRegra("violacao do painel -> alarme", "T3", t, p_.alarme_ms, 0);
    pend_.push_back({i, Tipo::Alarme, t, 0, p_.alarme_ms});
    const double mx = p_.bloqueio_alarme_ms + p_.tolerancia_bloqueio_ms;
    size_t j = novaRegra("alarme 5 s -> bloqueio", "T2", t, mx, p_.bloqueio_alarme_ms);
    if (tem_rx_ && bloqueio_ant_) {
      concluir(j, Situacao::NaoAplicavel, -1, "veiculo ja estava bloqueado");
    } else {
      pend_.push_back({j, Tipo::BloqueioAlarme, t, p_.bloqueio_alarme_ms, mx});
    }
  }
  painel_ant_ = e.painel_ok;

  // Limiares de bateria com histerese de 0,1 V para não disparar em cascata.
  if (armado_baixa_ && e.v_pino <= SIMP_LIMIAR_BAT_BAIXA) {
    armado_baixa_ = false;
    size_t i = novaRegra("V_pino <= 2,5 V -> bateria baixa", "T3", t, p_.bateria_ms, 0);
    pend_.push_back({i, Tipo::BatBaixa, t, 0, p_.bateria_ms});
  } else if (e.v_pino > SIMP_LIMIAR_BAT_BAIXA + 0.1) {
    armado_baixa_ = true;
  }
  if (armado_crit_ && e.v_pino <= SIMP_LIMIAR_BAT_CRITICA) {
    armado_crit_ = false;
    size_t i = novaRegra("V_pino <= 1,5 V -> bateria critica", "T3", t, p_.bateria_ms, 0);
    pend_.push_back({i, Tipo::BatCritica, t, 0, p_.bateria_ms});
  } else if (e.v_pino > SIMP_LIMIAR_BAT_CRITICA + 0.1) {
    armado_crit_ = true;
  }
}

void Verificador::comandoEmitido(int cmd, uint8_t id, uint64_t t0) {
  size_t i = novaRegra(std::string("comando '") + nomeComando(cmd) + "' (id " + std::to_string(id) + ")", "T4", t0,
                       p_.comando_ms, 0);
  pend_.push_back({i, Tipo::Comando, t0, 0, p_.comando_ms, cmd, id});
}

static bool ecoAlcancou(uint8_t eco, uint8_t id) {
  return eco != 0 && static_cast<int8_t>(static_cast<uint8_t>(eco - id)) >= 0;
}

bool Verificador::satisfeita(const Espera &e, const EspParaSim &r) const {
  switch (e.tipo) {
  case Tipo::Alarme: return r.alarme == 1;
  case Tipo::BloqueioAlarme:
    return r.bloqueio == 1 && r.motivo == static_cast<uint8_t>(BlockReason::ALARM_OVERRIDE);
  case Tipo::BatBaixa: return r.nivel_bat >= SIMP_BAT_BAIXA;
  case Tipo::BatCritica: return r.nivel_bat == SIMP_BAT_CRITICA;
  case Tipo::Comando: return ecoAlcancou(r.comando_id_eco, e.id);
  case Tipo::Ignicao: return r.ignicao_eco == e.valor;
  }
  return false;
}

void Verificador::observarResposta(const EspParaSim &r, uint64_t t) {
  bool bloqueio_explicado = false;
  bool desbloqueou = false;

  for (auto it = pend_.begin(); it != pend_.end();) {
    const Espera &e = *it;
    // Bloqueio por outro motivo durante a janela do alarme -> regra não se aplica.
    if (e.tipo == Tipo::BloqueioAlarme && r.bloqueio == 1 &&
        r.motivo != static_cast<uint8_t>(BlockReason::ALARM_OVERRIDE)) {
      concluir(e.idx, Situacao::NaoAplicavel, -1, "bloqueado antes por outro motivo");
      it = pend_.erase(it);
      continue;
    }
    if (!satisfeita(e, r)) {
      ++it;
      continue;
    }
    const double lat = ms(t, e.t0_us);
    Situacao s = Situacao::Ok;
    std::string det;
    if (lat < e.min_ms) {
      s = Situacao::Falhou;
      det = "antecipado (minimo " + std::to_string(static_cast<int>(e.min_ms)) + " ms)";
    } else if (lat > e.max_ms) {
      s = Situacao::Falhou;
      det = "fora do prazo";
    }
    if (e.tipo == Tipo::Comando && r.comando_id_eco == e.id) {
      if (e.cmd == SIMP_CMD_BLOQUEAR && r.bloqueio != 1) { s = Situacao::Falhou; det = "eco ok, mas bloqueio = 0"; }
      if (e.cmd == SIMP_CMD_DESBLOQUEAR && r.bloqueio != 0) { s = Situacao::Falhou; det = "eco ok, mas bloqueio = 1"; }
      if (e.cmd == SIMP_CMD_DESBLOQUEAR) desbloqueou = true;
    }
    if (e.tipo == Tipo::BloqueioAlarme || (e.tipo == Tipo::Comando && e.cmd == SIMP_CMD_BLOQUEAR))
      bloqueio_explicado = true;
    concluir(e.idx, s, lat, det);
    it = pend_.erase(it);
  }

  if (desbloqueou) { // desbloqueio também desliga o alarme: espera de bloqueio perde o sentido
    for (auto it = pend_.begin(); it != pend_.end();) {
      if (it->tipo == Tipo::BloqueioAlarme) {
        concluir(it->idx, Situacao::NaoAplicavel, -1, "desbloqueado pelo usuario durante o alarme");
        it = pend_.erase(it);
      } else ++it;
    }
  }

  if (tem_rx_ && bloqueio_ant_ == 0 && r.bloqueio == 1 && !bloqueio_explicado) {
    size_t i = novaRegra("bloqueio sem causa (nem comando nem alarme)", "--", t, 0, 0);
    concluir(i, Situacao::Falhou, -1, "motivo=" + std::to_string(r.motivo));
  }
  bloqueio_ant_ = r.bloqueio;
  tem_rx_ = true;
}

void Verificador::tick(uint64_t t) {
  for (auto it = pend_.begin(); it != pend_.end();) {
    if (ms(t, it->t0_us) > it->max_ms + p_.espera_extra_ms) {
      concluir(it->idx, Situacao::Falhou, -1,
               "sem resposta em " + std::to_string(static_cast<int>(it->max_ms + p_.espera_extra_ms)) + " ms");
      it = pend_.erase(it);
    } else ++it;
  }
}

void Verificador::finalizar(uint64_t t) {
  for (auto &e : pend_) {
    if (ms(t, e.t0_us) > e.max_ms)
      concluir(e.idx, Situacao::Falhou, -1, "sem resposta ate o fim do cenario");
    else
      concluir(e.idx, Situacao::Inconclusivo, -1, "cenario terminou antes do prazo");
  }
  pend_.clear();
}

int Verificador::falhas() const {
  return static_cast<int>(std::count_if(res_.begin(), res_.end(),
                                        [](const ResultadoRegra &r) { return r.situacao == Situacao::Falhou; }));
}

int Verificador::aprovadas() const {
  return static_cast<int>(
      std::count_if(res_.begin(), res_.end(), [](const ResultadoRegra &r) { return r.situacao == Situacao::Ok; }));
}

void Verificador::imprimirRelatorio(std::ostream &o) const {
  char buf[300];
  o << "\n  t (s)   tarefa  regra                                          latencia      prazo     resultado\n";
  o << "  ------  ------  ---------------------------------------------  ------------  --------  ------------\n";
  for (const auto &r : res_) {
    char lat[32], prazo[32];
    if (r.latencia_ms >= 0) std::snprintf(lat, sizeof lat, "%9.1f ms", r.latencia_ms);
    else std::snprintf(lat, sizeof lat, "%12s", "-");
    if (r.minimo_ms > 0) std::snprintf(prazo, sizeof prazo, "%.0f-%.0f", r.minimo_ms, r.prazo_ms);
    else if (r.prazo_ms > 0) std::snprintf(prazo, sizeof prazo, "<=%.0f", r.prazo_ms);
    else std::snprintf(prazo, sizeof prazo, "-");
    std::snprintf(buf, sizeof buf, "  %6.2f  %-6s  %-45.45s  %s  %-8s  %s%s%s\n", r.t_evento_s, r.tarefa.c_str(),
                  r.regra.c_str(), lat, prazo, nomeSituacao(r.situacao), r.detalhe.empty() ? "" : " - ",
                  r.detalhe.c_str());
    o << buf;
  }
  std::snprintf(buf, sizeof buf, "\n  %d regra(s): %d OK, %d falha(s)\n", static_cast<int>(res_.size()), aprovadas(),
                falhas());
  o << buf;
}

void Verificador::salvarCsv(const std::string &caminho) const {
  std::ofstream f(caminho);
  f << "t_evento_s,tarefa,regra,latencia_ms,minimo_ms,prazo_ms,resultado,detalhe\n";
  for (const auto &r : res_)
    f << r.t_evento_s << ',' << r.tarefa << ",\"" << r.regra << "\"," << r.latencia_ms << ',' << r.minimo_ms << ','
      << r.prazo_ms << ',' << nomeSituacao(r.situacao) << ",\"" << r.detalhe << "\"\n";
}

} // namespace str_sim
