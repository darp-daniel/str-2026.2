// Protocolo de quadros: CRC, montagem, decodificação com lixo e corrupção.
#include "mini_teste.hpp"
#include "sim_protocol.h"

#include <cstring>
#include <string>
#include <vector>

static std::vector<uint8_t> quadro(const SimParaEsp &m) {
  std::vector<uint8_t> q(SIMP_MAX_QUADRO);
  q.resize(simp_montar(SIMP_TIPO_SIM_PARA_ESP, &m, sizeof m, q.data()));
  return q;
}

static SimParaEsp exemplo(uint32_t seq) {
  SimParaEsp m{};
  m.seq = seq;
  m.t_sim_ms = seq * 10;
  m.ignicao = 1;
  m.painel_ok = 0;
  m.comando = SIMP_CMD_BLOQUEAR;
  m.comando_id = 7;
  m.v_bat_pino = 2.49f;
  m.velocidade = 12.5f;
  return m;
}

static std::vector<uint32_t> decodificar(const std::vector<uint8_t> &fluxo, SimpDecodificador &d) {
  std::vector<uint32_t> seqs;
  for (uint8_t b : fluxo)
    if (simp_alimentar(&d, b) == SIMP_TIPO_SIM_PARA_ESP) {
      SimParaEsp m;
      std::memcpy(&m, d.payload, sizeof m);
      seqs.push_back(m.seq);
    }
  return seqs;
}

TESTE(crc_vetor_conhecido) {
  const char *s = "123456789";
  CHECA(simp_crc16(reinterpret_cast<const uint8_t *>(s), 9) == 0x29B1);
}

TESTE(tamanhos_fixos) {
  CHECA(sizeof(SimParaEsp) == 20);
  CHECA(sizeof(EspParaSim) == 16);
  CHECA(quadro(exemplo(1)).size() == 26);
}

TESTE(ida_e_volta) {
  auto q = quadro(exemplo(42));
  SimpDecodificador d;
  simp_decodificador_init(&d);
  int achou = 0;
  for (size_t i = 0; i < q.size(); ++i) {
    uint8_t t = simp_alimentar(&d, q[i]);
    if (t) {
      achou++;
      CHECA(i == q.size() - 1);
      CHECA(t == SIMP_TIPO_SIM_PARA_ESP);
    }
  }
  CHECA(achou == 1);
  SimParaEsp m;
  std::memcpy(&m, d.payload, sizeof m);
  CHECA(m.seq == 42 && m.comando_id == 7 && m.painel_ok == 0);
  CHECA(m.v_bat_pino == 2.49f);
}

TESTE(ressincroniza_com_texto_de_log_no_meio) {
  // Como na UART da ESP: logs ESP_LOGx misturados com quadros.
  std::string log1 = "I (1234) STR: Ignicao=ON Painel=CONFORME\r\n";
  std::string log2 = "W (1300) STR: VIOLACAO DO PAINEL! ";
  log2.push_back(static_cast<char>(SIMP_SYNC0)); // byte de sync perdido no meio do texto
  log2 += " lixo\n";
  std::vector<uint8_t> fluxo(log1.begin(), log1.end());
  auto q1 = quadro(exemplo(1));
  fluxo.insert(fluxo.end(), q1.begin(), q1.end());
  fluxo.insert(fluxo.end(), log2.begin(), log2.end());
  auto q2 = quadro(exemplo(2));
  fluxo.insert(fluxo.end(), q2.begin(), q2.end());
  fluxo.push_back(SIMP_SYNC0); // sync duplicado
  auto q3 = quadro(exemplo(3));
  fluxo.insert(fluxo.end(), q3.begin(), q3.end());

  SimpDecodificador d;
  simp_decodificador_init(&d);
  auto seqs = decodificar(fluxo, d);
  CHECA(seqs.size() == 3);
  CHECA(seqs.size() == 3 && seqs[0] == 1 && seqs[1] == 2 && seqs[2] == 3);
  CHECA(d.bytes_descartados > 0);
}

TESTE(rejeita_quadro_corrompido) {
  auto q = quadro(exemplo(5));
  q[8] ^= 0x10; // um bit trocado no payload
  auto q2 = quadro(exemplo(6));
  q.insert(q.end(), q2.begin(), q2.end());
  SimpDecodificador d;
  simp_decodificador_init(&d);
  auto seqs = decodificar(q, d);
  CHECA(d.erros_crc == 1);
  CHECA(seqs.size() == 1 && seqs[0] == 6); // o quadro seguinte ainda é aproveitado
}

TESTE(len_invalido_nao_trava) {
  std::vector<uint8_t> fluxo = {SIMP_SYNC0, SIMP_SYNC1, 1, 200, 1, 2, 3};
  auto q = quadro(exemplo(9));
  fluxo.insert(fluxo.end(), q.begin(), q.end());
  SimpDecodificador d;
  simp_decodificador_init(&d);
  auto seqs = decodificar(fluxo, d);
  CHECA(seqs.size() == 1 && seqs[0] == 9);
}

TESTE(recusa_payload_grande_demais) {
  uint8_t grande[SIMP_MAX_PAYLOAD + 1] = {};
  uint8_t saida[SIMP_MAX_QUADRO + 8];
  CHECA(simp_montar(1, grande, sizeof grande, saida) == 0);
}

MINI_MAIN
