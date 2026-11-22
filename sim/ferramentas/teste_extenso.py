"""
teste_extenso.py — corrida longa de ponta a ponta que confere O QUE ACONTECEU
com o carro, não só os prazos da ESP.

    python sim/ferramentas/teste_extenso.py                  # CoppeliaSim aberto
    python sim/ferramentas/teste_extenso.py --recriar        # remonta a cena antes
    python sim/ferramentas/teste_extenso.py --esp serial:COM5
    python sim/ferramentas/teste_extenso.py --falso          # sem CoppeliaSim (testa a ferramenta)

O que ele faz, sozinho:
  1. roda o cenário (padrão C7, 45 s) com o carro INTERNO (modelo 1D) como referência;
  2. abre o ponte_coppelia.py numa porta livre (sem conflito com outra ponte aberta),
     com registro passo a passo do lado do CoppeliaSim;
  3. roda o mesmo cenário no CoppeliaSim, em tempo real, com a ESP escolhida;
  4. confere, com números:
       A. regras de prazo da ESP (o mesmo relatório do sim_bus);
       B. corte de torque: bloqueado ou sem ignição, o torque que CHEGOU ao CoppeliaSim é zero;
       C. sem torque o carro não ganha velocidade; com torque ele acelera;
       D. a velocidade do CoppeliaSim bate com o modelo interno;
       E. o carro fica em pé, na pista, e não anda de ré;
       F. posição x coerente com a velocidade integrada; patinagem das rodas;
       G. tempo: passos atrasados, deriva do relógio da física, quem é o gargalo;
  5. imprime uma tabela fase a fase e salva relatorio.txt + teste_extenso.png
     em sim/resultados/<data>_extenso/.

Código de saída: 0 sem FALHA (AVISO não reprova), 1 com FALHA, 2 erro de execução.
"""
import argparse
import bisect
import csv
import datetime
import math
import os
import queue
import re
import subprocess
import sys
import threading
import time

AQUI = os.path.dirname(os.path.abspath(__file__))
SIM = os.path.dirname(AQUI)
sys.path.insert(0, AQUI)
from rodar_cenarios import achar_sim_bus  # noqa: E402

PONTE = os.path.join(SIM, "coppelia", "ponte_coppelia.py")
RAIO = 0.31

# ------------------------------------------------------------------ utilidades


def ler_csv(caminho):
    with open(caminho, newline="", encoding="utf-8") as f:
        return list(csv.DictReader(f))


def f(x, padrao=float("nan")):
    try:
        return float(x)
    except (TypeError, ValueError):
        return padrao


def pct(v, p):
    if not v:
        return float("nan")
    v = sorted(v)
    return v[min(len(v) - 1, int(p * (len(v) - 1) + 0.5))]


def interp(ts, ys, t):
    i = bisect.bisect_left(ts, t)
    if i <= 0:
        return ys[0]
    if i >= len(ts):
        return ys[-1]
    t0, t1 = ts[i - 1], ts[i]
    a = (t - t0) / (t1 - t0) if t1 > t0 else 0
    return ys[i - 1] + a * (ys[i] - ys[i - 1])


class Relatorio:
    def __init__(self):
        self.linhas = []
        self.itens = []  # (grupo, nome, situacao, detalhe)

    def p(self, texto=""):
        print(texto, flush=True)
        self.linhas.append(texto)

    def item(self, grupo, nome, situacao, detalhe):
        self.itens.append((grupo, nome, situacao, detalhe))
        self.p(f"  [{situacao:5s}] {grupo}. {nome}: {detalhe}")

    def falhas(self):
        return [i for i in self.itens if i[2] == "FALHA"]

    def salvar(self, caminho):
        with open(caminho, "w", encoding="utf-8") as fp:
            fp.write("\n".join(self.linhas) + "\n")


# ------------------------------------------------------------------ execução


