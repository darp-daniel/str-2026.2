// ESP virtual: confere a lógica REAL do alarme (sensors/alarm.cpp) e do Block.
#include "block.hpp"
#include "esp_virtual.hpp"
#include "mini_teste.hpp"

using namespace str_sim;

static SimParaEsp q(uint32_t seq, bool painel, float v = 3.3f, uint8_t cmd = 0, uint8_t id = 0) {
  SimParaEsp m{};
  m.seq = seq;
  m.ignicao = 1;
  m.painel_ok = painel ? 1 : 0;
  m.v_bat_pino = v;
  m.comando = cmd;
  m.comando_id = id;
  return m;
}

// Avança de 10 em 10 ms, mandando o mesmo quadro.
static void rodar(EspVirtual &e, uint64_t &t, uint64_t ate_us, const SimParaEsp &m) {
  for (; t <= ate_us; t += 10000) {
    e.receber(m, t);
    e.servico(t);
  }
}

TESTE(violacao_ativa_alarme_no_proximo_ciclo) {
  EspVirtual e;
  uint64_t t = 0;
  rodar(e, t, 1000000, q(0, true));
  CHECA(!e.alarmeAtivo());
  rodar(e, t, 1060000, q(1, false));
  CHECA(e.alarmeAtivo());
  CHECA(!e.bloqueio().isBlocked());
}

TESTE(bloqueia_em_5_s_e_nao_antes) {
  EspVirtual e;
  uint64_t t = 0;
  rodar(e, t, 1000000, q(0, true));
  rodar(e, t, 1050000, q(1, false)); // alarme ativado entre 1,01 e 1,05 s
  rodar(e, t, 5990000, q(1, false));
  CHECA(!e.bloqueio().isBlocked()); // ainda dentro dos 5 s
  rodar(e, t, 6200000, q(1, false));
  CHECA(e.bloqueio().isBlocked());
  CHECA(e.bloqueio().getReason() == BlockReason::ALARM_OVERRIDE);
}

TESTE(comando_bloquear_e_imediato_e_tratado_uma_vez) {
  EspVirtual e;
  e.receber(q(0, true, 3.3f, SIMP_CMD_BLOQUEAR, 1), 0);
  CHECA(e.bloqueio().isBlocked());
  CHECA(e.bloqueio().getReason() == BlockReason::USER_COMMAND);
  e.receber(q(1, true, 3.3f, SIMP_CMD_DESBLOQUEAR, 2), 1000);
  CHECA(!e.bloqueio().isBlocked());
  // mesmo id repetido (retransmissão) não executa de novo
  e.receber(q(2, true, 3.3f, SIMP_CMD_DESBLOQUEAR, 2), 2000);
  e.receber(q(3, true, 3.3f, SIMP_CMD_BLOQUEAR, 2), 3000);
  CHECA(!e.bloqueio().isBlocked());
}

TESTE(resposta_ecoa_comando_e_sai_com_atraso) {
  ParametrosEspVirtual p;
  p.atraso_resposta_us = 2000;
  EspVirtual e(p);
  e.receber(q(5, true, 3.3f, SIMP_CMD_BLOQUEAR, 9), 100000);
  EspParaSim r;
  CHECA(!e.proximaResposta(101000, r));
  CHECA(e.proximaResposta(102000, r));
  CHECA(r.comando_id_eco == 9 && r.bloqueio == 1 && r.seq_eco == 5);
}

TESTE(niveis_de_bateria) {
  EspVirtual e;
  uint64_t t = 0;
  rodar(e, t, 100000, q(0, true, 3.0f));
  CHECA(e.nivelBateria() == SIMP_BAT_NORMAL);
  rodar(e, t, 200000, q(1, true, 2.45f));
  CHECA(e.nivelBateria() == SIMP_BAT_BAIXA);
  rodar(e, t, 300000, q(2, true, 1.4f));
  CHECA(e.nivelBateria() == SIMP_BAT_CRITICA);
  rodar(e, t, 400000, q(3, true, 1.52f)); // dentro da histerese
  CHECA(e.nivelBateria() == SIMP_BAT_CRITICA);
  rodar(e, t, 500000, q(4, true, 3.0f));
  CHECA(e.nivelBateria() == SIMP_BAT_NORMAL);
}

TESTE(desbloquear_tambem_desliga_alarme) {
  EspVirtual e;
  uint64_t t = 0;
  rodar(e, t, 100000, q(0, true));
  rodar(e, t, 200000, q(1, false));
  CHECA(e.alarmeAtivo());
  e.receber(q(2, false, 3.3f, SIMP_CMD_DESBLOQUEAR, 1), t);
  CHECA(!e.alarmeAtivo());
}

MINI_MAIN
