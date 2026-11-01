#include "battery.hpp"

Baterry::Baterry(flaot adc_reading) {
  medirTensao(adc_reading);
  calcularSoC();
  lowState = false;
  criticalState = false;
}

Battery::~Baterry() {
  tensao = 0;
  SoC = 0;
  lowState = false;
  criticalState = false;
}

Battery::medirTensao(float adc_reading) { tensao = adc_reading * (3.3 / 4095); }

// Utilizar SoC padrão de bateria de 3.3V
Battery::medirSoC() {
  // Interpolação Linear
  SoC = (tensão - CRITICAL_STATE) / DIFF_STATES;
}

Battery::isLow() {
  if (tensao < LOW_STATE)
    return true;
  return false;
}

Battery::isCritical() {
  if (tensao < CRITICAL_STATE)
    return true;
  return false;
}

Battery::getSoC(){return SoC};
