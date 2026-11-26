"""Cadeia completa até o adaptador do CoppeliaSim, com a cena FALSA:

    sim_bus (C++) --UDP--> ponte_coppelia.py --falso
         \--> ESP virtual

Confere: a ponte aprova os prazos e, enquanto a ESP mandou bloqueio = 1,
o torque enviado para a cena foi sempre zero.

uso: teste_ponte_coppelia.py <ponte_coppelia.py> <sim_bus> <cenario>
"""
import re
import socket
import subprocess
import sys
import time


def porta_livre():
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.bind(("127.0.0.1", 0))
    p = s.getsockname()[1]
    s.close()
    return p


def main():
    ponte, bus_exe, cenario = sys.argv[1:4]
    porta = porta_livre()
    adaptador = subprocess.Popen([sys.executable, ponte, "--falso", "--sair-no-fim", "--porta", str(porta)],
                                 stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    time.sleep(1.0)
    try:
        bus = subprocess.run([bus_exe, "--planta", f"coppelia:{porta}", "--cenario", cenario],
                             capture_output=True, text=True, timeout=120)
        saida_adapt = adaptador.communicate(timeout=15)[0]
    finally:
        if adaptador.poll() is None:
            adaptador.kill()
    print(bus.stdout)
    print(bus.stderr)
    print("--- ponte_coppelia.py ---")
    print(saida_adapt)

    ok = True
    if bus.returncode != 0 or "APROVADO" not in bus.stdout:
        print(f"FALHA: sim_bus saiu com {bus.returncode}")
        ok = False
    m = re.search(r"viu_bloqueio=(\d) torque_com_bloqueio_max=([\d.e+-]+)", saida_adapt)
    if not m:
        print("FALHA: adaptador nao imprimiu o resumo")
        ok = False
    else:
        if m.group(1) != "1":
            print("FALHA: a cena nunca recebeu bloqueio = 1")
            ok = False
        if float(m.group(2)) != 0.0:
            print(f"FALHA: torque {m.group(2)} enviado com o carro bloqueado")
            ok = False
    print("OK" if ok else "REPROVADO")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
