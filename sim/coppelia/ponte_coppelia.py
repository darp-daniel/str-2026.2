"""
ponte_coppelia.py — adaptador entre o sim_bus (C++) e o CoppeliaSim.

    sim_bus  --UDP 127.0.0.1:5010-->  ponte_coppelia.py  --ZeroMQ-->  CoppeliaSim
             (texto chave=valor)                          (Remote API, modo stepping)

A cada passo o sim_bus manda o torque e o estado da ESP; este script chama
str_trocar() do script da cena (cena_carro.lua), avança um passo da física
com sim.step() e devolve ignição/painel/pedal/rotação da roda.

Por que um adaptador em Python? O cliente oficial da ZeroMQ Remote API para
Python instala com um pip, enquanto o cliente C++ exige compilar cppzmq e
jsoncons. O laço de tempo real, o verificador e os modelos continuam em C++.

Uso:
    pip install coppeliasim-zmqremoteapi-client
    python ponte_coppelia.py                 # CoppeliaSim aberto com a cena
    python ponte_coppelia.py --falso         # sem CoppeliaSim (testes)
"""

import argparse
import math
import os
import socket
import sys
import time

AQUI = os.path.dirname(os.path.abspath(__file__))


def ler_kv(texto):
    d = {}
    for par in texto.split(";"):
        if "=" in par:
            k, v = par.split("=", 1)
            d[k] = v
    return d


def num(d, k, padrao=0.0):
    try:
        return float(d.get(k, padrao))
    except ValueError:
        return padrao


def montar_kv(d):
    partes = []
    for k, v in d.items():
        if isinstance(v, bool):
            v = int(v)
        if isinstance(v, float):
            v = f"{v:.6g}"
        partes.append(f"{k}={v}")
    return ";".join(partes)


SAIDA_NUMERICA = ("torque", "bloqueio", "alarme", "nivel", "motivo", "ign_eco", "tem_rx",
                  "v_pino", "soc", "corrente", "f_ign", "f_pai", "f_ped")


def saida_para_cena(m):
    s = {k: num(m, k) for k in SAIDA_NUMERICA if k in m}
    for k in ("bloqueio", "alarme", "nivel", "motivo", "ign_eco", "tem_rx", "f_ign", "f_pai"):
        if k in s:
            s[k] = int(s[k])
    s["status"] = m.get("status", "")
    return s


# ---------------------------------------------------------------- simuladores
class CenaFalsa:
    """Imita cena_carro.lua + física 1D. Serve para testar a cadeia sem o CoppeliaSim."""

    def __init__(self, dt):
        self.dt = dt
        self.t = 0.0
        self.ign, self.pai, self.ped, self.vel = 0, 1, 0.0, 0.0
        self.passos = 0
        self.torque_com_bloqueio_max = 0.0
        self.viu_bloqueio = False

    def descricao(self):
        return "cena FALSA (sem CoppeliaSim)"

    def iniciar(self):
        self.t = 0.0

    def trocar(self, s):
        if s.get("f_ign", -1) >= 0:
            self.ign = 1 if s["f_ign"] > 0.5 else 0
        if s.get("f_pai", -1) >= 0:
            self.pai = 1 if s["f_pai"] > 0.5 else 0
        if s.get("f_ped", -9) > -2:
            self.ped = s["f_ped"]
        T = s.get("torque", 0.0)
        if s.get("bloqueio", 0) == 1:
            self.viu_bloqueio = True
            self.torque_com_bloqueio_max = max(self.torque_com_bloqueio_max, abs(T))
        F = T / 0.31 - 0.5 * 1.2 * 0.3 * 2.2 * self.vel ** 2 - (0.012 * 1500 * 9.81 if self.vel > 0 else 0)
        self.vel = max(0.0, self.vel + F / 1500 * self.dt)
        self.t += self.dt
        self.passos += 1
        return {"ign": self.ign, "pai": self.pai, "ped": self.ped, "w": self.vel / 0.31,
                "vel": self.vel, "cmd": 0, "desc": 0, "t": self.t}

    def parar(self):
        pass

    def resumo(self):
        return (f"passos={self.passos} viu_bloqueio={int(self.viu_bloqueio)} "
                f"torque_com_bloqueio_max={self.torque_com_bloqueio_max:g}")


