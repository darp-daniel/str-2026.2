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
    print("\nteste: 3 s com torque de 2000 N.m nas rodas traseiras (ignicao ON, sem bloqueio)")
    tentar(lambda: sim.setStepping(True)) if hasattr(sim, "setStepping") else cliente.setStepping(True)
    sim.startSimulation()
    dt = sim.getSimulationTimeStep()
    p0 = sim.getObjectPosition(chassi, sim.handle_world)
    max_w, ret = 0.0, {}
    passos = int(3.0 / dt)
    for k in range(passos):
        ret = sim.callScriptFunction("str_trocar", script, {
            "torque": 2000.0, "f_ign": 1, "f_pai": 1, "f_ped": 0.5,
            "bloqueio": 0, "alarme": 0, "nivel": 0, "motivo": 0, "tem_rx": 0,
            "v_pino": 3.3, "soc": 0.9, "corrente": 0.0, "status": "diagnostico"})
        sim.step()
        w = abs(sim.getJointVelocity(te)) + abs(sim.getJointVelocity(td))
        max_w = max(max_w, w / 2)
    p1 = sim.getObjectPosition(chassi, sim.handle_world)
    alvo = (tentar(lambda: sim.getJointTargetVelocity(te)), tentar(lambda: sim.getJointTargetForce(te)))

    # 2 s sem torque: quanto o carro perde de velocidade "sozinho"
    v_ini = ret.get("vel", 0.0) if isinstance(ret, dict) else 0.0
    for k in range(int(2.0 / dt)):
        ret2 = sim.callScriptFunction("str_trocar", script, {
            "torque": 0.0, "f_ign": 1, "f_pai": 1, "f_ped": 0.0, "bloqueio": 0, "alarme": 0, "nivel": 0,
            "motivo": 0, "tem_rx": 0, "v_pino": 3.3, "soc": 0.9, "corrente": 0.0, "status": "diagnostico"})
        sim.step()
    v_fim = ret2.get("vel", 0.0)
    sim.stopSimulation()
    desac = (v_ini - v_fim) / 2.0

    desloc = math.dist(p0[:2], p1[:2])
    print(f"  passo da cena: {dt * 1000:.0f} ms, {passos} passos")
    print(f"  chassi: z {p0[2]:.2f} -> {p1[2]:.2f} m, andou {desloc:.2f} m em x/y "
          f"(x {p0[0]:.2f} -> {p1[0]:.2f})")
    print(f"  rodas traseiras: |w| maximo {max_w:.1f} rad/s; alvo da junta (vel, forca) = {alvo}")
    print(f"  str_trocar devolveu: {ret}")
    print(f"  depois, 2 s sem torque: {v_ini * 3.6:.1f} -> {v_fim * 3.6:.1f} km/h, "
          f"desaceleracao {desac:.2f} m/s^2 (modelo: ~0,2; acima de 0,8 algo freia o carro)")

    # ---------------------------------------------------------- 3. diagnóstico
    print("\nconclusao:")
    esperado = 0.5 * (2000 / 0.31 / 1540) * 9  # ~26 m em 3 s sem perdas
    if estatica_chassi == 1:
        print("  O CHASSI ESTA ESTATICO: a fisica nao mexe nele (fica congelado no ar).")
        print("  A cena foi criada por uma versao antiga do script. Recrie com:")
        print("    python sim/coppelia/ponte_coppelia.py --recriar   (depois Ctrl+C)")
        print("  e rode este diagnostico de novo.")
    elif desloc > 3:
        print(f"  COM TORQUE O CARRO ANDA ({desloc:.1f} m; ideal ~{esperado:.0f} m).")
        if desac > 0.8:
            print(f"  MAS ALGO FREIA O CARRO: {desac:.2f} m/s^2 sem torque. Rodas raspando no chassi?")
            print("  Veja 'mascara' das rodas e do chassi acima (deveria ser 0xff00) e recrie a cena.")
        print("  O problema esta no controle. Confira na janela 'Carro STR':")
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
