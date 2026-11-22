#include "modelos.hpp"

#include <algorithm>
#include <cmath>

namespace str_sim {

// Curva OCV por célula (SOC -> V), típica de Li-ion NMC.
static const double kSoc[] = {0.00, 0.05, 0.10, 0.20, 0.40, 0.60, 0.80, 1.00};
static const double kOcv[] = {3.00, 3.30, 3.45, 3.55, 3.68, 3.80, 3.95, 4.15};
static const int kPontos = sizeof(kSoc) / sizeof(kSoc[0]);

ModeloBateria::ModeloBateria(ParametrosBateria p) : p_(p) { setSoc(p.soc_inicial); }

double ModeloBateria::ocvCelula(double soc) {
  soc = std::clamp(soc, 0.0, 1.0);
  for (int i = 1; i < kPontos; ++i) {
    if (soc <= kSoc[i]) {
      double a = (soc - kSoc[i - 1]) / (kSoc[i] - kSoc[i - 1]);
      return kOcv[i - 1] + a * (kOcv[i] - kOcv[i - 1]);
    }
  }
  return kOcv[kPontos - 1];
}

void ModeloBateria::setSoc(double soc) { soc_ = std::clamp(soc, 0.0, 1.0); }

double ModeloBateria::vOc() const { return p_.celulas * ocvCelula(soc_); }

double ModeloBateria::vTerminal() const { return vOc() - p_.R_ohm * I_; }

double ModeloBateria::vPinoDe(double v_pacote) const {
  return std::clamp((v_pacote - p_.offset_V) * p_.ganho_pino, 0.0, p_.v_pino_max);
}

double ModeloBateria::vPino() const { return vPinoDe(vTerminal()); }

double ModeloBateria::passo(double P_W, double dt_s) {
  const double V = vOc();
  const double R = p_.R_ohm;
  // P = (V - R·I)·I  ->  R·I² - V·I + P = 0
  const double disc = V * V - 4.0 * R * P_W;
  double I = disc >= 0 ? (V - std::sqrt(disc)) / (2.0 * R) : V / (2.0 * R);
  if (I < 0) {
    I = std::max(I, -p_.I_carga_max_A);
    if (soc_ >= 1.0) I = 0.0;
  }
  if (soc_ <= 0.0 && I > 0) I = 0.0;
  I += dreno_A_;
  I_ = I;
  const double cap_As = p_.capacidade_Ah * 3600.0;
  setSoc(soc_ - fator_ * I * dt_s / cap_As);
  return I_;
}

// ---------------------------------------------------------------- Motor
double Motor::torqueRoda(double pedal, double w_roda) const {
  pedal = std::clamp(pedal, -1.0, 1.0);
  const double w_motor = std::max(std::abs(w_roda) * reducao, 1e-3);
  const double T_lim = std::min(T_max_motor, P_max / w_motor);
  return pedal * T_lim * reducao;
}

double Motor::potenciaEletrica(double T_roda, double w_roda) const {
  const double P_mec = T_roda * w_roda;
  return P_mec >= 0 ? P_mec / eta : P_mec * eta;
}

// ---------------------------------------------------------------- Veículo
void ModeloVeiculo::passo(double T_roda, double dt_s) {
  const double F_tracao = T_roda / p_.raio_roda_m;
  const double F_aero = 0.5 * p_.rho * p_.Cd * p_.area_m2 * v_ * v_;
  const double F_rol = v_ > 0 ? p_.Crr * p_.massa_kg * p_.g : 0.0;
  const double F = F_tracao - F_aero - F_rol;
  // O atrito pode parar o carro, mas não o empurra para trás.
  v_ = std::max(0.0, v_ + F / p_.massa_kg * dt_s);
  x_ += v_ * dt_s;
}

} // namespace str_sim
