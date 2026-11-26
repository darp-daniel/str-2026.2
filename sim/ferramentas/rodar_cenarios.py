"""
rodar_cenarios.py — roda C1..C6 (ou os que você escolher), salva CSVs e
gráficos e imprime um resumo. É o "testar tudo junto" em um comando.

    # sem placa, sem CoppeliaSim (rápido)
    python sim/ferramentas/rodar_cenarios.py

    # com a ESP32 física pela USB
    python sim/ferramentas/rodar_cenarios.py --esp serial:COM5

    # HIL completo: CoppeliaSim + ESP32 (deixe ponte_coppelia.py rodando antes)
    python sim/ferramentas/rodar_cenarios.py --planta coppelia --esp serial:COM5

Saída em sim/resultados/<data-hora>/ : <cenario>.csv, <cenario>_eventos.csv,
<cenario>.png e resumo.txt
"""
import argparse
import datetime
import glob
import os
import subprocess
import sys

AQUI = os.path.dirname(os.path.abspath(__file__))
SIM = os.path.dirname(AQUI)
sys.path.insert(0, AQUI)


def achar_sim_bus():
    for nome in ("sim_bus.exe", "sim_bus"):
        for pasta in ("build", "build/Release", "build/Debug"):
            p = os.path.join(SIM, pasta, nome)
            if os.path.exists(p):
                return p
    sys.exit("sim_bus nao encontrado. Compile antes:\n"
             "  cmake -S sim -B sim/build -G Ninja && cmake --build sim/build")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("cenarios", nargs="*", help="arquivos de cenario (padrao: sim/cenarios/C*.txt)")
    ap.add_argument("--planta", default="interna")
    ap.add_argument("--esp", default="virtual")
    ap.add_argument("--baud", default="115200")
    ap.add_argument("--saida", help="pasta de resultados")
    ap.add_argument("--sem-graficos", action="store_true")
    a = ap.parse_args()

    sim_bus = achar_sim_bus()
    cenarios = a.cenarios or sorted(glob.glob(os.path.join(SIM, "cenarios", "C*.txt")))
    pasta = a.saida or os.path.join(SIM, "resultados", datetime.datetime.now().strftime("%Y%m%d_%H%M%S"))
    os.makedirs(pasta, exist_ok=True)

    graficos = None
    if not a.sem_graficos:
        try:
            import graficos  # noqa: F401
        except ImportError as e:
            print(f"(sem graficos: {e}; instale matplotlib)")
            graficos = None

    linhas = []
    for c in cenarios:
        nome = os.path.splitext(os.path.basename(c))[0]
        csv = os.path.join(pasta, nome + ".csv")
        ev = os.path.join(pasta, nome + "_eventos.csv")
        cmd = [sim_bus, "--cenario", c, "--planta", a.planta, "--esp", a.esp, "--baud", a.baud,
               "--csv", csv, "--eventos", ev]
        print(f"\n===== {nome} =====", flush=True)
        r = subprocess.run(cmd, capture_output=True, text=True)
        with open(os.path.join(pasta, nome + ".log"), "w", encoding="utf-8") as f:
            f.write(r.stdout + r.stderr)
        rel = r.stdout[r.stdout.find("== Relatorio"):] if "== Relatorio" in r.stdout else r.stdout + r.stderr
        print(rel.strip())
        estado = {0: "APROVADO", 1: "REPROVADO"}.get(r.returncode, "ERRO")
        png = ""
        if graficos and os.path.exists(csv):
            png = graficos.gerar(csv, ev, titulo=f"{nome}  ({a.planta} + ESP {a.esp})")
        linhas.append((nome, estado, png))

    resumo = ["", f"planta={a.planta}  esp={a.esp}", ""]
    resumo += [f"  {n:28s} {e:10s} {os.path.basename(p) if p else ''}" for n, e, p in linhas]
    falhou = sum(1 for _, e, _ in linhas if e != "APROVADO")
    resumo += ["", f"  {len(linhas) - falhou}/{len(linhas)} cenarios aprovados", f"  resultados em {pasta}"]
    texto = "\n".join(resumo)
    print(texto)
    with open(os.path.join(pasta, "resumo.txt"), "w", encoding="utf-8") as f:
        f.write(texto + "\n")
    return 0 if falhou == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