def rodar_referencia(sim_bus, cenario, pasta, dt):
    csv_ref = os.path.join(pasta, "referencia_interna.csv")
    cmd = [sim_bus, "--planta", "interna", "--cenario", cenario, "--csv", csv_ref, "--silencioso"]
    if dt:
        cmd += ["--dt", str(dt)]
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode == 2 or not os.path.exists(csv_ref):
        sys.stderr.write(r.stdout + r.stderr)
        sys.exit("erro ao rodar a referencia interna")
    return csv_ref


def abrir_ponte(args, pasta):
    log = os.path.join(pasta, "ponte_coppelia.csv")
    cmd = [sys.executable, "-u", PONTE, "--porta", "0", "--sair-no-fim", "--log", log]
    if args.falso:
        cmd.append("--falso")
    if args.recriar:
        cmd.append("--recriar")
    if args.dt:
        cmd += ["--dt", str(args.dt)]
    proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
                            encoding="utf-8", errors="replace")
    linhas, fila = [], queue.Queue()

    def ler():
        for l in proc.stdout:
            l = l.rstrip()
            linhas.append(l)
            fila.put(l)
        fila.put(None)

    threading.Thread(target=ler, daemon=True).start()
    limite = time.time() + 90  # --recriar monta 200 faixas: pode demorar
    while time.time() < limite:
        try:
            l = fila.get(timeout=0.5)
        except queue.Empty:
            continue
        if l is None:
            break
        print(f"  [ponte] {l}", flush=True)
        m = re.search(r"esperando o sim_bus em UDP (\d+)", l)
        if m:
            return proc, int(m.group(1)), linhas, fila, log
    proc.kill()
    sys.exit("\nA ponte nao ficou pronta. Saida dela:\n  " + "\n  ".join(linhas) +
             "\nO CoppeliaSim esta aberto? (sem ele, use --falso para testar so a ferramenta)")


def rodar_coppelia(args, sim_bus, cenario, pasta, porta):
    csv_c = os.path.join(pasta, "coppelia.csv")
    ev_c = os.path.join(pasta, "coppelia_eventos.csv")
    cmd = [sim_bus, "--planta", f"coppelia:{porta}", "--esp", args.esp, "--cenario", cenario,
           "--csv", csv_c, "--eventos", ev_c]
    if args.dt:
        cmd += ["--dt", str(args.dt)]
    print("\n$ " + " ".join(cmd), flush=True)
    proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
                            encoding="utf-8", errors="replace")
    saida = []
    for l in proc.stdout:
        print(l.rstrip(), flush=True)
        saida.append(l.rstrip())
    proc.wait()
    return proc.returncode, saida, csv_c, ev_c


# ------------------------------------------------------------------ análise


def serie(linhas, nome):
    return [f(l.get(nome)) for l in linhas]


def fases(t, ign, ped, bloq, alarme, torque, vel, x):
    """Divide a corrida em trechos em que (ignição, pedal, bloqueio, alarme) não mudam."""
    out, i0 = [], 0
    chave = lambda i: (int(ign[i]), round(ped[i], 2), int(bloq[i]), int(alarme[i]))
    for i in range(1, len(t) + 1):
        if i == len(t) or chave(i) != chave(i0):
            if t[i - 1] - t[i0] >= 0.15:
                tq = [v for v in torque[i0:i] if not math.isnan(v)]
                out.append({
                    "t0": t[i0], "t1": t[i - 1], "ign": int(ign[i0]), "ped": ped[i0], "bloq": int(bloq[i0]),
                    "alarme": int(alarme[i0]), "torque": sum(tq) / len(tq) if tq else 0.0,
                    "v0": vel[i0], "v1": vel[i - 1], "vmax": max(vel[i0:i]),
                    "dx": (x[i - 1] - x[i0]) if x and not math.isnan(x[i0]) and not math.isnan(x[i - 1]) else float("nan"),
                })
            i0 = i
    return out


