// enlace.hpp — como a ponte conversa com a "ESP32".
//
//   EnlaceVirtual : ESP virtual no mesmo processo (testes, sem placa)
//   EnlaceUdp     : ESP virtual em outro processo (sim/apps/esp_virtual)
//                   ou firmware no alvo Linux do ESP-IDF
//   EnlaceSerial  : ESP32 física pela USB (HIL de verdade)
//
// UDP e serial usam o mesmo quadro de include/sim_protocol.h.
#pragma once

#include "esp_virtual.hpp"
#include "sim_protocol.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace str_sim {

struct EstatisticaEnlace {
  uint64_t quadros_enviados = 0;
  uint64_t quadros_recebidos = 0;
  uint64_t erros_crc = 0;
  uint64_t bytes_texto = 0; // bytes fora de quadro (logs da ESP)
};

class Enlace {
public:
  virtual ~Enlace() = default;
  virtual void enviar(const SimParaEsp &m, uint64_t t_us) = 0;
  // Não bloqueia. true se havia uma resposta.
  virtual bool receber(uint64_t t_us, EspParaSim &out) = 0;
  virtual std::string descricao() const = 0;
  const EstatisticaEnlace &estatistica() const { return est_; }
  // Linhas de texto que chegaram fora de quadro (ESP_LOGx da placa).
  std::function<void(const std::string &)> aoTexto;

protected:
  EstatisticaEnlace est_;
};

class EnlaceVirtual : public Enlace {
public:
  explicit EnlaceVirtual(EspVirtual &esp) : esp_(esp) {}
  void enviar(const SimParaEsp &m, uint64_t t_us) override;
  bool receber(uint64_t t_us, EspParaSim &out) override;
  std::string descricao() const override { return "ESP virtual (mesmo processo)"; }

private:
  EspVirtual &esp_;
};

// Base para transportes de bytes: monta/decodifica quadros e separa texto.
class EnlaceBytes : public Enlace {
public:
  void enviar(const SimParaEsp &m, uint64_t t_us) override;
  bool receber(uint64_t t_us, EspParaSim &out) override;

protected:
  virtual bool escreverBytes(const uint8_t *b, size_t n) = 0;
  virtual size_t lerBytes(uint8_t *b, size_t max) = 0; // não bloqueia

private:
  void alimentarTexto(uint8_t b);
  SimpDecodificador dec_{};
  std::string linha_;
  uint8_t buf_[512];
  size_t buf_n_ = 0, buf_i_ = 0;
};

// Socket UDP não bloqueante (também usado pelo app esp_virtual).
class SocketUdp {
public:
  // porta_local = 0 -> efêmera
  explicit SocketUdp(uint16_t porta_local);
  ~SocketUdp();
  SocketUdp(const SocketUdp &) = delete;
  SocketUdp &operator=(const SocketUdp &) = delete;
  void definirDestino(const std::string &host, uint16_t porta);
  bool temDestino() const { return tem_destino_; }
  bool enviar(const uint8_t *b, size_t n);
  // Lê um datagrama (não bloqueia). Se responderAoRemetente, o remetente
  // vira o destino dos próximos envios.
  size_t receber(uint8_t *b, size_t max, bool responderAoRemetente = false);
  // Espera até timeout_ms por um datagrama.
  size_t receberComTimeout(uint8_t *b, size_t max, int timeout_ms);
  uint16_t portaLocal() const { return porta_local_; }

private:
  intptr_t s_ = -1;
  uint16_t porta_local_ = 0;
  bool tem_destino_ = false;
  unsigned char destino_[32] = {}; // sockaddr_in
};

class EnlaceUdp : public EnlaceBytes {
public:
  EnlaceUdp(const std::string &host, uint16_t porta_remota, uint16_t porta_local = 0);
  std::string descricao() const override { return desc_; }

protected:
  bool escreverBytes(const uint8_t *b, size_t n) override;
  size_t lerBytes(uint8_t *b, size_t max) override;

private:
  SocketUdp sock_;
  std::string desc_;
};

class EnlaceSerial : public EnlaceBytes {
public:
  EnlaceSerial(const std::string &porta, int baud);
  ~EnlaceSerial() override;
  std::string descricao() const override { return desc_; }

protected:
  bool escreverBytes(const uint8_t *b, size_t n) override;
  size_t lerBytes(uint8_t *b, size_t max) override;

private:
  intptr_t h_ = -1;
  std::string desc_;
};

// "virtual" | "udp:host:porta" | "serial:COM5" | "serial:/dev/ttyUSB0"
std::unique_ptr<Enlace> criarEnlace(const std::string &spec, int baud, EspVirtual *esp_virtual);

} // namespace str_sim
