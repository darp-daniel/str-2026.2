// esp_virtual — a ESP virtual num processo separado, escutando UDP.
//
// Serve para testar a ponte com um enlace de verdade (sockets) sem a placa:
//   terminal 1:  esp_virtual --porta 5005
//   terminal 2:  sim_bus --esp udp:127.0.0.1:5005 --cenario cenarios/C4_comando_usuario.txt
#include "enlace.hpp"
#include "esp_virtual.hpp"
#include "sim_protocol.h"

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <timeapi.h>
#endif

using namespace str_sim;

static std::atomic<bool> g_parar{false};

int main(int argc, char **argv) {
  uint16_t porta = 5005;
  double max_s = 0;
  ParametrosEspVirtual p;
  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    if (a == "--porta" && i + 1 < argc) porta = static_cast<uint16_t>(std::stoi(argv[++i]));
    else if (a == "--duracao" && i + 1 < argc) max_s = std::stod(argv[++i]);
    else if (a == "--falha-atraso-comando" && i + 1 < argc) p.atraso_comando_us = static_cast<uint32_t>(std::stod(argv[++i]) * 1000);
    else if (a == "--falha-ignora-painel") p.ignora_painel = true;
    else {
      std::puts("uso: esp_virtual [--porta 5005] [--duracao S] [--falha-atraso-comando MS] [--falha-ignora-painel]");
      return a == "-h" || a == "--help" ? 0 : 2;
    }
  }
  std::signal(SIGINT, [](int) { g_parar = true; });
#ifdef _WIN32
  timeBeginPeriod(1); // sleep de ~1 ms em vez de ~15,6 ms
#endif

  try {
    SocketUdp sock(porta);
    EspVirtual esp(p);
    SimpDecodificador dec;
    simp_decodificador_init(&dec);
    std::printf("esp_virtual escutando UDP %u (Ctrl+C sai)\n", porta);
    std::fflush(stdout);

    const auto t0 = std::chrono::steady_clock::now();
    uint8_t buf[1024];
    uint64_t rx = 0, tx = 0;
    while (!g_parar) {
      const uint64_t t = static_cast<uint64_t>(
          std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - t0).count());
      if (max_s > 0 && t / 1e6 > max_s) break;

      size_t n;
      while ((n = sock.receber(buf, sizeof buf, true)) > 0) {
        for (size_t i = 0; i < n; ++i) {
          if (simp_alimentar(&dec, buf[i]) == SIMP_TIPO_SIM_PARA_ESP && dec.len == sizeof(SimParaEsp)) {
            SimParaEsp m;
            std::memcpy(&m, dec.payload, sizeof m);
            esp.receber(m, t);
            ++rx;
          }
        }
      }
      esp.servico(t);
      EspParaSim r;
      while (esp.proximaResposta(t, r)) {
        uint8_t q[SIMP_MAX_QUADRO];
        size_t qn = simp_montar(SIMP_TIPO_ESP_PARA_SIM, &r, sizeof r, q);
        if (sock.enviar(q, qn)) ++tx;
      }
      std::this_thread::sleep_for(std::chrono::microseconds(500));
    }
    std::printf("esp_virtual: %llu quadros recebidos, %llu respostas enviadas, %u erros CRC\n",
                static_cast<unsigned long long>(rx), static_cast<unsigned long long>(tx), dec.erros_crc);
    return 0;
  } catch (const std::exception &e) {
    std::fprintf(stderr, "erro: %s\n", e.what());
    return 2;
  }
}