def analisar(args, rel, rc, saida_bus, csv_c, csv_ref, log_ponte, saida_ponte):
    dt = args.dt or 0.01
    C = ler_csv(csv_c)
    R = ler_csv(csv_ref)
    P = ler_csv(log_ponte) if os.path.exists(log_ponte) else []
    if len(C) < 10:
        rel.item("A", "execucao", "FALHA", f"so {len(C)} passos registrados no CoppeliaSim")
        return

    t = serie(C, "t_s")
    t_sim = serie(C, "t_sim_s")
    vel = serie(C, "vel_kmh")
    torque = serie(C, "torque_Nm")
    ign = serie(C, "ignicao")
    ped = serie(C, "pedal")
    bloq = serie(C, "bloqueio")
    alarme = serie(C, "alarme")
    atrasado = serie(C, "atrasado")

    px = [f(l.get("x")) for l in P]
    tem_pos = P and not all(math.isnan(v) for v in px)
    # o log da ponte tem 1 linha por passo, na mesma ordem do CSV do sim_bus
    n = min(len(C), len(P)) if P else len(C)
    x = px[:n] + [float("nan")] * (len(C) - n) if tem_pos else []

    # ---- A. prazos da ESP
    rel.p("\nA. Prazos da ESP (verificador do sim_bus)")
    resumo = next((l for l in saida_bus if "regra(s):" in l), "")
    m = re.search(r"(\d+) regra\(s\): (\d+) OK, (\d+) falha", resumo)
    if m and rc == 0:
        rel.item("A", "regras da Tabela I", "OK", resumo.strip())
    else:
        rel.item("A", "regras da Tabela I", "FALHA", (resumo.strip() or "sem relatorio") + f" (saida {rc})")
    crc = re.search(r"erros CRC: (\d+)", "\n".join(saida_bus))
    if crc:
        rel.item("A", "enlace sem erro de CRC", "OK" if crc.group(1) == "0" else "FALHA", f"{crc.group(1)} erros")

    # ---- B. corte de torque
    rel.p("\nB. Corte de torque (rele)")
    # o torque da linha i foi calculado no passo anterior, com o estado da linha i-1
    sem_tracao = [i for i in range(1, len(C)) if bloq[i - 1] == 1 or ign[i - 1] == 0]
    pior = max((abs(torque[i]) for i in sem_tracao), default=0.0)
    rel.item("B", "torque calculado pelo sim_bus com bloqueio/sem ignicao",
             "OK" if pior == 0 else "FALHA", f"{len(sem_tracao)} passos sem tracao, |torque| max {pior:g} N.m")
    if P:
        pb = [abs(f(l["torque"])) for l in P if f(l.get("bloqueio")) == 1]
        rel.item("B", "torque que chegou ao CoppeliaSim durante o bloqueio",
                 "OK" if max(pb, default=0) == 0 else "FALHA",
                 f"{len(pb)} passos bloqueados no log da ponte, |torque| max {max(pb, default=0):g} N.m")
    else:
        rel.item("B", "log da ponte", "AVISO", "sem ponte_coppelia.csv: nao deu para ver o lado do CoppeliaSim")

    # ---- C. dinâmica fase a fase
    rel.p("\nC. O que o carro fez, fase a fase")
    fs = fases(t, ign, ped, bloq, alarme, torque, vel, x)
    rel.p("   t (s)          ign pedal bloq alarme  torque N.m   v ini -> fim (max) km/h   dx m   esperado  resultado")
    falhas_c = 0
    for fa in fs:
        dv = fa["v1"] - fa["v0"]
        dur = fa["t1"] - fa["t0"]
        if fa["torque"] > 50:
            esperado = "acelera"
            ok = dv > 0.5 or fa["v1"] > 150  # pode já estar perto da máxima
        elif fa["torque"] < -50:
            esperado = "freia"
            ok = dv < 0 or fa["v0"] < 0.5
        else:
            esperado = "nao acelera"
            ok = fa["vmax"] <= fa["v0"] + 1.0
        if dur < 0.5:  # trecho curto demais para julgar
            res = "-"
        else:
            res = "OK" if ok else "FALHA"
            falhas_c += not ok
        rel.p(f"   {fa['t0']:5.1f}-{fa['t1']:5.1f}   {fa['ign']:3d} {fa['ped']:5.2f} {fa['bloq']:4d} {fa['alarme']:6d}"
              f"  {fa['torque']:10.0f}   {fa['v0']:6.1f} -> {fa['v1']:6.1f} ({fa['vmax']:6.1f})"
              f"   {fa['dx']:6.1f}   {esperado:9s} {res}")
    rel.item("C", "comportamento em cada fase", "OK" if falhas_c == 0 else "FALHA",
             f"{len(fs)} fases, {falhas_c} com comportamento errado")
    vmax = max(vel)
    rel.item("C", "o carro andou", "OK" if vmax > 10 else "FALHA", f"velocidade maxima {vmax:.1f} km/h")

    # ---- D. CoppeliaSim x modelo interno
    rel.p("\nD. CoppeliaSim x modelo interno (mesmo cenario)")
    tr, vr = serie(R, "t_s"), serie(R, "vel_kmh")
    dif = [vel[i] - interp(tr, vr, t[i]) for i in range(len(C))]
    rms = math.sqrt(sum(d * d for d in dif) / len(dif))
    imax = max(range(len(dif)), key=lambda i: abs(dif[i]))
    vref = max(vr)
    sit = "OK" if rms <= max(5.0, 0.15 * vref) else "AVISO"
    rel.item("D", "velocidade", sit,
             f"RMS {rms:.1f} km/h, maior diferenca {dif[imax]:+.1f} km/h em t={t[imax]:.1f} s "
             f"(max interno {vref:.1f}, CoppeliaSim {vmax:.1f} km/h)")
    if sit == "AVISO":
        rel.p("         (o CoppeliaSim tem patinagem, atrito e inercia das rodas que o modelo 1D nao tem;"
              " diferenca grande pede revisar atrito/massa da cena)")

    # ---- E/F. postura e coerência (precisa do log da ponte com posição)
    rel.p("\nE/F. Postura do carro e coerencia da fisica")
    if tem_pos:
        z = [f(l.get("z")) for l in P[:n]]
        y = [f(l.get("y")) for l in P[:n]]
        zmin, zmax = min(z), max(z)
        rel.item("E", "chassi em pe", "OK" if 0.5 < zmin and zmax < 1.0 else "FALHA",
                 f"altura do centro {zmin:.3f} a {zmax:.3f} m (montado em 0,75)")
        rel.item("E", "na pista", "OK" if max(abs(v) for v in y) < 5 else "FALHA",
                 f"desvio lateral max {max(abs(v) for v in y):.2f} m (pista de 12 m)")
        fora = next((i for i in range(n) if abs(y[i]) > 6 or z[i] < 0.5), None)
        if fora is not None:
            rel.p(f"         o carro SAIU DA PISTA em t={f(P[fora]['t_sim']):.1f} s, x={px[fora]:.1f} m, "
                  f"y={y[fora]:.2f} m, z={z[fora]:.2f} m; o que vem depois disso nao vale")
        recuo = max(max(px[:i + 1]) - px[i] for i in range(n))
        rel.item("E", "sem marcha a re", "OK" if recuo < 0.3 else "FALHA", f"maior recuo em x {recuo:.2f} m")
        ts_p = [f(l.get("t_sim")) for l in P[:n]]
        vp = [f(l.get("vel_ms")) for l in P[:n]]
        integ = sum(vp[i] * (ts_p[i + 1] - ts_p[i]) for i in range(n - 1))
        andou = px[n - 1] - px[0]
        erro = abs(integ - andou) / max(andou, 1.0)
        rel.item("F", "x confere com a velocidade integrada", "OK" if erro < 0.1 else "AVISO",
                 f"andou {andou:.1f} m, integral da velocidade {integ:.1f} m ({100 * erro:.1f} %)")
        w = [f(l.get("w_roda")) for l in P[:n]]
        tq = [f(l.get("torque")) for l in P[:n]]
        pat = [abs(w[i] * RAIO - vp[i]) / vp[i] for i in range(n) if vp[i] > 2 and tq[i] > 50]
        if pat:
            mp = pct(pat, 0.5)
            rel.item("F", "patinagem das rodas motrizes acelerando", "OK" if mp < 0.2 else "AVISO",
                     f"mediana {100 * mp:.1f} %, p95 {100 * pct(pat, 0.95):.1f} % (|w.r - v| / v)")
    else:
        rel.item("E", "posicao do carro", "AVISO",
                 "sem x/y/z no log (cena falsa ou script antigo na cena: rode com --recriar)")

    # ---- G. tempo
    rel.p("\nG. Tempo real")
    nat = int(sum(1 for a in atrasado if a == 1))
    rel.item("G", "passos do sim_bus no horario", "OK" if nat < 0.05 * len(C) else "AVISO",
             f"{nat}/{len(C)} atrasados ({100 * nat / len(C):.0f} %, atraso > {dt * 500:.0f} ms)")
    deriva = [t[i] - t_sim[i] for i in range(len(C)) if not math.isnan(t_sim[i])]
    if deriva:
        d0 = deriva[0]
        dmax = max(abs(d - d0) for d in deriva)
        rel.item("G", "relogio da fisica acompanha o relogio real", "OK" if dmax < 0.1 else "AVISO",
                 f"diferenca max {1000 * dmax:.0f} ms entre tempo real e tempo do CoppeliaSim")
    if P:
        ms = [f(l.get("ms_passo")) for l in P]
        lim = dt * 1000
        acima = sum(1 for v in ms if v > lim)
        rel.item("G", "tempo do CoppeliaSim por passo (ZeroMQ + fisica)", "OK" if pct(ms, 0.95) < lim else "AVISO",
                 f"media {sum(ms) / len(ms):.1f} ms, mediana {pct(ms, 0.5):.1f}, p95 {pct(ms, 0.95):.1f}, "
                 f"max {max(ms):.1f}; {acima}/{len(ms)} acima de {lim:.0f} ms")
        if nat:
            lentos = sum(1 for i in range(1, n) if atrasado[i] == 1 and (f(P[i - 1]["ms_passo"]) > lim / 2
                                                                        or f(P[i]["ms_passo"]) > lim / 2))
            rel.p(f"         {lentos}/{nat} passos atrasados vieram logo apos um passo lento do CoppeliaSim"
                  f" (> {lim / 2:.0f} ms):")
            if lentos > 0.6 * nat:
                rel.p("         o gargalo e o CoppeliaSim/ZeroMQ. Tente: --dt 0.02, fechar outras janelas,"
                      " desligar a renderizacao (botao 'toggle rendering') ou o modo 'real-time' da cena.")
            else:
                rel.p("         o gargalo NAO e o CoppeliaSim: e o agendamento do Windows no sim_bus/ponte.")
        rel.p("         Os atrasos so deixam as latencias medidas MAIORES (conservadoras); nao mascaram falha da ESP.")
    rtt = next((l for l in saida_bus if "idade do quadro" in l), "")
    if rtt:
        rel.p("         " + rtt.strip())
    res_ponte = next((l for l in saida_ponte if "tempo por passo" in l), "")
    if res_ponte:
        rel.p("         ponte: " + res_ponte.split("encerrou. ")[-1])

    # ---- gráfico
    if not args.sem_grafico:
        try:
            grafico(os.path.join(os.path.dirname(csv_c), "teste_extenso.png"), t, vel, tr, vr, torque, ign,
                    bloq, alarme, P, n, dt, atrasado, args.titulo)
            rel.p(f"\ngrafico: {os.path.join(os.path.dirname(csv_c), 'teste_extenso.png')}")
        except Exception as e:  # matplotlib ausente não reprova o teste
            rel.p(f"\n(sem grafico: {e})")


