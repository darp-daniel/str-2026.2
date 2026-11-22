"""
diagnostico_cena.py — descobre por que o carro não anda no CoppeliaSim.

Com o CoppeliaSim aberto na cena do carro (e SEM o ponte_coppelia.py rodando):

    python sim/coppelia/diagnostico_cena.py

1. Lista os objetos do carro: tipo, dinâmico/estático, respondable, massa,
   modo das juntas e posição.
2. Roda 3 s de simulação mandando torque direto para as rodas (ignição
   ligada, sem ESP, sem bloqueio) e mede o que aconteceu.
3. Diz em qual elo está o problema.
"""
import math
import sys
import time

try:
    from coppeliasim_zmqremoteapi_client import RemoteAPIClient
except ImportError:
    sys.exit("falta o cliente: pip install coppeliasim-zmqremoteapi-client")


def tentar(f, padrao=None):
    try:
        return f()
    except Exception:
        return padrao


def main():
    cliente = RemoteAPIClient()
    sim = tentar(lambda: cliente.require("sim")) or cliente.getObject("sim")
    v = tentar(lambda: sim.getInt32Param(sim.intparam_program_full_version), 0)
    print(f"CoppeliaSim {v // 1000000}.{v // 10000 % 100}.{v // 100 % 100} rev {v % 100}")
    print(f"motor de fisica: {tentar(lambda: sim.getInt32Param(sim.intparam_dynamic_engine), '?')} "
          "(0=Bullet, 1=ODE, 2=Vortex, 3=Newton, 4=MuJoCo)")

    if sim.getSimulationState() != sim.simulation_stopped:
        sim.stopSimulation()
        while sim.getSimulationState() != sim.simulation_stopped:
            time.sleep(0.05)

    # ---------------------------------------------------------- 1. estrutura
    chassi = tentar(lambda: sim.getObject("/CarroEV"))
    if chassi is None:
        sys.exit("\n/CarroEV NAO existe: o script do carro nao rodou ate o fim.\n"
                 "Abra o script de /CarroSTR, veja se ha erro (vermelho) na janela de status do\n"
                 "CoppeliaSim e me mande a mensagem. Ou rode: python sim/coppelia/ponte_coppelia.py --recriar")

    shape_t = tentar(lambda: sim.sceneobject_shape, None) or tentar(lambda: sim.object_shape_type, 0)
    joint_t = tentar(lambda: sim.sceneobject_joint, None) or tentar(lambda: sim.object_joint_type, 1)
    estatica_chassi = tentar(lambda: sim.getObjectInt32Param(chassi, sim.shapeintparam_static))
    pista = tentar(lambda: sim.getObject("/Pista"))
    print("\nobjetos do carro (e a pista):")
    for h in list(sim.getObjectsInTree(chassi)) + ([pista] if pista is not None else []):
        nome = tentar(lambda: sim.getObjectAlias(h), str(h))
        tipo = sim.getObjectType(h)
        p = sim.getObjectPosition(h, sim.handle_world)
        pos = f"pos=({p[0]:6.2f},{p[1]:6.2f},{p[2]:5.2f})"
        if tipo == shape_t:
            est = tentar(lambda: sim.getObjectInt32Param(h, sim.shapeintparam_static))
            resp = tentar(lambda: sim.getObjectInt32Param(h, sim.shapeintparam_respondable))
            massa = tentar(lambda: sim.getShapeMass(h))
            atr = tentar(lambda: sim.getEngineFloatParam(sim.bullet_body_friction, h))
            atr = f"{atr:.2f}" if isinstance(atr, (int, float)) else "?"
            msk = tentar(lambda: sim.getObjectInt32Param(h, sim.shapeintparam_respondable_mask))
            atr += f" mascara={hex(msk) if isinstance(msk, int) else '?'}"
            print(f"  forma  {nome:14s} estatica={est} respondable={resp} massa={massa if massa is None else round(massa)} "
                  f"atrito(bullet)={atr} {pos}")
        elif tipo == joint_t:
            modo = tentar(lambda: sim.getJointMode(h))
            ctrl = tentar(lambda: sim.getObjectInt32Param(h, sim.jointintparam_dynctrlmode))
            print(f"  junta  {nome:14s} modo={modo} dynctrl={ctrl} {pos}")
        else:
            print(f"  outro  {nome:14s} tipo={tipo} {pos}")

    script = None
    dummy = tentar(lambda: sim.getObject("/CarroSTR"))
    if dummy is not None:
        script = (tentar(lambda: sim.getScript(sim.scripttype_customization, dummy))
                  or tentar(lambda: sim.getObject("/CarroSTR/Script")))
    if script is None:
        sys.exit("\nnao achei o script de /CarroSTR")

    te = sim.getObject("/CarroEV/junta_TE")
    td = sim.getObject("/CarroEV/junta_TD")

    # ---------------------------------------------------------- 2. teste dinâmico
    # Roteiro (12 s): acelera 3 s, rola 2 s sem torque (motorista ligado),
    # acelera 2 s, rola 2 s sem torque (motorista desligado), acelera 3 s a 1125 N.m.
    tentar(lambda: sim.setStepping(True)) if hasattr(sim, "setStepping") else cliente.setStepping(True)
    sim.startSimulation()
    dt = sim.getSimulationTimeStep()
    cfg = tentar(lambda: sim.callScriptFunction("str_config", script, {}), {})
    pdt = tentar(lambda: sim.getFloatParam(sim.floatparam_physicstimestep))
    print(f"\njunta motriz em modo {'FORCA/TORQUE' if cfg.get('modo_forca') else 'velocidade (antigo)'}; "
          f"passo da cena {dt * 1000:.0f} ms; passo da fisica {pdt * 1000 if pdt else '?'} ms")

    def trecho(torque, segundos):
        r, pat, n = {}, [], int(round(segundos / dt))
        v0 = None
        for _ in range(n):
            r = sim.callScriptFunction("str_trocar", script, {
                "torque": torque, "f_ign": 1, "f_pai": 1, "f_ped": 0.5 if torque > 0 else 0.0,
                "bloqueio": 0, "alarme": 0, "nivel": 0, "motivo": 0, "tem_rx": 0,
                "v_pino": 3.3, "soc": 0.9, "corrente": 0.0, "status": "diagnostico"})
            sim.step()
            if v0 is None:
                v0 = r.get("vel", 0.0)
            v, w = r.get("vel", 0.0), r.get("w", 0.0) * 0.31
            if torque > 0 and v > 1.0:
                pat.append(abs(w - v) / v)
        v1 = r.get("vel", 0.0)
        pat.sort()
        return v0, v1, (v1 - v0) / segundos, (pat[len(pat) // 2] if pat else float("nan")), r

    p0 = sim.getObjectPosition(chassi, sim.handle_world)
    a1 = trecho(2000.0, 3.0)
    p1 = sim.getObjectPosition(chassi, sim.handle_world)
    c_on = trecho(0.0, 2.0)
    a2 = trecho(2000.0, 2.0)
    tentar(lambda: sim.callScriptFunction("str_config", script, {"motorista": 0}))
    c_off = trecho(0.0, 2.0)
    tentar(lambda: sim.callScriptFunction("str_config", script, {"motorista": 1}))
    a3 = trecho(1125.0, 3.0)
    p2 = sim.getObjectPosition(chassi, sim.handle_world)
    sim.stopSimulation()

    m_carro = 1580.0
    ideal = lambda T: T / 0.31 / m_carro
    print("\n  trecho                         v ini -> fim km/h   aceleracao m/s2   ideal   patinagem")
    for nome, t, T in (("2000 N.m, 3 s", a1, 2000), ("sem torque, motorista ON", c_on, 0),
                       ("2000 N.m, 2 s", a2, 2000), ("sem torque, motorista OFF", c_off, 0),
                       ("1125 N.m (pedal 0,5), 3 s", a3, 1125)):
        id_ = ideal(T) if T else -0.14
        pat = f"{100 * t[3]:.0f} %" if t[3] == t[3] else "-"
        print(f"  {nome:28s} {t[0] * 3.6:7.1f} -> {t[1] * 3.6:5.1f}      {t[2]:+6.2f}          {id_:+5.2f}   {pat}")
    print(f"  chassi: z {p0[2]:.2f} -> {p2[2]:.2f} m; desvio lateral final {p2[1]:+.2f} m")
    desloc = math.dist(p0[:2], p1[:2])
    desac = -min(c_on[2], c_off[2])
    max_w = max(a1[4].get("w", 0.0), 0.0)
    # ---------------------------------------------------------- 3. diagnóstico
    print("\nconclusao:")
    esperado = 0.5 * (2000 / 0.31 / 1540) * 9  # ~26 m em 3 s sem perdas
    if estatica_chassi == 1:
        print("  O CHASSI ESTA ESTATICO: a fisica nao mexe nele (fica congelado no ar).")
        print("  A cena foi criada por uma versao antiga do script. Recrie com:")
        print("    python sim/coppelia/ponte_coppelia.py --recriar   (depois Ctrl+C)")
        print("  e rode este diagnostico de novo.")
    elif desloc > 3:
        print(f"  COM TORQUE O CARRO ANDA ({desloc:.1f} m em 3 s; ideal ~{esperado:.0f} m).")
        ef = a3[2] / ideal(1125)
        print(f"  aceleracao com 1125 N.m: {100 * ef:.0f} % da ideal "
              f"({'OK' if ef > 0.8 else 'BAIXA: o torque nao esta chegando inteiro ao chao'})")
        print(f"  sem torque: {-c_on[2]:.2f} m/s^2 com motorista, {-c_off[2]:.2f} sem "
              f"(modelo ~0,14; {'OK' if desac < 0.35 else 'ALTA: algo freia o carro'})")
        if desac >= 0.35 and -c_off[2] < 0.6 * -c_on[2]:
            print("  -> o motorista virtual e quem freia.")
        elif desac >= 0.35:
            print("  -> nao e o motorista: e o contato roda-pista do motor de fisica (resistencia ao rolamento).")
        print("  Se o carro nao anda na corrida com a ponte, confira na janela 'Carro STR':")
        print("   - 'Ignicao ligada' marcada? sem ignicao o torque e zero;")
        print("   - esfera de bloqueio (4a do teto) vermelha? bloqueado o torque e zero;")
        print("     (desmarcar 'Painel conforme' dispara o alarme e bloqueia em 5 s)")
        print("   - a linha 'torque ... N.m' da janela sobe quando voce mexe no acelerador?")
    elif max_w > 5:
        print("  AS RODAS GIRAM MAS O CARRO NAO SAI: as rodas nao tem atrito com o chao")
        print("  ou o chassi esta encostando/preso. Veja o z do chassi e das rodas acima.")
    elif p1[2] < 0.2:
        print("  O CARRO AFUNDOU/CAIU: o chassi atravessou o chao. Veja se a Pista e respondable.")
    else:
        print("  AS RODAS NAO GIRAM: o torque nao chega na junta. Veja 'dynctrl' das juntas")
        print("  traseiras acima (deveria ser o modo velocidade) e o 'alvo da junta'.")
    print("\nMe mande toda esta saida se nao resolver.")


if __name__ == "__main__":
    main()
