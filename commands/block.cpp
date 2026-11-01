#include "block.hpp"
#include <cstdio>

// Se estiver a compilar para a placa ESP32 física, inclui o driver nativo de
// GPIO do ESP-IDF
#if !CONFIG_IDF_TARGET_LINUX && defined(ESP_PLATFORM)
#include "driver/gpio.h"
#endif

Block::Block(uint32_t relay_gpio, uint32_t led_gpio)
    : blocked(false), reason(BlockReason::NONE), relayPin(relay_gpio),
      ledPin(led_gpio) {}

void Block::init() {
#if !CONFIG_IDF_TARGET_LINUX && defined(ESP_PLATFORM)
  gpio_reset_pin(static_cast<gpio_num_t>(relayPin));
  gpio_set_direction(static_cast<gpio_num_t>(relayPin), GPIO_MODE_OUTPUT);

  gpio_reset_pin(static_cast<gpio_num_t>(ledPin));
  gpio_set_direction(static_cast<gpio_num_t>(ledPin), GPIO_MODE_OUTPUT);
#endif
  updateHardware();
}

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

void Block::updateHardware() {
#if !CONFIG_IDF_TARGET_LINUX && defined(ESP_PLATFORM)
  // Aciona o Relé e o LED na ESP32 física (Secção III-A)
  gpio_set_level(static_cast<gpio_num_t>(relayPin), blocked ? 1 : 0);
  gpio_set_level(static_cast<gpio_num_t>(ledPin), blocked ? 1 : 0);
#else
  // No contentor FreeRTOS Linux / Simulador (CoppeliaSim / Gazebo):
  // Aqui a ponte de simulação (SimHAL) lê o estado ou publica no socket UDP/ZMQ
  std::printf("[SIM_HW] Atuador Block -> Rele(GPIO %u): %s | LED(GPIO %u): %s "
              "| Motivo: %s\n",
              relayPin, blocked ? "ATIVO (Corte de Ignicao)" : "INATIVO",
              ledPin, blocked ? "ON" : "OFF", getReasonString());
#endif
}

bool Block::isBlocked() const { return blocked; }

BlockReason Block::getReason() const { return reason; }

const char *Block::getReasonString() const {
  switch (reason) {
  case BlockReason::USER_COMMAND:
    return "Comando do Utilizador (T4)";
  case BlockReason::ALARM_OVERRIDE:
    return "Override de Violacao do Painel (T2)";
  case BlockReason::CRITICAL_BAT:
    return "Bateria Critica < 1.5V (T2)";
  default:
    return "Normal (Desbloqueado)";
  }
}
