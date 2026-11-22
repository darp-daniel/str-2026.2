#include "block.hpp"
#include "driver/gpio.h"
#include "esp_log.h"
static constexpr int RELAY_ACTIVE_LEVEL = 0; // mudar para 1 se modulo for ativo em alto
static const char *TAG = "BLOCK";
Block::Block(uint32_t relay_gpio, uint32_t led_gpio): blocked(false), reason(BlockReason::NONE), relayPin(relay_gpio), ledPin(led_gpio) {}
void Block::init() {
  gpio_set_level(static_cast<gpio_num_t>(relayPin), 1-RELAY_ACTIVE_LEVEL);
  gpio_reset_pin(static_cast<gpio_num_t>(relayPin));
  gpio_set_direction(static_cast<gpio_num_t>(relayPin), GPIO_MODE_OUTPUT);
  updateHardware();
}
void Block::lockByUser(){ if(!blocked){blocked=true;reason=BlockReason::USER_COMMAND;updateHardware();}}
void Block::lockByOverride(){ if(!blocked){blocked=true;reason=BlockReason::ALARM_OVERRIDE;updateHardware();}}
void Block::unlock(){if(blocked){blocked=false;reason=BlockReason::NONE;updateHardware();}}
void Block::updateHardware(){gpio_set_level(static_cast<gpio_num_t>(relayPin), blocked?RELAY_ACTIVE_LEVEL:1-RELAY_ACTIVE_LEVEL);ESP_LOGI(TAG,"Bloqueio: %s | %s",blocked?"ON":"OFF",getReasonString());}
bool Block::isBlocked() const{return blocked;}
BlockReason Block::getReason() const{return reason;}
const char *Block::getReasonString() const{switch(reason){case BlockReason::USER_COMMAND:return "Comando usuario";case BlockReason::ALARM_OVERRIDE:return "Violacao painel";case BlockReason::CRITICAL_BAT:return "Bateria critica";default:return "Normal";}}
