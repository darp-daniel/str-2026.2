#include "planta.hpp"

#include <cstdio>
#include <map>
#include <sstream>
#include <stdexcept>

namespace str_sim {

// ======================================================== Planta (base)
void Planta::aplicar(const AcaoCenario &a) {
  switch (a.tipo) {
  case TipoAcao::Ignicao: ignicao_ = a.valor > 0.5; break;
  case TipoAcao::Painel: painel_ok_ = a.valor > 0.5; break;
  case TipoAcao::Pedal: pedal_ = a.valor; break;
  case TipoAcao::Soc: bat_.setSoc(a.valor); break;
  case TipoAcao::Descarga: bat_.setFatorDescarga(a.valor); break;
  case TipoAcao::Dreno: bat_.setDreno(a.valor); break;
  case TipoAcao::Comando: break; // tratado pelo laço (vai para a ESP)
  }
}

EstadoPlanta Planta::ler() {
  EstadoPlanta e;
  e.ignicao = ignicao_;
  e.painel_ok = painel_ok_;
  e.pedal = pedal_;
  e.v_pino = bat_.vPino();
  e.soc = bat_.soc();
  e.corrente_A = bat_.corrente();
  e.velocidade = velocidade_;
  e.torque = torque_;
  e.t_sim = t_sim_;
  e.comando_ui = comando_ui_;
  comando_ui_ = SIMP_CMD_NENHUM;
  return e;
}

void Planta::atuar(const EspParaSim &rx, bool tem_rx) {
  rx_ = rx;
  tem_rx_ = tem_rx;
  bloqueio_ = tem_rx && rx.bloqueio;
}

double Planta::torqueComandado(double w_roda) const {
  // Sem ignição ou com bloqueio ativo, o motor não entrega torque (relé de corte).
  if (!ignicao_ || bloqueio_) return 0.0;
  return motor_.torqueRoda(pedal_, w_roda);
}

// ======================================================== PlantaInterna
void PlantaInterna::passo(double dt) {
  const double w = veic_.wRoda();
  double T = torqueComandado(w);
  if (veic_.velocidade() <= 1e-3 && T < 0) T = 0; // sem regeneração parado
  bat_.passo(motor_.potenciaEletrica(T, w), dt);
  veic_.passo(T, dt);
  torque_ = T;
  velocidade_ = veic_.velocidade();
  t_sim_ += dt;
}

// ======================================================== PlantaCoppelia
static std::map<std::string, std::string> parseKv(const std::string &s) {
  std::map<std::string, std::string> m;
  std::istringstream in(s);
  std::string par;
  while (std::getline(in, par, ';')) {
    auto eq = par.find('=');
    if (eq != std::string::npos) m[par.substr(0, eq)] = par.substr(eq + 1);
  }
  return m;
}

static double num(const std::map<std::string, std::string> &m, const char *k, double padrao) {
  auto it = m.find(k);
  if (it == m.end()) return padrao;
  try {
    return std::stod(it->second);
  } catch (...) {
    return padrao;
  }
}

PlantaCoppelia::PlantaCoppelia(uint16_t porta, int timeout_ms) : sock_(0), timeout_ms_(timeout_ms) {
  sock_.definirDestino("127.0.0.1", porta);
  auto r = parseKv(trocar("op=ola"));
  dt_cena_ = num(r, "dt", 0.01);
  t_sim_ = num(r, "t", 0);
}

PlantaCoppelia::~PlantaCoppelia() {
  try {
    const std::string m = "op=fim";
    sock_.enviar(reinterpret_cast<const uint8_t *>(m.data()), m.size());
  } catch (...) {
  }
}

std::string PlantaCoppelia::trocar(const std::string &msg) {
  uint8_t buf[2048];
  while (sock_.receber(buf, sizeof buf) > 0) {} // descarta respostas velhas
  for (int tentativa = 0; tentativa < 3; ++tentativa) {
    sock_.enviar(reinterpret_cast<const uint8_t *>(msg.data()), msg.size());
    size_t n = sock_.receberComTimeout(buf, sizeof buf - 1, timeout_ms_);
    if (n > 0) {
      std::string r(reinterpret_cast<char *>(buf), n);
      auto kv = parseKv(r);
      if (kv.count("erro")) throw std::runtime_error("ponte_coppelia.py: " + kv["erro"]);
      return r;
    }
  }
  throw std::runtime_error("sem resposta de ponte_coppelia.py na porta UDP (ele esta rodando? o CoppeliaSim esta aberto?)");
}

void PlantaCoppelia::aplicar(const AcaoCenario &a) {
  Planta::aplicar(a);
  // Ações do cenário também mexem na interface da cena (checkbox/slider).
  if (a.tipo == TipoAcao::Ignicao) forcar_ign_ = ignicao_ ? 1 : 0;
  if (a.tipo == TipoAcao::Painel) forcar_painel_ = painel_ok_ ? 1 : 0;
  if (a.tipo == TipoAcao::Pedal) forcar_pedal_ = pedal_;
}

void PlantaCoppelia::passo(double) {
  // w da roda é o do passo anterior (o CoppeliaSim avança depois de receber o torque)
  const double w = velocidade_ / 0.31;
  double T = torqueComandado(w);
  if (velocidade_ <= 1e-3 && T < 0) T = 0;

  std::string st = status_;
  for (char &c : st)
    if (c == ';' || c == '=') c = ',';

  char buf[1024];
  std::snprintf(buf, sizeof buf,
                "op=passo;torque=%.3f;bloqueio=%d;alarme=%d;nivel=%d;motivo=%d;ign_eco=%d;tem_rx=%d;"
                "v_pino=%.3f;soc=%.4f;corrente=%.1f;f_ign=%d;f_pai=%d;f_ped=%.3f;status=%s",
                T, bloqueio_ ? 1 : 0, rx_.alarme, rx_.nivel_bat, rx_.motivo, rx_.ignicao_eco, tem_rx_ ? 1 : 0,
                bat_.vPino(), bat_.soc(), bat_.corrente(), forcar_ign_, forcar_painel_, forcar_pedal_, st.c_str());
  forcar_ign_ = forcar_painel_ = -1;
  forcar_pedal_ = -9;

  auto r = parseKv(trocar(buf));
  const double w_real = num(r, "w", 0);
  // Estado da interface da cena (o usuário pode clicar nela durante o teste)
  ignicao_ = num(r, "ign", ignicao_) > 0.5;
  painel_ok_ = num(r, "pai", painel_ok_) > 0.5;
  pedal_ = num(r, "ped", pedal_);
  velocidade_ = num(r, "vel", velocidade_);
  t_sim_ = num(r, "t", t_sim_);
  int cmd = static_cast<int>(num(r, "cmd", 0));
  if (cmd != SIMP_CMD_NENHUM) comando_ui_ = cmd;
  bool desc = num(r, "desc", 0) > 0.5;
  if (desc != desc_ui_) { // botão "descarga rápida" da interface
    desc_ui_ = desc;
    bat_.setFatorDescarga(desc ? 40.0 : 1.0);
    bat_.setDreno(desc ? 30.0 : 0.0);
  }
  // Energia: usa a rotação medida na cena
  bat_.passo(motor_.potenciaEletrica(T, w_real), dtCena());
  torque_ = T;
}

std::unique_ptr<Planta> criarPlanta(const std::string &tipo) {
  if (tipo == "interna") return std::make_unique<PlantaInterna>();
  if (tipo == "coppelia") return std::make_unique<PlantaCoppelia>();
  if (tipo.rfind("coppelia:", 0) == 0)
    return std::make_unique<PlantaCoppelia>(static_cast<uint16_t>(std::stoi(tipo.substr(9))));
  throw std::runtime_error("planta desconhecida: " + tipo + " (use interna ou coppelia[:porta])");
}

} // namespace str_sim
