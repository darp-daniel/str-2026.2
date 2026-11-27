#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "alarm.hpp"
#include "block.hpp"
#include <chrono>
#include "sim_link.hpp"

static const char *TAG="STR";
static Block bloqueio(26,27); // GPIO 27 nao e usado: LED esta nos contatos do rele
static Alarm alarme;

// Block e Alarm sao usados pela tarefa de sensores e pela de comandos (T4).
static SemaphoreHandle_t g_mutex;

// Periodo da coleta/interpretacao (Tabela I: T1/T2 = 50 ms).
static constexpr int PERIODO_MS = 50;

#if CONFIG_STR_MODO_SIM

static uint8_t g_nivel_bat = SIMP_BAT_NORMAL;
static uint8_t g_cmd_eco = 0;

// Estado da ESP -> simulador. Chamar SEM o mutex.
static void publicarEstado(const SimParaEsp &q) {
  EspParaSim r{};
  xSemaphoreTake(g_mutex, portMAX_DELAY);
  r.seq_eco = q.seq;
  r.t_esp_ms = static_cast<uint32_t>(esp_timer_get_time() / 1000);
  r.bloqueio = bloqueio.isBlocked() ? 1 : 0;
  r.alarme = alarme.isActive() ? 1 : 0;
  r.motivo = static_cast<uint8_t>(bloqueio.getReason());
  r.nivel_bat = g_nivel_bat;
  r.ignicao_eco = q.ignicao;
  r.comando_id_eco = g_cmd_eco;
  xSemaphoreGive(g_mutex);
  sim_link::enviar(r);
}

#endif

// T4 - Comando: esporadica, liberada pela chegada de um comando do usuario.
static void tarefaComandos(void*) {
  sim_link::Comando c;

  for (;;) {
    if (xQueueReceive(sim_link::filaComandos(), &c, portMAX_DELAY) != pdTRUE)
      continue;
      #if !CONFIG_STR_MODO_SIM
          const int64_t t_inicio_us = esp_timer_get_time();
      #endif

    xSemaphoreTake(g_mutex, portMAX_DELAY);

    switch (c.cmd) {
      case SIMP_CMD_BLOQUEAR:
        bloqueio.lockByUser();
        break;

      case SIMP_CMD_DESBLOQUEAR:
        bloqueio.unlock();
        alarme.desativar();
        break;

      default:
        break; // ATUALIZAR: so publica o estado agora (T4 -> T5)
    }

#if CONFIG_STR_MODO_SIM

    g_cmd_eco = c.id;

#else

    const bool alarmeOn = alarme.isActive();
    const bool bloqOn = bloqueio.isBlocked();

#endif

    xSemaphoreGive(g_mutex);

#if CONFIG_STR_MODO_SIM

    SimParaEsp q{};
    sim_link::ultimoQuadro(q);
    publicarEstado(q);

#else

    const int64_t t_fim_us = esp_timer_get_time();

const int64_t interpretacao_us =
    c.t_fila_us - c.t_rx_us;

const int64_t espera_us =
    t_inicio_us - c.t_fila_us;

const int64_t processamento_us =
    t_fim_us - t_inicio_us;

const int64_t resposta_us =
    t_fim_us - c.t_rx_us;

  ESP_LOGI(TAG,
      "Comando=%u ID=%u Alarme=%s Bloqueio=%s",
      static_cast<unsigned>(c.cmd),
      static_cast<unsigned>(c.id),
      alarmeOn ? "ON" : "OFF",
      bloqOn ? "ON" : "OFF");

  ESP_LOGI(TAG,
      "C4 ID=%u | Interpretacao=%lld us | "
      "Espera=%lld us | Processamento=%lld us | "
      "Resposta=%lld us | Deadline interno=%s",
      static_cast<unsigned>(c.id),
      static_cast<long long>(interpretacao_us),
      static_cast<long long>(espera_us),
      static_cast<long long>(processamento_us),
      static_cast<long long>(resposta_us),
      resposta_us <= 30000 ? "OK" : "VIOLADO");

#endif
  }
}

