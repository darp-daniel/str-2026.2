// modelos.hpp — modelos físicos do carro elétrico (bateria, motor, dinâmica).
//
// Ficam em C++ puro para serem testados sem o CoppeliaSim. No modo
// CoppeliaSim, a dinâmica (chassi, rodas, atrito) é do motor de física do
// simulador; bateria e motor continuam vindo daqui.
#pragma once

namespace str_sim {

// ---------------------------------------------------------------- Bateria
//
// Pacote de 96 células Li-ion em série, circuito equivalente Voc(SOC) - R·I.
//
// A tensão no pino do ADC sai de um circuito condicionador
// (amplificador diferencial):  V_pino = (V_pacote - offset) * ganho,
// limitada a 0..3,3 V. O ganho e o offset foram escolhidos para que, em
// circuito aberto:
//     SOC = 20 %  ->  V_pino = 2,5 V   (limiar "bateria baixa")
//     SOC =  5 %  ->  V_pino = 1,5 V   (limiar "bateria crítica")
// Acima de ~45 % de SOC o pino satura em 3,3 V; a faixa útil do ADC fica
// concentrada onde os alertas acontecem.
struct ParametrosBateria {
  int celulas = 96;
  double capacidade_Ah = 60.0;
  double R_ohm = 0.08;        // resistência interna do pacote
  double soc_inicial = 0.90;
  double ganho_pino = 1.0 / 24.0;
  double offset_V = 280.8;
  double v_pino_max = 3.3;
  double I_carga_max_A = 150.0; // limite de corrente de regeneração
};

class ModeloBateria {
public:
  explicit ModeloBateria(ParametrosBateria p = {});

  static double ocvCelula(double soc); // curva tabelada, interpolada

  // Avança dt segundos com potência elétrica P (W). P > 0 = descarga.
  // Retorna a corrente (A, positiva = descarga).
  double passo(double P_W, double dt_s);

  double soc() const { return soc_; }
  double corrente() const { return I_; }
  double vOc() const;
  double vTerminal() const;
  double vPino() const;
  double vPinoDe(double v_pacote) const;

  void setSoc(double soc);
  // Descarga acelerada para os cenários de teste: multiplica a variação de
  // SOC (não a corrente). fator = 1 -> normal.
  void setFatorDescarga(double f) { fator_ = f < 0 ? 0 : f; }
  // Consumo parasita constante (A), somado à corrente do motor.
  void setDreno(double A) { dreno_A_ = A; }
  const ParametrosBateria &parametros() const { return p_; }

private:
  ParametrosBateria p_;
  double soc_;
  double I_ = 0.0;
  double fator_ = 1.0;
  double dreno_A_ = 0.0;
};

// ---------------------------------------------------------------- Motor
// Motor de tração com redução fixa. Torque constante até a velocidade-base e
// potência constante acima dela. Valores no eixo da roda.
struct Motor {
  double T_max_motor = 250.0; // N·m no eixo do motor
  double P_max = 100e3;       // W
  double reducao = 9.0;
  double eta = 0.90;

  // pedal em [-1, 1] (negativo = frenagem regenerativa). Retorna o torque
  // TOTAL no eixo das rodas motrizes (N·m).
  double torqueRoda(double pedal, double w_roda) const;
  // Potência elétrica pedida à bateria (W) para um torque/rotação na roda.
  double potenciaEletrica(double T_roda, double w_roda) const;
};

// ---------------------------------------------------------------- Veículo
// Dinâmica longitudinal 1D, usada quando o CoppeliaSim não está no laço.
struct ParametrosVeiculo {
  double massa_kg = 1500.0;
  double raio_roda_m = 0.31;
  double Cd = 0.30;
  double area_m2 = 2.2;
  double rho = 1.2;
  double Crr = 0.012;
  double g = 9.81;
};

class ModeloVeiculo {
public:
  explicit ModeloVeiculo(ParametrosVeiculo p = {}) : p_(p) {}
  // Avança dt com torque total nas rodas. O carro não anda de ré.
  void passo(double T_roda, double dt_s);
  double velocidade() const { return v_; } // m/s
  double wRoda() const { return v_ / p_.raio_roda_m; }
  double posicao() const { return x_; }
  void setVelocidade(double v) { v_ = v < 0 ? 0 : v; }
  const ParametrosVeiculo &parametros() const { return p_; }

private:
  ParametrosVeiculo p_;
  double v_ = 0.0;
  double x_ = 0.0;
};

} // namespace str_sim
