#include "simulacao.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <deque>
#include <fstream>
#include <iostream>
#include <thread>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <timeapi.h>
#endif

namespace str_sim {

namespace {

// Resolução de 1 ms no sleep do Windows (o padrão é ~15,6 ms).
struct ResolucaoTimer {
  ResolucaoTimer() {
#ifdef _WIN32
    timeBeginPeriod(1);
#endif
  }
  ~ResolucaoTimer() {
#ifdef _WIN32
    timeEndPeriod(1);
#endif
  }
};

// Espera até 'alvo' com precisão de ~0,1 ms. Só o sleep (mesmo com
// timeBeginPeriod) não basta: no Windows 11 um processo de console sem janela
// visível pode continuar acordando em fatias de 15,6 ms, e metade dos passos de
// 10 ms saía atrasada. Timer de alta resolução até ~0,5 ms antes, depois espera ativa.
class Despertador {
public:
  Despertador() {
#ifdef _WIN32
#ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002
#endif
    timer_ = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
#endif
  }
  ~Despertador() {
#ifdef _WIN32
    if (timer_) CloseHandle(timer_);
#endif
  }
  void esperarAte(std::chrono::steady_clock::time_point alvo) {
    using namespace std::chrono;
    const auto folga = microseconds(500);
    auto falta = alvo - steady_clock::now();
    if (falta > folga) {
#ifdef _WIN32
      if (timer_) {
        LARGE_INTEGER due;
        due.QuadPart = -static_cast<LONGLONG>(duration_cast<microseconds>(falta - folga).count() * 10); // 100 ns, relativo
        if (SetWaitableTimer(timer_, &due, 0, nullptr, nullptr, FALSE)) WaitForSingleObject(timer_, INFINITE);
      } else {
        std::this_thread::sleep_until(alvo - folga);
      }
#else
      std::this_thread::sleep_until(alvo - folga);
#endif
    }
    while (steady_clock::now() < alvo) std::this_thread::yield();
  }

private:
#ifdef _WIN32
  HANDLE timer_ = nullptr;
#endif
};

struct ComandoFila {
  int cmd;
  uint64_t t_emitido_us;
  uint8_t id = 0; // 0 = ainda não transmitido
};

bool ecoAlcancou(uint8_t eco, uint8_t id) {
  return eco != 0 && static_cast<int8_t>(static_cast<uint8_t>(eco - id)) >= 0;
}

} // namespace

ResumoExecucao executar(Planta &planta, Enlace &enlace, const Cenario *cenario, Verificador &verif,
                        const OpcoesLaco &op) {
  ResumoExecucao res;
  ResolucaoTimer timer;
  const uint64_t dt_us = static_cast<uint64_t>(std::llround(op.dt_s * 1e6));
  static const Cenario vazio;
  ExecutorCenario exec(cenario ? *cenario : vazio);
  const double duracao = cenario && cenario->duracao_s > 0 ? cenario->duracao_s : op.duracao_max_s;

  std::ofstream csv;
  if (!op.csv.empty()) {
    csv.open(op.csv);
    csv << "t_s,t_sim_s,seq,ignicao,painel_ok,pedal,v_pino,soc,corrente_A,vel_kmh,torque_Nm,cmd,cmd_id,"
           "rx,bloqueio,alarme,motivo,nivel_bat,ignicao_eco,cmd_eco,rtt_ms,atrasado\n";
  }

  if (op.verbose) {
    enlace.aoTexto = [](const std::string &l) { std::cout << "  [ESP] " << l << "\n"; };
    verif.aoConcluir = [](const ResultadoRegra &r) {
      if (r.situacao == Situacao::NaoAplicavel) return;
      if (r.latencia_ms >= 0)
        std::printf("  %7.2f s  %-12s %-45s %8.1f ms\n", r.t_evento_s + r.latencia_ms / 1000.0,
                    nomeSituacao(r.situacao), r.regra.c_str(), r.latencia_ms);
      else
        std::printf("  %7.2f s  %-12s %-45s %s\n", r.t_evento_s, nomeSituacao(r.situacao), r.regra.c_str(),
                    r.detalhe.c_str());
    };
  }

  std::deque<ComandoFila> fila;
  uint8_t prox_id = 0;
  uint32_t seq = 0;
  EspParaSim ultimo_rx{};
  bool tem_rx = false;
  std::vector<uint64_t> t_envio(1024, 0); // seq -> instante de envio
  uint32_t ultimo_seq_eco = UINT32_MAX;

  Despertador despertador;
  const auto inicio = std::chrono::steady_clock::now();
  uint64_t t_us = 0;

  for (uint64_t k = 0;; ++k) {
    bool atrasado = false;
    if (op.tempo_real) {
      const auto alvo = inicio + std::chrono::microseconds(k * dt_us);
      auto agora = std::chrono::steady_clock::now();
      if (agora < alvo) {
        despertador.esperarAte(alvo);
        agora = std::chrono::steady_clock::now();
      }
      t_us = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(agora - inicio).count());
      if (agora - alvo > std::chrono::microseconds(dt_us / 2)) {
        atrasado = true;
        res.passos_atrasados++;
      }
    } else {
      t_us = k * dt_us;
    }
    const double t_s = t_us / 1e6;
    if (op.parar && op.parar->load()) break;
    if (duracao > 0 && t_s >= duracao) break;

    // 1. respostas da ESP
    double rtt = -1;
    EspParaSim rx{};
    while (enlace.receber(t_us, rx)) {
      ultimo_rx = rx;
      tem_rx = true;
      verif.observarResposta(rx, t_us);
      if (rx.seq_eco != ultimo_seq_eco && rx.seq_eco < seq && seq - rx.seq_eco < t_envio.size()) {
        rtt = (t_us - t_envio[rx.seq_eco % t_envio.size()]) / 1000.0;
        res.rtt_ms.push_back(rtt);
        ultimo_seq_eco = rx.seq_eco;
      }
      while (!fila.empty() && fila.front().id != 0 && ecoAlcancou(rx.comando_id_eco, fila.front().id))
        fila.pop_front();
    }

    // 2. cenário
    for (const auto &a : exec.acoesAte(t_s)) {
      if (a.tipo == TipoAcao::Comando) fila.push_back({static_cast<int>(a.valor), t_us});
      else planta.aplicar(a);
    }

    // 3. planta -> ESP
    EstadoPlanta e = planta.ler();
    if (e.comando_ui != SIMP_CMD_NENHUM) fila.push_back({e.comando_ui, t_us});
    verif.observarPlanta(e, t_us);

    SimParaEsp tx{};
    if (!fila.empty()) {
      auto &c = fila.front();
      if (c.id == 0) {
        if (++prox_id == 0) prox_id = 1; // 0 é reservado para "nenhum"
        c.id = prox_id;
        verif.comandoEmitido(c.cmd, c.id, c.t_emitido_us);
      }
      tx.comando = static_cast<uint8_t>(c.cmd);
      tx.comando_id = c.id;
    } else {
      tx.comando = SIMP_CMD_NENHUM;
      tx.comando_id = prox_id;
    }
    tx.seq = seq;
    tx.t_sim_ms = static_cast<uint32_t>(t_us / 1000);
    tx.ignicao = e.ignicao ? 1 : 0;
    tx.painel_ok = e.painel_ok ? 1 : 0;
    tx.v_bat_pino = static_cast<float>(e.v_pino);
    tx.velocidade = static_cast<float>(e.velocidade);
    t_envio[seq % t_envio.size()] = t_us;
    enlace.enviar(tx, t_us);
    ++seq;

    // 4. atuação + física
    planta.atuar(ultimo_rx, tem_rx);
    planta.status(verif.ultimoEvento());
    planta.passo(op.dt_s);
    verif.tick(t_us);
    res.passos++;

    // 5. registro
    if (csv) {
      char l[400];
      std::snprintf(l, sizeof l,
                    "%.4f,%.4f,%u,%d,%d,%.3f,%.4f,%.4f,%.2f,%.2f,%.1f,%d,%d,%d,%d,%d,%d,%d,%d,%d,%.2f,%d\n", t_s,
                    e.t_sim, tx.seq, tx.ignicao, tx.painel_ok, e.pedal, e.v_pino, e.soc, e.corrente_A,
                    e.velocidade * 3.6, e.torque, tx.comando, tx.comando_id, tem_rx ? 1 : 0, ultimo_rx.bloqueio,
                    ultimo_rx.alarme, ultimo_rx.motivo, ultimo_rx.nivel_bat, ultimo_rx.ignicao_eco,
                    ultimo_rx.comando_id_eco, rtt, atrasado ? 1 : 0);
      csv << l;
    }
  }

