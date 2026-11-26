/*
 * sim_protocol.h — contrato de comunicação entre o simulador (PC) e a ESP32.
 *
 * Usado pelos dois lados:
 *   - firmware (ESP-IDF, modo HIL)      -> src/sim_link.cpp
 *   - ponte do simulador (PC, sim/)     -> sim/nucleo/enlace.cpp
 *
 * Quadro (frame) no fio, igual para UART e UDP:
 *
 *   [0xA5][0x5A][tipo][len][payload ... len bytes][crc_lo][crc_hi]
 *
 *   crc = CRC-16/CCITT-FALSE sobre (tipo, len, payload).
 *
 * Os bytes de sincronismo + CRC permitem que a ponte descarte texto de log
 * (ESP_LOGx) que chega misturado na mesma UART e se ressincronize sozinha.
 *
 * Header em C puro, sem alocação dinâmica, para ser usado no firmware.
 * Ambos os lados são little-endian (x86/ARM do PC e Xtensa da ESP32).
 */
#ifndef STR_SIM_PROTOCOL_H
#define STR_SIM_PROTOCOL_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SIMP_SYNC0 0xA5u
#define SIMP_SYNC1 0x5Au
#define SIMP_MAX_PAYLOAD 48u
#define SIMP_OVERHEAD 6u /* 2 sync + tipo + len + 2 crc */
#define SIMP_MAX_QUADRO (SIMP_MAX_PAYLOAD + SIMP_OVERHEAD)

/* Tipos de mensagem */
enum {
  SIMP_TIPO_SIM_PARA_ESP = 1,
  SIMP_TIPO_ESP_PARA_SIM = 2
};

/* Comandos do usuário (T4). Cada comando novo recebe um comando_id novo; a
 * ponte repete o mesmo id até receber o eco, e a ESP trata cada id uma vez. */
enum {
  SIMP_CMD_NENHUM = 0,
  SIMP_CMD_BLOQUEAR = 1,
  SIMP_CMD_DESBLOQUEAR = 2,
  SIMP_CMD_ATUALIZAR = 3 /* atualização imediata do dashboard (T4 -> T5) */
};

/* Nível da bateria calculado pela ESP (T2) */
enum {
  SIMP_BAT_NORMAL = 0,
  SIMP_BAT_BAIXA = 1,  /* v_pino <= 2,5 V */
  SIMP_BAT_CRITICA = 2 /* v_pino <= 1,5 V */
};

#define SIMP_LIMIAR_BAT_BAIXA 2.5f
#define SIMP_LIMIAR_BAT_CRITICA 1.5f

#pragma pack(push, 1)

/* Simulador -> ESP32: estado dos "sensores" do carro (a cada passo da simulação) */
typedef struct {
  uint32_t seq;        /* número de sequência (a ESP ecoa o último recebido) */
  uint32_t t_sim_ms;   /* tempo de simulação */
  uint8_t ignicao;     /* 0/1 */
  uint8_t painel_ok;   /* 1 = conforme, 0 = violado */
  uint8_t comando;     /* SIMP_CMD_* */
  uint8_t comando_id;  /* id do comando acima (0 = nenhum ainda) */
  float v_bat_pino;    /* tensão no pino do ADC, 0..3,3 V */
  float velocidade;    /* m/s, só para o dashboard */
} SimParaEsp;

/* ESP32 -> Simulador: atuação e estado interno do firmware */
typedef struct {
  uint32_t seq_eco;       /* último seq recebido (mede ida e volta) */
  uint32_t t_esp_ms;      /* relógio da ESP (informativo) */
  uint8_t bloqueio;       /* 0/1 -> corta o torque no simulador */
  uint8_t alarme;         /* 0/1 */
  uint8_t motivo;         /* BlockReason (block.hpp) */
  uint8_t nivel_bat;      /* SIMP_BAT_* */
  uint8_t ignicao_eco;    /* ignição como a ESP enxerga (estado do dashboard) */
  uint8_t comando_id_eco; /* último comando_id processado por T4 */
  uint16_t reservado;
} EspParaSim;

#pragma pack(pop)

typedef char simp_check_tam1[(sizeof(SimParaEsp) == 20) ? 1 : -1];
typedef char simp_check_tam2[(sizeof(EspParaSim) == 16) ? 1 : -1];

