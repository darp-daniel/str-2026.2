"""Enlace UDP com dois processos: esp_virtual (servidor) + sim_bus (ponte), em tempo real.

uso: teste_udp.py <esp_virtual> <sim_bus> <cenario>
"""
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
    esp_exe, bus_exe, cenario = sys.argv[1:4]
    porta = porta_livre()
    esp = subprocess.Popen([esp_exe, "--porta", str(porta), "--duracao", "60"],
                           stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    time.sleep(0.5)
    try:
        bus = subprocess.run([bus_exe, "--esp", f"udp:127.0.0.1:{porta}", "--cenario", cenario],
                             capture_output=True, text=True, timeout=120)
    finally:
        esp.terminate()
        esp_saida = esp.communicate(timeout=10)[0]
    print(bus.stdout)
    print(bus.stderr)
    print("--- esp_virtual ---")
    print(esp_saida)

    ok = True
    if bus.returncode != 0:
        print(f"FALHA: sim_bus saiu com {bus.returncode}")
        ok = False
    if "tempo real" not in bus.stdout:
        print("FALHA: o enlace UDP deveria forcar tempo real")
        ok = False
    if "APROVADO" not in bus.stdout:
        print("FALHA: verificador nao aprovou")
        ok = False
    if "erros CRC: 0" not in bus.stdout:
        print("FALHA: houve erro de CRC no enlace")
        ok = False
    print("OK" if ok else "REPROVADO")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
