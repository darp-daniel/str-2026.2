#include "cenario.hpp"

#include "sim_protocol.h"

#include <algorithm>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace str_sim {

const char *nomeAcao(TipoAcao t) {
  switch (t) {
  case TipoAcao::Ignicao: return "ignicao";
  case TipoAcao::Painel: return "painel";
  case TipoAcao::Pedal: return "pedal";
  case TipoAcao::Soc: return "soc";
  case TipoAcao::Descarga: return "descarga";
  case TipoAcao::Dreno: return "dreno";
  case TipoAcao::Comando: return "comando";
  }
  return "?";
}

const char *nomeComando(int cmd) {
  switch (cmd) {
  case SIMP_CMD_BLOQUEAR: return "bloquear";
  case SIMP_CMD_DESBLOQUEAR: return "desbloquear";
  case SIMP_CMD_ATUALIZAR: return "atualizar";
  default: return "nenhum";
  }
}

static std::runtime_error erro(const std::string &nome, int linha, const std::string &msg) {
  return std::runtime_error(nome + ":" + std::to_string(linha) + ": " + msg);
}

Cenario lerCenario(const std::string &texto, const std::string &nome) {
  Cenario c;
  c.nome = nome;
  std::istringstream in(texto);
  std::string linha;
  int n = 0;
  while (std::getline(in, linha)) {
    ++n;
    auto hash = linha.find('#');
    if (hash != std::string::npos) linha.erase(hash);
    std::istringstream ls(linha);
    std::string a;
    if (!(ls >> a)) continue; // linha vazia

    if (a == "duracao") {
      if (!(ls >> c.duracao_s) || c.duracao_s <= 0) throw erro(nome, n, "duracao invalida");
      continue;
    }

    AcaoCenario ac;
    ac.linha = n;
    try {
      size_t usados = 0;
      ac.t_s = std::stod(a, &usados);
      if (usados != a.size()) throw std::invalid_argument("t");
    } catch (...) {
      throw erro(nome, n, "esperado '<tempo> <acao> <valor>' ou 'duracao <s>', veio '" + a + "'");
    }
    if (ac.t_s < 0) throw erro(nome, n, "tempo negativo");

    std::string acao, valor;
    if (!(ls >> acao >> valor)) throw erro(nome, n, "faltou acao ou valor");

    auto numero = [&](double lo, double hi) {
      try {
        size_t usados = 0;
        double v = std::stod(valor, &usados);
        if (usados != valor.size() || v < lo || v > hi) throw std::invalid_argument("v");
        return v;
      } catch (...) {
        throw erro(nome, n, "valor invalido para '" + acao + "': " + valor);
      }
    };

    if (acao == "ignicao") { ac.tipo = TipoAcao::Ignicao; ac.valor = numero(0, 1); }
    else if (acao == "painel") { ac.tipo = TipoAcao::Painel; ac.valor = numero(0, 1); }
    else if (acao == "pedal") { ac.tipo = TipoAcao::Pedal; ac.valor = numero(-1, 1); }
    else if (acao == "soc") { ac.tipo = TipoAcao::Soc; ac.valor = numero(0, 1); }
    else if (acao == "descarga") { ac.tipo = TipoAcao::Descarga; ac.valor = numero(0, 10000); }
    else if (acao == "dreno") { ac.tipo = TipoAcao::Dreno; ac.valor = numero(0, 5000); }
    else if (acao == "comando") {
      ac.tipo = TipoAcao::Comando;
      if (valor == "bloquear") ac.valor = SIMP_CMD_BLOQUEAR;
      else if (valor == "desbloquear") ac.valor = SIMP_CMD_DESBLOQUEAR;
      else if (valor == "atualizar") ac.valor = SIMP_CMD_ATUALIZAR;
      else throw erro(nome, n, "comando desconhecido: " + valor);
    } else {
      throw erro(nome, n, "acao desconhecida: " + acao);
    }
    c.acoes.push_back(ac);
  }
  std::stable_sort(c.acoes.begin(), c.acoes.end(),
                   [](const AcaoCenario &x, const AcaoCenario &y) { return x.t_s < y.t_s; });
  return c;
}

Cenario carregarCenario(const std::string &caminho) {
  std::ifstream f(caminho);
  if (!f) throw std::runtime_error("nao consegui abrir o cenario: " + caminho);
  std::stringstream ss;
  ss << f.rdbuf();
  std::string nome = caminho;
  auto barra = nome.find_last_of("/\\");
  if (barra != std::string::npos) nome = nome.substr(barra + 1);
  return lerCenario(ss.str(), nome);
}

std::vector<AcaoCenario> ExecutorCenario::acoesAte(double t_s) {
  std::vector<AcaoCenario> r;
  // pequena folga para erros de arredondamento de t = k·dt
  while (prox_ < c_.acoes.size() && c_.acoes[prox_].t_s <= t_s + 1e-9) r.push_back(c_.acoes[prox_++]);
  return r;
}

} // namespace str_sim
