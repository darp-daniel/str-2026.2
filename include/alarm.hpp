#pragma once
#include "block.hpp"
#include <chrono>
#define MAX_TIME_ACTIVE 5000
class Alarm {
  std::chrono::steady_clock::time_point timeActivated;
  bool active;
public:
  Alarm();
  ~Alarm();
  void ativar();
  void desativar();
  bool chamarBlock(std::chrono::steady_clock::time_point agora, Block &blockActuator);
  bool isActive();
};
