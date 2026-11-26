// Verificador: aprova respostas no prazo e reprova atrasos, antecipações e bloqueio sem causa.
#include "block.hpp"
#include "mini_teste.hpp"
#include "planta.hpp"
#include "verificador.hpp"

#include <cmath>

using namespace str_sim;

static EstadoPlanta est(bool ign, bool painel, double v = 3.3) {
  EstadoPlanta e;
  e.ignicao = ign;
  e.painel_ok = painel;
  e.v_pino = v;
  return e;
}

static EspParaSim rx(uint8_t ign_eco, uint8_t alarme = 0, uint8_t bloq = 0, uint8_t motivo = 0, uint8_t nivel = 0,
                     uint8_t cmd_eco = 0) {
  EspParaSim r{};
  r.ignicao_eco = ign_eco;
  r.alarme = alarme;
  r.bloqueio = bloq;
  r.motivo = motivo;
  r.nivel_bat = nivel;
  r.comando_id_eco = cmd_eco;
  return r;
}

static const ResultadoRegra *achar(const Verificador &v, const std::string &trecho) {
  for (auto &r : v.resultados())
    if (r.regra.find(trecho) != std::string::npos) return &r;
  return nullptr;
}

static const uint8_t ALARME = static_cast<uint8_t>(BlockReason::ALARM_OVERRIDE);
static const uint8_t USUARIO = static_cast<uint8_t>(BlockReason::USER_COMMAND);

TESTE(alarme_no_prazo_e_bloqueio_na_janela) {
  Verificador v;
  v.observarPlanta(est(1, 1), 0);
  v.observarResposta(rx(1), 10000);
  v.observarPlanta(est(1, 0), 1000000);
  v.observarResposta(rx(1, 1), 1060000);
  v.observarResposta(rx(1, 1, 1, ALARME), 6080000);
  v.finalizar(7000000);
  CHECA(v.falhas() == 0);
  auto a = achar(v, "-> alarme");
  CHECA(a && a->situacao == Situacao::Ok);
  CHECA(a ? std::fabs(a->latencia_ms - 60) < 1e-6 : false);
  auto b = achar(v, "-> bloqueio");
  CHECA(b && b->situacao == Situacao::Ok);
}

TESTE(alarme_atrasado_reprova) {
  Verificador v;
  v.observarPlanta(est(1, 1), 0);
  v.observarResposta(rx(1), 10000);
  v.observarPlanta(est(1, 0), 1000000);
  v.observarResposta(rx(1, 1), 1150000); // 150 ms > 100 ms
  auto a = achar(v, "-> alarme");
  CHECA(a && a->situacao == Situacao::Falhou && a->latencia_ms > 100);
}

TESTE(bloqueio_antecipado_reprova) {
  Verificador v;
  v.observarPlanta(est(1, 1), 0);
  v.observarResposta(rx(1), 10000);
  v.observarPlanta(est(1, 0), 1000000);
  v.observarResposta(rx(1, 1, 1, ALARME), 2000000); // 1 s depois; devia ser 5 s
  auto b = achar(v, "-> bloqueio");
  CHECA(b && b->situacao == Situacao::Falhou);
  CHECA(b && b->detalhe.find("antecipado") != std::string::npos);
}

TESTE(sem_resposta_reprova_por_timeout) {
  Verificador v;
  v.observarPlanta(est(1, 1), 0);
  v.observarResposta(rx(1), 10000);
  v.observarPlanta(est(1, 0), 1000000);
  for (uint64_t t = 1000000; t < 4000000; t += 10000) v.tick(t);
  auto a = achar(v, "-> alarme");
  CHECA(a && a->situacao == Situacao::Falhou);
}

TESTE(comando_confirma_com_estado_certo) {
  Verificador v;
  v.observarPlanta(est(1, 1), 0);
  v.observarResposta(rx(1), 5000);
  v.comandoEmitido(SIMP_CMD_BLOQUEAR, 1, 100000);
  v.observarResposta(rx(1, 0, 1, USUARIO, 0, 1), 112000);
  v.comandoEmitido(SIMP_CMD_DESBLOQUEAR, 2, 200000);
  v.observarResposta(rx(1, 0, 1, USUARIO, 0, 2), 210000); // eco ok, mas continua bloqueado
  auto c1 = achar(v, "'bloquear' (id 1)");
  auto c2 = achar(v, "'desbloquear' (id 2)");
  CHECA(c1 && c1->situacao == Situacao::Ok && std::fabs(c1->latencia_ms - 12) < 1e-6);
  CHECA(c2 && c2->situacao == Situacao::Falhou);
}

TESTE(bloqueio_sem_causa_reprova) {
  Verificador v;
  v.observarPlanta(est(1, 1), 0);
  v.observarResposta(rx(1), 10000);
  v.observarResposta(rx(1, 0, 1, USUARIO), 20000);
  CHECA(achar(v, "sem causa") != nullptr);
  CHECA(v.falhas() == 1);
}

TESTE(limiares_de_bateria_com_histerese) {
  Verificador v;
  v.observarPlanta(est(1, 1, 3.0), 0);
  v.observarResposta(rx(1), 5000);
  v.observarPlanta(est(1, 1, 2.49), 100000);
  v.observarPlanta(est(1, 1, 2.52), 110000); // oscilação: não rearma
  v.observarPlanta(est(1, 1, 2.48), 120000);
  v.observarResposta(rx(1, 0, 0, 0, SIMP_BAT_BAIXA), 150000);
  int n = 0;
  for (auto &r : v.resultados())
    if (r.regra.find("2,5 V") != std::string::npos) n++;
  CHECA(n == 1);
  CHECA(v.falhas() == 0);
}

TESTE(cenario_curto_demais_fica_inconclusivo) {
  Verificador v;
  v.observarPlanta(est(1, 1), 0);
  v.observarResposta(rx(1), 5000);
  v.observarPlanta(est(1, 0), 1000000);
  v.observarResposta(rx(1, 1), 1050000);
  v.finalizar(3000000); // terminou antes dos 5 s do bloqueio
  auto b = achar(v, "-> bloqueio");
  CHECA(b && b->situacao == Situacao::Inconclusivo);
  CHECA(v.falhas() == 0);
}

MINI_MAIN
