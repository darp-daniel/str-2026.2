// Modelos físicos: escala da bateria no pino, descarga, regeneração, motor e veículo.
#include "mini_teste.hpp"
#include "modelos.hpp"

using namespace str_sim;

TESTE(pino_bate_com_os_limiares_do_relatorio) {
  ModeloBateria b;
  b.setSoc(0.20);
  CHECA_PERTO(b.vPino(), 2.5, 1e-6); // limiar "baixa"
  b.setSoc(0.05);
  CHECA_PERTO(b.vPino(), 1.5, 1e-6); // limiar "crítica"
  b.setSoc(0.90);
  CHECA_PERTO(b.vPino(), 3.3, 1e-9); // satura no topo do ADC
  b.setSoc(0.0);
  CHECA(b.vPino() >= 0.0);
}

TESTE(pino_cai_monotonicamente_com_o_soc) {
  ModeloBateria b;
  double ant = 10;
  for (int i = 100; i >= 0; --i) {
    b.setSoc(i / 100.0);
    CHECA(b.vPino() <= ant + 1e-12);
    ant = b.vPino();
  }
}

TESTE(descarga_reduz_soc_e_tensao_sob_carga) {
  ModeloBateria b;
  b.setSoc(0.3);
  const double v0 = b.vTerminal();
  double I = b.passo(50e3, 1.0); // 50 kW por 1 s
  CHECA(I > 100 && I < 200);
  CHECA(b.soc() < 0.3);
  CHECA(b.vTerminal() < v0); // queda R·I
  CHECA_PERTO(0.3 - b.soc(), I / (60.0 * 3600), 1e-9);
}

TESTE(regeneracao_carrega_e_respeita_limite) {
  ModeloBateria b;
  b.setSoc(0.5);
  double I = b.passo(-200e3, 1.0); // pedido de regeneração enorme
  CHECA(I < 0);
  CHECA_PERTO(I, -b.parametros().I_carga_max_A, 1e-9);
  CHECA(b.soc() > 0.5);
  b.setSoc(1.0);
  CHECA_PERTO(b.passo(-10e3, 1.0), 0.0, 1e-12); // cheia: não aceita carga
}

TESTE(descarga_acelerada_e_dreno) {
  ModeloBateria b;
  b.setSoc(0.5);
  b.setFatorDescarga(10);
  b.setDreno(36); // 36 A
  b.passo(0, 100.0);
  CHECA_PERTO(0.5 - b.soc(), 10 * 36 * 100.0 / (60 * 3600.0), 1e-9);
}

TESTE(motor_torque_constante_e_depois_potencia_constante) {
  Motor m;
  CHECA_PERTO(m.torqueRoda(1.0, 1.0), m.T_max_motor * m.reducao, 1e-9);
  const double w_alta = 100.0; // rad/s na roda
  CHECA_PERTO(m.torqueRoda(1.0, w_alta) * w_alta, m.P_max, 1.0);
  CHECA_PERTO(m.torqueRoda(-0.5, 1.0), -0.5 * m.T_max_motor * m.reducao, 1e-9);
  CHECA(m.potenciaEletrica(100, 10) > 100 * 10);   // perdas na tração
  CHECA(m.potenciaEletrica(-100, 10) > -100 * 10); // perdas na regeneração
}

TESTE(veiculo_acelera_de_0_a_100_em_tempo_plausivel) {
  ModeloVeiculo v;
  Motor m;
  double t = 0;
  while (v.velocidade() < 100 / 3.6 && t < 60) {
    v.passo(m.torqueRoda(1.0, v.wRoda()), 0.01);
    t += 0.01;
  }
  CHECA(t > 4 && t < 15); // carro elétrico compacto
}

TESTE(veiculo_sem_torque_desacelera_e_nao_da_re) {
  ModeloVeiculo v;
  v.setVelocidade(20);
  for (int i = 0; i < 100; ++i) v.passo(0, 0.01);
  CHECA(v.velocidade() < 20);
  for (int i = 0; i < 100000; ++i) v.passo(0, 0.01);
  CHECA(v.velocidade() >= 0 && v.velocidade() < 1e-6);
  v.passo(-1000, 0.01);
  CHECA(v.velocidade() == 0);
}

MINI_MAIN