class CenaCoppelia:
    def __init__(self, host, porta_zmq, dt):
        try:
            from coppeliasim_zmqremoteapi_client import RemoteAPIClient
        except ImportError:
            sys.exit("falta o cliente: pip install coppeliasim-zmqremoteapi-client")
        print(f"conectando ao CoppeliaSim em {host}:{porta_zmq} ...")
        self.cliente = RemoteAPIClient(host=host, port=porta_zmq)
        try:
            self.sim = self.cliente.require("sim")  # cliente 2.x
        except AttributeError:
            self.sim = self.cliente.getObject("sim")  # cliente 1.x
        self.dt_pedido = dt
        self.script = self._achar_ou_criar_script()
        self.rodando = False

    def descricao(self):
        return "CoppeliaSim"

    # -- localizar o script da cena (a API mudou entre 4.6, 4.7 e 4.8+)
    def _achar_script(self):
        sim = self.sim
        try:
            h = sim.getObject("/CarroSTR")
        except Exception:
            return None
        for tentativa in (
            lambda: sim.getScript(sim.scripttype_customization, h),
            lambda: sim.getObject("/CarroSTR/Script"),
            lambda: sim.getScriptHandle(sim.scripttype_customizationscript, "CarroSTR"),
        ):
            try:
                s = tentativa()
                if s is not None and s != -1:
                    return s
            except Exception:
                pass
        return None

    def _achar_ou_criar_script(self):
        s = self._achar_script()
        if s is not None:
            print("script da cena encontrado em /CarroSTR")
            return s
        print("/CarroSTR nao encontrado: criando o carro e o script na cena aberta ...")
        sim = self.sim
        codigo = open(os.path.join(AQUI, "cena_carro.lua"), encoding="utf-8").read()
        dummy = sim.createDummy(0.1)
        sim.setObjectAlias(dummy, "CarroSTR")
        erros = []
        try:  # CoppeliaSim 4.8+: scripts são objetos da cena
            s = sim.createScript(sim.scripttype_customization, codigo, 0, "lua")
            sim.setObjectParent(s, dummy, True)
            try:
                sim.initScript(s)
            except Exception:
                pass
            return s
        except Exception as e:
            erros.append(f"createScript: {e}")
        try:  # 4.6 / 4.7
            s = sim.addScript(sim.scripttype_customizationscript)
            sim.setScriptStringParam(s, sim.scriptstringparam_text, codigo)
            sim.associateScriptWithObject(s, dummy)
            return s
        except Exception as e:
            erros.append(f"addScript: {e}")
        sys.exit("nao consegui criar o script automaticamente (" + "; ".join(erros) + ").\n"
                 "Siga o passo a passo no topo de cena_carro.lua e rode de novo.")

    def iniciar(self):
        sim = self.sim
        if sim.getSimulationState() != sim.simulation_stopped:
            sim.stopSimulation()
            while sim.getSimulationState() != sim.simulation_stopped:
                time.sleep(0.05)
        if self.dt_pedido:
            try:
                sim.setFloatParam(sim.floatparam_simulation_time_step, self.dt_pedido)
            except Exception as e:
                print(f"aviso: nao consegui mudar o passo da cena ({e})")
        try:
            sim.setStepping(True)
        except AttributeError:
            self.cliente.setStepping(True)
        sim.startSimulation()
        self.rodando = True
        self.dt = sim.getSimulationTimeStep()

    def trocar(self, s):
        sim = self.sim
        r = sim.callScriptFunction("str_trocar", self.script, s)
        try:
            sim.step()
        except AttributeError:
            self.cliente.step()
        r = dict(r or {})
        r["t"] = sim.getSimulationTime()
        return r

    def parar(self):
        if self.rodando:
            self.sim.stopSimulation()
            self.rodando = False

    def resumo(self):
        return ""


# ---------------------------------------------------------------- laço
def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--porta", type=int, default=5010, help="porta UDP onde o sim_bus fala (padrao 5010)")
    ap.add_argument("--zmq-host", default="localhost")
    ap.add_argument("--zmq-porta", type=int, default=23000)
    ap.add_argument("--dt", type=float, default=0.01, help="passo da fisica em s (padrao 0.01; 0 = o da cena)")
    ap.add_argument("--falso", action="store_true", help="nao usa o CoppeliaSim (para testes)")
    ap.add_argument("--sair-no-fim", action="store_true", help="encerra quando o sim_bus terminar")
    a = ap.parse_args()

    cena = CenaFalsa(a.dt or 0.01) if a.falso else CenaCoppelia(a.zmq_host, a.zmq_porta, a.dt)

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind(("127.0.0.1", a.porta))
    print(f"ponte_coppelia: {cena.descricao()}, esperando o sim_bus em UDP {a.porta} (Ctrl+C sai)", flush=True)

    ativo = False
    try:
        while True:
            dados, origem = sock.recvfrom(4096)
            m = ler_kv(dados.decode("utf-8", "replace"))
            op = m.get("op")
            try:
                if op == "ola":
                    cena.iniciar()
                    ativo = True
                    resp = {"ok": 1, "dt": cena.dt, "t": 0.0}
                    print(f"sim_bus conectado de {origem}; passo da cena = {cena.dt * 1000:.0f} ms", flush=True)
                elif op == "passo":
                    if not ativo:
                        cena.iniciar()
                        ativo = True
                    resp = cena.trocar(saida_para_cena(m))
                    resp["ok"] = 1
                elif op == "fim":
                    cena.parar()
                    ativo = False
                    print("sim_bus encerrou. " + cena.resumo(), flush=True)
                    if a.sair_no_fim:
                        return 0
                    continue
                else:
                    resp = {"erro": f"op desconhecida: {op}"}
            except Exception as e:  # erro do CoppeliaSim vai para o sim_bus
                resp = {"erro": str(e).replace(";", ",").replace("=", ":")}
            sock.sendto(montar_kv(resp).encode(), origem)
    except KeyboardInterrupt:
        pass
    finally:
        cena.parar()
        sock.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
