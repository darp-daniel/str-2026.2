#pragma once
#include "block.hpp"
#include <chrono>
#include <cstdint>

#define MAX_TIME_ACTIVE 5000

class Alarm {
private:
  std::chrono::steady_clock::time_point timeActivated;
  bool active;

public:
  void ativar();
  void desativar();
  bool chamarBlock(std::chrono::steady_clock::time_point dt_ms,
                   Block &blockAtuator);
  bool isActive();
};
