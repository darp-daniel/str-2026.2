#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "alarm.hpp"
#include "block.hpp"
#include <chrono>
static const char *TAG="STR";
static Block bloqueio(26,27); // GPIO 27 nao e usado: LED esta nos contatos do rele
static Alarm alarme;
static void tarefaSensores(void*) {
  bool ignicao=false, painel=true;
  bool anteriorPainel=true;
  bool primeiro=true;
  for (;;) {
    // botoes conectam GPIO ao GND; pressionado = true
    ignicao = gpio_get_level(GPIO_NUM_18)==0;
    painel = gpio_get_level(GPIO_NUM_19)==0;
    if (primeiro) { anteriorPainel=painel; primeiro=false; }
    if (anteriorPainel && !painel) {
      ESP_LOGW(TAG,"VIOLACAO DO PAINEL! Alarme iniciado (5 s)");
      alarme.ativar();
    }
    anteriorPainel=painel;
    if (alarme.chamarBlock(std::chrono::steady_clock::now(),bloqueio)) {
      ESP_LOGE(TAG,"Timeout alarme: BLOQUEIO ACIONADO");
    }
    static int cont=0;
    if (++cont>=20) {
      cont=0;
      ESP_LOGI(TAG,"Ignicao=%s Painel=%s Alarme=%s Bloqueio=%s",ignicao?"ON":"OFF",painel?"CONFORME":"VIOLADO",alarme.isActive()?"ON":"OFF",bloqueio.isBlocked()?"ON":"OFF");
    }
    vTaskDelay(pdMS_TO_TICKS(100));
  }
}
extern "C" void app_main(void) {
  // entradas com pull-up interno: solto=alto, pressionado=baixo
  gpio_config_t in={};
  in.pin_bit_mask=(1ULL<<18)|(1ULL<<19);
  in.mode=GPIO_MODE_INPUT;
  in.pull_up_en=GPIO_PULLUP_ENABLE;
  in.pull_down_en=GPIO_PULLDOWN_DISABLE;
  in.intr_type=GPIO_INTR_DISABLE;
  ESP_ERROR_CHECK(gpio_config(&in));
  bloqueio.init();
  ESP_LOGI(TAG,"Teste iniciado: pressione e solte GPIO19 para simular violacao");
  xTaskCreate(tarefaSensores,"sensores_alarme",4096,nullptr,5,nullptr);
}
