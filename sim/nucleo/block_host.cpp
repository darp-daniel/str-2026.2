// block_host.cpp — implementação do Block (include/block.hpp) para o PC.
//
// Mesma lógica de commands/block.cpp, sem GPIO: o "relé" aqui é só o estado,
// que a ESP virtual devolve ao simulador. O firmware real continua usando
// commands/block.cpp.
#include "block.hpp"

Block::Block(uint32_t relay_gpio, uint32_t led_gpio)
    : blocked(false), reason(BlockReason::NONE), relayPin(relay_gpio), ledPin(led_gpio) {}

void Block::init() { updateHardware(); }

void Block::lockByUser() {
  if (!blocked) {
    blocked = true;
    reason = BlockReason::USER_COMMAND;
    updateHardware();
  }
}

void Block::lockByOverride() {
  if (!blocked) {
    blocked = true;
    reason = BlockReason::ALARM_OVERRIDE;
    updateHardware();
  }
}

void Block::unlock() {
  if (blocked) {
    blocked = false;
    reason = BlockReason::NONE;
    updateHardware();
  }
}

void Block::updateHardware() {}

bool Block::isBlocked() const { return blocked; }
BlockReason Block::getReason() const { return reason; }

const char *Block::getReasonString() const {
  switch (reason) {
  case BlockReason::USER_COMMAND: return "Comando usuario";
  case BlockReason::ALARM_OVERRIDE: return "Violacao painel";
  case BlockReason::CRITICAL_BAT: return "Bateria critica";
  default: return "Normal";
  }
}
