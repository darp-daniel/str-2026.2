// planta.hpp — o "carro": de onde vêm os sinais lidos pela ESP e para onde
// vai a atuação dela (bloqueio -> corte de torque).
//
//   PlantaInterna  : dinâmica 1D em C++ (testes rápidos, sem CoppeliaSim)
//   PlantaCoppelia : dinâmica no CoppeliaSim, via sim/coppelia/ponte_coppelia.py
//
// Nas duas, bateria e motor são os modelos de modelos.hpp.
#pragma once

#include "cenario.hpp"
#include "enlace.hpp"
#include "modelos.hpp"
#include "sim_protocol.h"

#include <memory>
#include <string>

namespace str_sim {

struct EstadoPlanta {
  bool ignicao = false;
  bool painel_ok = true;
  double pedal = 0;
  double v_pino = 3.3;
  double soc = 0;
  double corrente_A = 0;
  double velocidade = 0; // m/s
  double torque = 0;     // N·m nas rodas (o que foi aplicado no último passo)
  double t_sim = 0;      // tempo do motor de física (s)
  int comando_ui = SIMP_CMD_NENHUM; // pedido pela interface do CoppeliaSim
};

class Planta {
public:
  virtual ~Planta() = default;
  virtual void aplicar(const AcaoCenario &a); // ações do cenário
  virtual EstadoPlanta ler();                 // consome comando_ui
  // Resposta mais recente da ESP (tem_rx = false enquanto nada chegou).
  virtual void atuar(const EspParaSim &rx, bool tem_rx);
  virtual void passo(double dt_s) = 0;
  virtual void status(const std::string &) {}
  virtual std::string descricao() const = 0;

  const ModeloBateria &bateria() const { return bat_; }

protected:
  double torqueComandado(double w_roda) const;

  ModeloBateria bat_;
  Motor motor_;
  bool ignicao_ = false;
  bool painel_ok_ = true;
  double pedal_ = 0;
  bool bloqueio_ = false;
  EspParaSim rx_{};
  bool tem_rx_ = false;
  double torque_ = 0;
  double velocidade_ = 0;
  double t_sim_ = 0;
  int comando_ui_ = SIMP_CMD_NENHUM;
};

class PlantaInterna : public Planta {
public:
  void passo(double dt_s) override;
  std::string descricao() const override { return "planta interna (dinamica 1D em C++)"; }
  const ModeloVeiculo &veiculo() const { return veic_; }

private:
  ModeloVeiculo veic_;
};

// Conversa por UDP (texto "chave=valor;...") com sim/coppelia/ponte_coppelia.py,
// que por sua vez chama o CoppeliaSim pela ZeroMQ Remote API.
class PlantaCoppelia : public Planta {
public:
  explicit PlantaCoppelia(uint16_t porta_adaptador = 5010, int timeout_ms = 2000);
  ~PlantaCoppelia() override;
  void aplicar(const AcaoCenario &a) override;
  void passo(double dt_s) override;
  void status(const std::string &s) override { status_ = s; }
  std::string descricao() const override { return "CoppeliaSim (via ponte_coppelia.py)"; }
  double dtCena() const { return dt_cena_; }

private:
  std::string trocar(const std::string &msg);

  SocketUdp sock_;
  int timeout_ms_;
  double dt_cena_ = 0.01;
  int forcar_ign_ = -1, forcar_painel_ = -1;
  double forcar_pedal_ = -9;
  bool desc_ui_ = false;
  std::string status_;
};

std::unique_ptr<Planta> criarPlanta(const std::string &tipo);

} // namespace str_sim
