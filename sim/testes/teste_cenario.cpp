// Leitura e execução de cenários.
#include "cenario.hpp"
#include "mini_teste.hpp"
#include "sim_protocol.h"

#include <stdexcept>

using namespace str_sim;

TESTE(le_cenario_valido_e_ordena) {
  auto c = lerCenario("# comentario\nduracao 12\n5.0 comando bloquear\n0 ignicao 1 # fim\n\n1.5 pedal -0.25\n");
  CHECA(c.duracao_s == 12);
  CHECA(c.acoes.size() == 3);
  CHECA(c.acoes[0].tipo == TipoAcao::Ignicao && c.acoes[0].valor == 1);
  CHECA(c.acoes[1].tipo == TipoAcao::Pedal && c.acoes[1].valor == -0.25);
  CHECA(c.acoes[2].tipo == TipoAcao::Comando && c.acoes[2].valor == SIMP_CMD_BLOQUEAR);
}

static bool falha(const char *txt) {
  try {
    lerCenario(txt);
  } catch (const std::runtime_error &) {
    return true;
  }
  return false;
}

TESTE(recusa_entradas_invalidas) {
  CHECA(falha("1.0 voar 1\n"));
  CHECA(falha("1.0 pedal 2\n"));
  CHECA(falha("abc pedal 0.1\n"));
  CHECA(falha("1.0 comando explodir\n"));
  CHECA(falha("1.0 pedal\n"));
  CHECA(falha("-1 pedal 0\n"));
  CHECA(falha("duracao -3\n"));
}

TESTE(mensagem_de_erro_tem_numero_da_linha) {
  try {
    lerCenario("0 ignicao 1\n\n2 pedal x\n", "teste.txt");
    CHECA(false);
  } catch (const std::runtime_error &e) {
    CHECA(std::string(e.what()).find("teste.txt:3") != std::string::npos);
  }
}

TESTE(executor_entrega_em_ordem_e_uma_vez) {
  auto c = lerCenario("duracao 3\n0 ignicao 1\n1 pedal 0.5\n1 painel 0\n2 pedal 0\n");
  ExecutorCenario ex(c);
  CHECA(ex.acoesAte(0.0).size() == 1);
  CHECA(ex.acoesAte(0.5).empty());
  auto a = ex.acoesAte(1.0);
  CHECA(a.size() == 2 && a[0].tipo == TipoAcao::Pedal && a[1].tipo == TipoAcao::Painel);
  CHECA(ex.acoesAte(10).size() == 1);
  CHECA(ex.acoesAte(10).empty());
  CHECA(!ex.terminou(2.9) && ex.terminou(3.0));
}

TESTE(cenarios_do_repositorio_sao_validos) {
  const char *arquivos[] = {"C1_ignicao.txt",         "C2_violacao_painel.txt", "C3_bateria.txt",
                            "C4_comando_usuario.txt", "C5_carga_maxima.txt",    "C6_rajada_comandos.txt"};
  for (const char *a : arquivos) {
    try {
      auto c = carregarCenario(std::string(STR_DIR_CENARIOS) + "/" + a);
      CHECA(c.duracao_s > 0);
      CHECA(!c.acoes.empty());
    } catch (const std::exception &e) {
      std::printf("    %s\n", e.what());
      CHECA(false);
    }
  }
}

MINI_MAIN