static void tarefaSensores(void*) {
  bool ignicao=false, painel=true;
  bool anteriorPainel=true;
  bool primeiro=true;
  TickType_t ultimo = xTaskGetTickCount();
  for (;;) {
    xTaskDelayUntil(&ultimo, pdMS_TO_TICKS(PERIODO_MS)); // liberacao periodica, sem deriva
#if CONFIG_STR_MODO_SIM
    SimParaEsp q{};
    if (!sim_link::ultimoQuadro(q)) continue; // simulador ainda nao mandou nada
    ignicao = q.ignicao != 0;
    painel = q.painel_ok != 0;
#else
    // botoes conectam GPIO ao GND; pressionado = true
    ignicao = gpio_get_level(GPIO_NUM_18)==0;
    painel = gpio_get_level(GPIO_NUM_19)==0;
#endif
    xSemaphoreTake(g_mutex, portMAX_DELAY);
    if (primeiro) { anteriorPainel=painel; primeiro=false; }
    if (anteriorPainel && !painel) {
      ESP_LOGW(TAG,"VIOLACAO DO PAINEL! Alarme iniciado (5 s)");
      alarme.ativar();
    }
    anteriorPainel=painel;
    if (alarme.chamarBlock(std::chrono::steady_clock::now(),bloqueio)) {
      ESP_LOGE(TAG,"Timeout alarme: BLOQUEIO ACIONADO");
    }
#if CONFIG_STR_MODO_SIM
    // Nivel da bateria pela tensao no pino (por enquanto vinda do simulador;
    // na placa, trocar por leitura do ADC calibrado). Histerese de 0,05 V.
    {
      const float v = q.v_bat_pino, h = 0.05f;
      uint8_t n = v <= SIMP_LIMIAR_BAT_CRITICA ? SIMP_BAT_CRITICA
                : v <= SIMP_LIMIAR_BAT_BAIXA   ? SIMP_BAT_BAIXA : SIMP_BAT_NORMAL;
      if (n < g_nivel_bat) {
        const float lim = g_nivel_bat == SIMP_BAT_CRITICA ? SIMP_LIMIAR_BAT_CRITICA : SIMP_LIMIAR_BAT_BAIXA;
        if (v <= lim + h) n = g_nivel_bat;
      }
      g_nivel_bat = n;
    }
#endif
    const bool alarmeOn = alarme.isActive(), bloqOn = bloqueio.isBlocked();
    xSemaphoreGive(g_mutex);
#if CONFIG_STR_MODO_SIM
    publicarEstado(q);
#endif
    static int cont=0;
    if (++cont >= 2000 / PERIODO_MS) {
      cont=0;
      ESP_LOGI(TAG,"Ignicao=%s Painel=%s Alarme=%s Bloqueio=%s",ignicao?"ON":"OFF",painel?"CONFORME":"VIOLADO",alarmeOn?"ON":"OFF",bloqOn?"ON":"OFF");
    }
  }
}

extern "C" void app_main(void) {
  g_mutex = xSemaphoreCreateMutex();
  // entradas com pull-up interno: solto=alto, pressionado=baixo
  gpio_config_t in={};
  in.pin_bit_mask=(1ULL<<18)|(1ULL<<19);
  in.mode=GPIO_MODE_INPUT;
  in.pull_up_en=GPIO_PULLUP_ENABLE;
  in.pull_down_en=GPIO_PULLDOWN_DISABLE;
  in.intr_type=GPIO_INTR_DISABLE;
  ESP_ERROR_CHECK(gpio_config(&in));
  bloqueio.init();
#if CONFIG_STR_MODO_SIM
  ESP_LOGI(TAG,"Modo HIL: entradas vindas do simulador pela UART0");
#else
  ESP_LOGI(TAG,"Modo fisico: botoes e comandos pela UART0");
#endif

  sim_link::iniciar(10); // recepcao: acima de tudo

  xTaskCreate(tarefaComandos,"T4_comando",4096,nullptr,8,nullptr); // T4: maior prioridade da aplicacao

  xTaskCreate(tarefaSensores,"sensores_alarme",4096,nullptr,5,nullptr);
}