/* CRC-16/CCITT-FALSE (poly 0x1021, init 0xFFFF). crc("123456789") = 0x29B1 */
static inline uint16_t simp_crc16_atualiza(uint16_t crc, uint8_t b) {
  crc ^= (uint16_t)((uint16_t)b << 8);
  for (int i = 0; i < 8; ++i)
    crc = (crc & 0x8000u) ? (uint16_t)((crc << 1) ^ 0x1021u) : (uint16_t)(crc << 1);
  return crc;
}

static inline uint16_t simp_crc16(const uint8_t *dados, size_t n) {
  uint16_t crc = 0xFFFFu;
  for (size_t i = 0; i < n; ++i) crc = simp_crc16_atualiza(crc, dados[i]);
  return crc;
}

/* Monta um quadro em `saida` (capacidade >= len + SIMP_OVERHEAD).
 * Retorna o tamanho do quadro, ou 0 se len for grande demais. */
static inline size_t simp_montar(uint8_t tipo, const void *payload, uint8_t len,
                                 uint8_t *saida) {
  if (len > SIMP_MAX_PAYLOAD) return 0;
  saida[0] = SIMP_SYNC0;
  saida[1] = SIMP_SYNC1;
  saida[2] = tipo;
  saida[3] = len;
  memcpy(&saida[4], payload, len);
  uint16_t crc = simp_crc16(&saida[2], (size_t)len + 2u);
  saida[4 + len] = (uint8_t)(crc & 0xFFu);
  saida[5 + len] = (uint8_t)(crc >> 8);
  return (size_t)len + SIMP_OVERHEAD;
}

/* Decodificador byte a byte (máquina de estados). Tolerante a lixo/texto. */
typedef struct {
  uint8_t estado;
  uint8_t tipo;
  uint8_t len;
  uint8_t idx;
  uint16_t crc_calc;
  uint8_t crc_lo;
  uint8_t payload[SIMP_MAX_PAYLOAD];
  uint32_t quadros_ok;
  uint32_t erros_crc;
  uint32_t bytes_descartados; /* bytes fora de quadro (ex.: texto de log) */
} SimpDecodificador;

static inline void simp_decodificador_init(SimpDecodificador *d) {
  memset(d, 0, sizeof(*d));
}

/* Alimenta um byte. Retorna o tipo do quadro quando um quadro válido termina
 * (payload em d->payload, tamanho d->len); senão retorna 0. */
static inline uint8_t simp_alimentar(SimpDecodificador *d, uint8_t b) {
  switch (d->estado) {
  case 0: /* procurando SYNC0 */
    if (b == SIMP_SYNC0) d->estado = 1;
    else d->bytes_descartados++;
    return 0;
  case 1: /* esperando SYNC1 */
    if (b == SIMP_SYNC1) d->estado = 2;
    else if (b == SIMP_SYNC0) { d->bytes_descartados++; } /* continua em 1 */
    else { d->bytes_descartados += 2; d->estado = 0; }
    return 0;
  case 2: /* tipo */
    d->tipo = b;
    d->crc_calc = simp_crc16_atualiza(0xFFFFu, b);
    d->estado = 3;
    return 0;
  case 3: /* len */
    if (b > SIMP_MAX_PAYLOAD) { d->erros_crc++; d->estado = (b == SIMP_SYNC0) ? 1 : 0; return 0; }
    d->len = b;
    d->idx = 0;
    d->crc_calc = simp_crc16_atualiza(d->crc_calc, b);
    d->estado = (b == 0) ? 5 : 4;
    return 0;
  case 4: /* payload */
    d->payload[d->idx++] = b;
    d->crc_calc = simp_crc16_atualiza(d->crc_calc, b);
    if (d->idx >= d->len) d->estado = 5;
    return 0;
  case 5: /* crc lo */
    d->crc_lo = b;
    d->estado = 6;
    return 0;
  default: { /* 6: crc hi */
    uint16_t crc_rx = (uint16_t)(d->crc_lo | ((uint16_t)b << 8));
    d->estado = 0;
    if (crc_rx == d->crc_calc) { d->quadros_ok++; return d->tipo; }
    d->erros_crc++;
    return 0;
  }
  }
}

#ifdef __cplusplus
}
#endif

#endif /* STR_SIM_PROTOCOL_H */
