#include "esp_virtual.hpp"

namespace str_sim {

EspVirtual::EspVirtual(ParametrosEspVirtual p) : p_(p), block_(26, 27) {}

void EspVirtual::receber(const SimParaEsp &m, uint64_t t_us) {
  ultimo_ = m;
  if (!tem_dados_) {
    tem_dados_ = true;
    proximo_ciclo_us_ = t_us; // primeira liberação de T1 assim que há dados
  }
  // T4 é disparada pela chegada do comando (esporádica), não pelo período.
  if (m.comando != SIMP_CMD_NENHUM && m.comando_id != ultimo_cmd_id_) {
    ultimo_cmd_id_ = m.comando_id;
    if (p_.atraso_comando_us == 0) executarComando(m.comando, m.comando_id, t_us);
    else comandos_.push_back({t_us + p_.atraso_comando_us, m.comando, m.comando_id});
  }
}

void EspVirtual::servico(uint64_t t_us) {
  while (!comandos_.empty() && comandos_.front().pronto_us <= t_us) {
    auto c = comandos_.front();
    comandos_.pop_front();
    executarComando(c.cmd, c.id, c.pronto_us);
  }
  while (tem_dados_ && proximo_ciclo_us_ <= t_us) {
    cicloT1T2(proximo_ciclo_us_);
    proximo_ciclo_us_ += p_.periodo_us;
  }
}

void EspVirtual::cicloT1T2(uint64_t t_us) {
  // ---- T1: coleta (aqui: último quadro recebido)
  const bool painel = ultimo_.painel_ok != 0;
  if (primeiro_) {
    painel_anterior_ = painel;
    primeiro_ = false;
  }

  // ---- T2: interpretação e eventos (mesma lógica de src/main.cpp)
  if (painel_anterior_ && !painel && !p_.ignora_painel) {
    alarme_.ativar();
    ancora_real_ = std::chrono::steady_clock::now();
    ancora_sim_us_ = t_us;
  }
  painel_anterior_ = painel;

  if (alarme_.isActive()) {
    auto agora = ancora_real_ + std::chrono::microseconds(t_us - ancora_sim_us_);
    alarme_.chamarBlock(agora, block_);
  }

  if (p_.implementa_bateria) {
    const float v = ultimo_.v_bat_pino;
    const float h = 0.05f; // histerese para não oscilar perto do limiar
    uint8_t n;
    if (v <= SIMP_LIMIAR_BAT_CRITICA) n = SIMP_BAT_CRITICA;
    else if (v <= SIMP_LIMIAR_BAT_BAIXA) n = SIMP_BAT_BAIXA;
    else n = SIMP_BAT_NORMAL;
    // so melhora de nivel depois de subir h acima do limiar
    if (n < nivel_bat_) {
      const float limiar = (nivel_bat_ == SIMP_BAT_CRITICA) ? SIMP_LIMIAR_BAT_CRITICA : SIMP_LIMIAR_BAT_BAIXA;
      if (v <= limiar + h) n = nivel_bat_;
    }
    nivel_bat_ = n;
  }

  enfileirarResposta(t_us); // T5 simplificada: estado a cada ciclo
}

void EspVirtual::executarComando(uint8_t cmd, uint8_t id, uint64_t t_us) {
  switch (cmd) {
  case SIMP_CMD_BLOQUEAR: block_.lockByUser(); break;
  case SIMP_CMD_DESBLOQUEAR:
    block_.unlock();
    alarme_.desativar();
    break;
  default: break; // ATUALIZAR: só força o envio do estado (T4 -> T5)
  }
  cmd_eco_ = id;
  enfileirarResposta(t_us);
}

void EspVirtual::enfileirarResposta(uint64_t t_us) {
  EspParaSim r{};
  r.seq_eco = ultimo_.seq;
  r.t_esp_ms = static_cast<uint32_t>(t_us / 1000);
  r.bloqueio = block_.isBlocked() ? 1 : 0;
  r.alarme = alarme_.isActive() ? 1 : 0;
  r.motivo = static_cast<uint8_t>(block_.getReason());
  r.nivel_bat = nivel_bat_;
  r.ignicao_eco = ultimo_.ignicao;
  r.comando_id_eco = cmd_eco_;
  respostas_.push_back({t_us + p_.atraso_resposta_us, r});
}

bool EspVirtual::proximaResposta(uint64_t t_us, EspParaSim &out) {
  if (respostas_.empty() || respostas_.front().pronto_us > t_us) return false;
  out = respostas_.front().msg;
  respostas_.pop_front();
  return true;
}

} // namespace str_sim
