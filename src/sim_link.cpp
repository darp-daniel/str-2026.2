#include "sim_link.hpp"

#include "driver/uart.h"
#include "esp_log.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#if __has_include("driver/uart_vfs.h")
#include "driver/uart_vfs.h" // ESP-IDF 5.3+
#define STR_TEM_UART_VFS 1
#endif

#include <cstring>

namespace sim_link {
namespace {

constexpr uart_port_t PORTA = UART_NUM_0;
constexpr int BAUD = 115200;
const char *TAG = "SIM_LINK";

SimParaEsp g_ultimo{};
bool g_tem = false;
portMUX_TYPE g_mux = portMUX_INITIALIZER_UNLOCKED;
QueueHandle_t g_cmds = nullptr;
SemaphoreHandle_t g_tx = nullptr;
uint8_t g_ultimo_id = 0;

void tarefaRx(void *) {
  SimpDecodificador dec;
  simp_decodificador_init(&dec);
  uint8_t buf[64];
  for (;;) {
    int n = uart_read_bytes(PORTA, buf, sizeof buf, pdMS_TO_TICKS(20));
    for (int i = 0; i < n; ++i) {
      if (simp_alimentar(&dec, buf[i]) != SIMP_TIPO_SIM_PARA_ESP || dec.len != sizeof(SimParaEsp)) continue;
      SimParaEsp m;
      std::memcpy(&m, dec.payload, sizeof m);
      taskENTER_CRITICAL(&g_mux);
      g_ultimo = m;
      g_tem = true;
      taskEXIT_CRITICAL(&g_mux);
      // A ponte repete o mesmo comando até ver o eco; só o primeiro vai para T4.
      if (m.comando != SIMP_CMD_NENHUM && m.comando_id != g_ultimo_id) {
        g_ultimo_id = m.comando_id;
        Comando c{m.comando, m.comando_id};
        if (xQueueSend(g_cmds, &c, 0) != pdTRUE) ESP_LOGW(TAG, "fila de comandos cheia");
      }
    }
  }
}

} // namespace

void iniciar(UBaseType_t prioridade_rx) {
  uart_config_t cfg = {};
  cfg.baud_rate = BAUD;
  cfg.data_bits = UART_DATA_8_BITS;
  cfg.parity = UART_PARITY_DISABLE;
  cfg.stop_bits = UART_STOP_BITS_1;
  cfg.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
  cfg.source_clk = UART_SCLK_DEFAULT;
  ESP_ERROR_CHECK(uart_driver_install(PORTA, 1024, 1024, 0, nullptr, 0));
  ESP_ERROR_CHECK(uart_param_config(PORTA, &cfg));
#ifdef STR_TEM_UART_VFS
  // logs (printf/ESP_LOGx) passam pelo mesmo driver: um log não corta um quadro no meio
  uart_vfs_dev_use_driver(PORTA);
#endif
  g_cmds = xQueueCreate(16, sizeof(Comando));
  g_tx = xSemaphoreCreateMutex();
  xTaskCreate(tarefaRx, "sim_rx", 4096, nullptr, prioridade_rx, nullptr);
}

bool ultimoQuadro(SimParaEsp &out) {
  taskENTER_CRITICAL(&g_mux);
  bool tem = g_tem;
  out = g_ultimo;
  taskEXIT_CRITICAL(&g_mux);
  return tem;
}

QueueHandle_t filaComandos() { return g_cmds; }

void enviar(const EspParaSim &m) {
  uint8_t q[SIMP_MAX_QUADRO];
  size_t n = simp_montar(SIMP_TIPO_ESP_PARA_SIM, &m, sizeof m, q);
  xSemaphoreTake(g_tx, portMAX_DELAY);
  uart_write_bytes(PORTA, q, n);
  xSemaphoreGive(g_tx);
}

} // namespace sim_link