  verif.finalizar(t_us);
  res.regras = static_cast<int>(verif.resultados().size());
  res.aprovadas = verif.aprovadas();
  res.falhas = verif.falhas();
  res.enlace = enlace.estatistica();
  return res;
}

void imprimirResumo(const ResumoExecucao &r, std::ostream &o) {
  char buf[300];
  std::snprintf(buf, sizeof buf, "  passos: %llu (atrasados: %llu)   quadros enviados/recebidos: %llu/%llu   "
                                 "erros CRC: %llu   bytes de texto: %llu\n",
                static_cast<unsigned long long>(r.passos), static_cast<unsigned long long>(r.passos_atrasados),
                static_cast<unsigned long long>(r.enlace.quadros_enviados),
                static_cast<unsigned long long>(r.enlace.quadros_recebidos),
                static_cast<unsigned long long>(r.enlace.erros_crc),
                static_cast<unsigned long long>(r.enlace.bytes_texto));
  o << buf;
  if (!r.rtt_ms.empty()) {
    auto v = r.rtt_ms;
    std::sort(v.begin(), v.end());
    double soma = 0;
    for (double x : v) soma += x;
    auto pct = [&](double p) { return v[std::min(v.size() - 1, static_cast<size_t>(p * (v.size() - 1) + 0.5))]; };
    std::snprintf(buf, sizeof buf, "  idade do quadro ecoado na resposta (PC->ESP->PC): media %.1f ms, mediana %.1f, p95 %.1f, max %.1f (n=%zu)\n",
                  soma / v.size(), pct(0.5), pct(0.95), v.back(), v.size());
    o << buf;
  }
}

} // namespace str_sim