def grafico(caminho, t, vel, tr, vr, torque, ign, bloq, alarme, P, n, dt, atrasado, titulo):
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    from graficos import SUPERFICIE, TINTA, TINTA_2, GRADE, SERIE, CRITICO, NEUTRO

    plt.rcParams.update({"font.size": 9, "axes.edgecolor": GRADE, "axes.labelcolor": TINTA_2,
                         "xtick.color": TINTA_2, "ytick.color": TINTA_2, "axes.titlecolor": TINTA,
                         "axes.titlesize": 10, "axes.titleweight": "bold", "axes.titlelocation": "left"})
    fig, ax = plt.subplots(4, 1, figsize=(11, 9.5), sharex=True, facecolor=SUPERFICIE,
                           gridspec_kw={"height_ratios": [3, 2, 1.2, 2]})
    for a in ax:
        a.set_facecolor(SUPERFICIE)
        a.grid(True, color=GRADE, linewidth=0.6)
        for s in ("top", "right"):
            a.spines[s].set_visible(False)

    # faixas de bloqueio no fundo dos painéis físicos
    def sombrear(a):
        i0 = None
        for i in range(len(t) + 1):
            on = i < len(t) and bloq[i] == 1
            if on and i0 is None:
                i0 = i
            if not on and i0 is not None:
                a.axvspan(t[i0], t[i - 1], color=CRITICO, alpha=0.08, linewidth=0)
                i0 = None

    a = ax[0]
    sombrear(a)
    a.plot(tr, vr, color=NEUTRO, linewidth=1.5, linestyle="--", label="modelo interno (referencia)")
    a.plot(t, vel, color=SERIE[0], linewidth=2, label="CoppeliaSim")
    a.set_ylabel("km/h")
    a.set_title("Velocidade do carro (faixa vermelha = bloqueado pela ESP)")
    a.legend(loc="upper left", frameon=False)

    a = ax[1]
    sombrear(a)
    a.plot(t, torque, color=SERIE[1], linewidth=1.5, label="calculado pelo sim_bus")
    if P:
        a.plot(t[:n], [f(l["torque"]) for l in P[:n]], color=SERIE[2], linewidth=1, linestyle=":",
               label="recebido pelo CoppeliaSim")
    a.set_ylabel("N.m nas rodas")
    a.set_title("Torque")
    a.legend(loc="upper right", frameon=False)

    a = ax[2]
    trilhas = [("ignicao", ign, SERIE[0]), ("alarme", alarme, SERIE[3]), ("bloqueio", bloq, CRITICO)]
    for k, (nome, v, c) in enumerate(trilhas):
        base = k * 1.4
        a.fill_between(t, base, [base + 0.9 * x for x in v], step="post", color=c, alpha=0.85, linewidth=0)
        a.text(t[0], base + 0.45, nome + "  ", ha="right", va="center", color=TINTA_2)
    a.set_yticks([])
    a.set_ylim(-0.2, 1.4 * len(trilhas))
    a.set_title("Estados (ignicao do simulador; alarme e bloqueio vindos da ESP)")

    a = ax[3]
    if P:
        a.plot(t[:n], [f(l["ms_passo"]) for l in P[:n]], color=SERIE[0], linewidth=0.8,
               label="tempo do CoppeliaSim por passo")
    at = [t[i] for i in range(len(t)) if atrasado[i] == 1]
    if at:
        a.plot(at, [dt * 1000 * 1.9] * len(at), "|", color=CRITICO, markersize=6, label="passo atrasado")
    a.axhline(dt * 1000, color=TINTA_2, linewidth=1, linestyle="--")
    a.text(t[-1], dt * 1000, f" passo {dt * 1000:.0f} ms", va="bottom", ha="right", color=TINTA_2)
    a.set_ylabel("ms")
    a.set_xlabel("tempo (s)")
    a.set_title("Tempo real")
    a.legend(loc="upper right", frameon=False)

    fig.suptitle(titulo, x=0.01, ha="left", color=TINTA, fontsize=12, fontweight="bold")
    fig.tight_layout()
    fig.savefig(caminho, dpi=120, facecolor=SUPERFICIE)
    plt.close(fig)


