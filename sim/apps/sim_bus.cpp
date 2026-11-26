// sim_bus — ponte entre o carro simulado e a ESP32, com verificação de prazos.
//
// Exemplos:
//   sim_bus --cenario cenarios/C2_violacao_painel.txt
//       planta interna + ESP virtual, mais rápido que o tempo real (teste)
//   sim_bus --planta coppelia --cenario cenarios/C2_violacao_painel.txt
//       carro no CoppeliaSim + ESP virtual (teste visual sem a placa)
//   sim_bus --planta coppelia --esp serial:COM5
//       HIL completo: CoppeliaSim + ESP32 física, interativo (Ctrl+C sai)
#include "cenario.hpp"
#include "enlace.hpp"
#include "esp_virtual.hpp"
#include "planta.hpp"
#include "simulacao.hpp"
#include "verificador.hpp"

#include <atomic>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <string>

using namespace str_sim;

static std::atomic<bool> g_parar{false};

static void ajuda() {
  std::puts(
      "uso: sim_bus [opcoes]\n"
      "  --planta interna|coppelia[:porta]  de onde vem o carro (padrao: interna)\n"
      "  --esp virtual|udp:HOST:PORTA|serial:PORTA\n"
      "                                     com quem a ponte conversa (padrao: virtual)\n"
      "  --baud N                           baud da serial (padrao: 115200)\n"
      "  --cenario ARQ                      roteiro de teste (sem ele: modo interativo)\n"
      "  --duracao S                        limite de tempo no modo interativo\n"
      "  --dt S                             passo da ponte (padrao: 0.01; coppelia usa o da cena)\n"
      "  --tempo-real                       forca o ritmo do relogio (automatico fora do modo virtual)\n"
      "  --csv ARQ                          serie temporal de cada passo\n"
      "  --eventos ARQ                      resultado de cada regra (CSV)\n"
      "  --silencioso                       nao imprime eventos ao vivo\n"
      "Falhas injetadas na ESP virtual (para ver o verificador reprovar):\n"
      "  --falha-atraso-comando MS          T4 demora MS a mais\n"
      "  --falha-ignora-painel              violacao do painel nunca e detectada\n"
      "  --falha-sem-bateria                ESP nao calcula o nivel da bateria\n"
      "Saida: 0 = todas as regras OK, 1 = alguma falhou, 2 = erro de execucao.");
}

int main(int argc, char **argv) {
  std::string planta_spec = "interna", esp_spec = "virtual", cen_arq, csv, eventos;
  int baud = 115200;
  double dt = 0.01, duracao = 0;
  bool tempo_real = false, silencioso = false, dt_explicito = false;
  ParametrosEspVirtual pev;

  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    auto valor = [&]() -> std::string {
      if (i + 1 >= argc) {
        std::fprintf(stderr, "faltou valor para %s\n", a.c_str());
        std::exit(2);
      }
      return argv[++i];
    };
    if (a == "--planta") planta_spec = valor();
    else if (a == "--esp") esp_spec = valor();
    else if (a == "--baud") baud = std::stoi(valor());
    else if (a == "--cenario") cen_arq = valor();
    else if (a == "--duracao") duracao = std::stod(valor());
    else if (a == "--dt") { dt = std::stod(valor()); dt_explicito = true; }
    else if (a == "--tempo-real") tempo_real = true;
    else if (a == "--csv") csv = valor();
    else if (a == "--eventos") eventos = valor();
    else if (a == "--silencioso") silencioso = true;
    else if (a == "--falha-atraso-comando") pev.atraso_comando_us = static_cast<uint32_t>(std::stod(valor()) * 1000);
    else if (a == "--falha-ignora-painel") pev.ignora_painel = true;
    else if (a == "--falha-sem-bateria") pev.implementa_bateria = false;
    else if (a == "-h" || a == "--help") { ajuda(); return 0; }
    else { std::fprintf(stderr, "opcao desconhecida: %s\n", a.c_str()); ajuda(); return 2; }
  }

  std::signal(SIGINT, [](int) { g_parar = true; });
  // Sem buffer: lido por um pipe (teste_extenso.py) a saida aparece na hora, nao so no fim.
  std::setvbuf(stdout, nullptr, _IONBF, 0);

  try {
    Cenario cen;
    if (!cen_arq.empty()) cen = carregarCenario(cen_arq);

    EspVirtual esp(pev);
    auto planta = criarPlanta(planta_spec);
    auto enlace = criarEnlace(esp_spec, baud, &esp);

    if (auto *pc = dynamic_cast<PlantaCoppelia *>(planta.get())) {
      if (!dt_explicito) dt = pc->dtCena();
      tempo_real = true; // a cena anda no ritmo do relógio (para o usuário ver e interagir)
    }
    if (esp_spec != "virtual") tempo_real = true; // placa real não espera a simulação

    OpcoesLaco op;
    op.dt_s = dt;
    op.tempo_real = tempo_real;
    op.duracao_max_s = duracao;
    op.csv = csv;
    op.verbose = !silencioso;
    op.parar = &g_parar;

    std::printf("== sim_bus ==\n  planta : %s\n  enlace : %s\n  cenario: %s\n  dt     : %.0f ms, %s\n\n",
                planta->descricao().c_str(), enlace->descricao().c_str(),
                cen_arq.empty() ? "(interativo)" : cen.nome.c_str(), dt * 1000,
                tempo_real ? "tempo real" : "o mais rapido possivel");
    if (cen_arq.empty() && duracao <= 0) std::puts("  Ctrl+C para encerrar.\n");

    Verificador verif;
    auto res = executar(*planta, *enlace, cen_arq.empty() ? nullptr : &cen, verif, op);

    std::puts("\n== Relatorio de prazos ==");
    verif.imprimirRelatorio(std::cout);
    imprimirResumo(res, std::cout);
    if (!eventos.empty()) verif.salvarCsv(eventos);
    if (!csv.empty()) std::printf("  serie temporal: %s\n", csv.c_str());
    std::printf("\n  RESULTADO: %s\n", res.ok() ? "APROVADO" : "REPROVADO");
    return res.ok() ? 0 : 1;
  } catch (const std::exception &e) {
    std::fprintf(stderr, "erro: %s\n", e.what());
    return 2;
  }
}
