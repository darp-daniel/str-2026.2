#pragma once

#define LOW_STATE 3.21
#define CRITICAL_STATE 3.1
#define DIFF_STATES 1.2

class Battery {
private:
  float tensao;
  float SoC;
  bool lowState;
  bool criticalState;

public:
  void medirTensao(float adc_reading);
  void calcularSoC();
  bool isLow();
  bool isCritical;

  // getters
  float getSoC();
};