# ------------------------------------------------------------------ main


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--cenario", default=os.path.join(SIM, "cenarios", "C7_percurso_completo.txt"))
    ap.add_argument("--esp", default="virtual", help="virtual | serial:COM5 | udp:host:porta")
    ap.add_argument("--dt", type=float, help="passo em s (padrao 0.01)")
    ap.add_argument("--recriar", action="store_true", help="remonta carro e pista com o cena_carro.lua atual")
    ap.add_argument("--falso", action="store_true", help="usa a cena falsa da ponte (sem CoppeliaSim)")
    ap.add_argument("--saida", help="pasta de resultados")
    ap.add_argument("--sem-grafico", action="store_true")
    a = ap.parse_args()

    sim_bus = achar_sim_bus()
    nome = os.path.splitext(os.path.basename(a.cenario))[0]
    pasta = a.saida or os.path.join(SIM, "resultados", datetime.datetime.now().strftime("%Y%m%d_%H%M%S") + "_extenso")
    os.makedirs(pasta, exist_ok=True)
    a.titulo = f"Teste extenso: {nome} ({'cena falsa' if a.falso else 'CoppeliaSim'}, ESP {a.esp})"

    print(f"== teste extenso: {nome} ==\nresultados em {pasta}\n\n1. referencia com o carro interno ...", flush=True)
    csv_ref = rodar_referencia(sim_bus, a.cenario, pasta, a.dt)

    print("2. abrindo a ponte do CoppeliaSim ...", flush=True)
    ponte, porta, saida_ponte, fila, log_ponte = abrir_ponte(a, pasta)
    try:
        print("3. corrida no CoppeliaSim (tempo real) ...", flush=True)
        rc, saida_bus, csv_c, _ = rodar_coppelia(a, sim_bus, a.cenario, pasta, porta)
        try:
            ponte.wait(timeout=15)
        except subprocess.TimeoutExpired:
            ponte.kill()
        while True:  # mostra o que a ponte imprimiu no fim (resumo de tempo)
            try:
                l = fila.get_nowait()
            except queue.Empty:
                break
            if l:
                print(f"  [ponte] {l}")
    finally:
        if ponte.poll() is None:
            ponte.kill()

    if rc == 2:
        sys.exit("sim_bus terminou com erro de execucao (veja acima)")

    rel = Relatorio()
    rel.p("\n================ CONFERENCIA ================")
    rel.p(a.titulo)
    analisar(a, rel, rc, saida_bus, csv_c, csv_ref, log_ponte, saida_ponte)

    falhas = rel.falhas()
    avisos = [i for i in rel.itens if i[2] == "AVISO"]
    rel.p(f"\n{len(rel.itens)} conferencias: {len(rel.itens) - len(falhas) - len(avisos)} OK, "
          f"{len(avisos)} aviso(s), {len(falhas)} falha(s)")
    rel.p("RESULTADO: " + ("REPROVADO" if falhas else "APROVADO" + (" com avisos" if avisos else "")))
    rel.salvar(os.path.join(pasta, "relatorio.txt"))
    print(f"relatorio: {os.path.join(pasta, 'relatorio.txt')}")
    return 1 if falhas else 0


if __name__ == "__main__":
    sys.exit(main())
