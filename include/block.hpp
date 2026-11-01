#pragma once
#include <cstdint>

// Enum para rastrear a origem do bloqueio no Dashboard (T5) e nas Mensagens
// (T3)
enum class BlockReason {
  NONE = 0,
  USER_COMMAND,   // Acionado remotamente pelo utilizador (Tarefa T4)
  ALARM_OVERRIDE, // Acionado por timeout de violação do painel (Tarefa T2 /
                  // Alarm)
  CRITICAL_BAT // Opcional: Acionado por subtensão severa < 1.5V (Tarefa T2 /
               // Battery)
};

class Block {
private:
  bool blocked = false; // Estado atual do bloqueio (true = bloqueado)
  BlockReason reason = BlockReason::NONE; // Motivo do bloqueio ativo
  uint32_t relayPin; // Pino do Relé físico/simulado (Secção III-A)
  uint32_t ledPin;   // Pino do LED indicador de bloqueio (Secção III-A)

  // Método interno que efetua o acionamento físico (GPIO) ou envia ao Simulador
  void updateHardware();

public:
  // Construtor com pinos padrão da ESP32 (ex: GPIO 26 p/ Relé, GPIO 27 p/ LED)
  Block(uint32_t relay_gpio = 26, uint32_t led_gpio = 27);

  // Inicializa os pinos ou a ponte de simulação
  void init();

  // Chamado por T4 quando o utilizador envia comando de bloqueio externo
  void lockByUser();

  // Chamado por Alarm::chamarBlock() em T2 quando estoura o MAX_TIME_ACTIVE
  void lockByOverride();

  // Chamado por T4 para destravar o veículo (exige comando explícito do
  // utilizador)
  void unlock();

  // Getters utilizados por T2 (Ignition::combustao) e T5 (UpdateDashboard)
  bool isBlocked() const;
  BlockReason getReason() const;
  const char *getReasonString() const;
};
