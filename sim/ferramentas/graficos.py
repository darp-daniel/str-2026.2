"""
graficos.py — teste visual offline: transforma o CSV de uma execução do sim_bus
num PNG com a linha do tempo e as latências medidas.

    python graficos.py serie.csv [eventos.csv] [-o saida.png] [--titulo "C2"]

Painéis (eixo x compartilhado, um eixo y por painel):
  1. velocidade do carro
  2. torque nas rodas (mostra o corte quando a ESP bloqueia)
  3. tensão no pino do ADC, com os limiares 2,5 V e 1,5 V
  4. estados digitais: ignição, painel violado, alarme, bloqueio
e, se houver eventos.csv, um gráfico com a latência de cada regra em % do prazo.
"""
import argparse
import csv
import os
import sys

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt  # noqa: E402

# Paleta de referência (skill dataviz): categóricas em ordem fixa + status reservados.
SUPERFICIE = "#fcfcfb"
TINTA = "#0b0b0b"
TINTA_2 = "#52514e"
GRADE = "#e4e3df"
SERIE = ["#2a78d6", "#eb6834", "#1baf7a", "#eda100", "#e87ba4", "#008300", "#6250d6", "#e34948"]
OK = "#0ca30c"
CRITICO = "#d03b3b"
NEUTRO = "#9a9993"


def ler_csv(caminho):
    with open(caminho, newline="", encoding="utf-8") as f:
        return list(csv.DictReader(f))


def col(linhas, nome, conv=float):
    return [conv(l[nome]) for l in linhas]


def estilo(ax, ylabel):
    ax.set_facecolor(SUPERFICIE)
    ax.set_ylabel(ylabel, color=TINTA_2, fontsize=9)
    ax.tick_params(colors=TINTA_2, labelsize=8, length=0)
    ax.grid(True, axis="y", color=GRADE, linewidth=0.8)
    ax.grid(False, axis="x")
    for lado in ("top", "right", "left"):
        ax.spines[lado].set_visible(False)
    ax.spines["bottom"].set_color(GRADE)


def gerar(serie_csv, eventos_csv=None, saida=None, titulo=None):
    s = ler_csv(serie_csv)
    if not s:
        raise SystemExit(f"{serie_csv} esta vazio")
    t = col(s, "t_s")
    eventos = ler_csv(eventos_csv) if eventos_csv and os.path.exists(eventos_csv) else []
    eventos = [e for e in eventos if e["resultado"] != "N/A"]

    tem_lat = bool(eventos)
    alturas = [1.2, 1.0, 1.2, 1.3] + ([max(1.2, 0.28 * len(eventos))] if tem_lat else [])
    fig = plt.figure(figsize=(11, 2.0 * sum(alturas) + 0.8), facecolor=SUPERFICIE)
    gs = fig.add_gridspec(len(alturas), 1, height_ratios=alturas, hspace=0.45, top=0.95, bottom=0.05)
    ax_v = fig.add_subplot(gs[0])
    ax_t = fig.add_subplot(gs[1], sharex=ax_v)
    ax_p = fig.add_subplot(gs[2], sharex=ax_v)
    ax_e = fig.add_subplot(gs[3], sharex=ax_v)

    # 1. velocidade
    ax_v.plot(t, col(s, "vel_kmh"), color=SERIE[0], linewidth=2)
    estilo(ax_v, "velocidade (km/h)")
    ax_v.set_ylim(bottom=0)

    # 2. torque
    ax_t.plot(t, col(s, "torque_Nm"), color=SERIE[0], linewidth=2)
    estilo(ax_t, "torque (N·m)")

    # 3. tensão no pino + limiares
    ax_p.plot(t, col(s, "v_pino"), color=SERIE[0], linewidth=2)
    for v, rot in ((2.5, "2,5 V bateria baixa"), (1.5, "1,5 V bateria critica")):
        ax_p.axhline(v, color=TINTA_2, linewidth=1, linestyle=(0, (4, 3)))
        ax_p.text(t[-1], v + 0.05, rot, ha="right", va="bottom", fontsize=8, color=TINTA_2)
    estilo(ax_p, "V no pino do ADC")
    ax_p.set_ylim(0, 3.5)

    # 4. estados digitais em faixas (identidade pelo rótulo do eixo, não só pela cor)
    faixas = [
        ("ignicao", [int(x) for x in col(s, "ignicao")], SERIE[0]),
        ("painel violado", [1 - int(x) for x in col(s, "painel_ok")], SERIE[1]),
        ("alarme (ESP)", [int(x) for x in col(s, "alarme")], SERIE[2]),
        ("bloqueio (ESP)", [int(x) for x in col(s, "bloqueio")], SERIE[3]),
    ]
    for i, (nome, ys, cor) in enumerate(faixas):
        base = len(faixas) - 1 - i
        ax_e.fill_between(t, base, [base + 0.7 * y for y in ys], step="post", color=cor, linewidth=0)
    ax_e.set_yticks([len(faixas) - 1 - i + 0.35 for i in range(len(faixas))])
    ax_e.set_yticklabels([f[0] for f in faixas])
    # comandos do usuário: marcadores no topo
    cmds = [(float(l["t_s"]), int(l["cmd"])) for l in s]
    vistos, xs = set(), []
    for (tt, c), l in zip(cmds, s):
        cid = l["cmd_id"]
        if c and cid not in vistos:
            vistos.add(cid)
            xs.append(tt)
    if xs:
        ax_e.plot(xs, [len(faixas) + 0.1] * len(xs), linestyle="none", marker="v", markersize=8,
                  color=TINTA_2, markeredgecolor=SUPERFICIE, markeredgewidth=2, label="comando do usuario")
        ax_e.legend(loc="lower right", fontsize=8, frameon=False, bbox_to_anchor=(1, 0.98))
    estilo(ax_e, "")
    ax_e.grid(False)
    ax_e.set_ylim(-0.2, len(faixas) + 0.4)
    ax_e.set_xlabel("tempo (s)", color=TINTA_2, fontsize=9)
    for ax in (ax_v, ax_t, ax_p):
        plt.setp(ax.get_xticklabels(), visible=False)

    # marca os eventos verificados nos painéis de tempo (linhas finas)
    for e in eventos:
        te = float(e["t_evento_s"])
        for ax in (ax_v, ax_t, ax_p, ax_e):
            ax.axvline(te, color=GRADE, linewidth=1, zorder=0)

    # 5. latências em % do prazo
    if tem_lat:
        ax_l = fig.add_subplot(gs[4])
        nomes, pct, cores, rot = [], [], [], []
        for e in eventos:
            lat, prazo = float(e["latencia_ms"]), float(e["prazo_ms"])
            nomes.append(f"{float(e['t_evento_s']):5.2f}s {e['tarefa']} {e['regra']}")
            falhou = e["resultado"] == "FALHOU"
            if lat >= 0 and prazo > 0:
                pct.append(100 * lat / prazo)
                rot.append(f"{lat:.0f} ms / {prazo:.0f} ms  {e['resultado']}")
            else:
                pct.append(0)
                rot.append(f"{e['resultado']}: {e['detalhe']}")
            cores.append(CRITICO if falhou else (NEUTRO if e["resultado"] == "INCONCLUSIVO" else OK))
        y = list(range(len(nomes)))[::-1]
        ax_l.barh(y, pct, height=0.6, color=cores, edgecolor=SUPERFICIE, linewidth=2)
        ax_l.axvline(100, color=TINTA, linewidth=1)
        ax_l.text(100, len(nomes) - 0.4, " prazo", fontsize=8, color=TINTA, va="bottom")
        for yy, p, r in zip(y, pct, rot):
            ax_l.text(p + 2, yy, r, va="center", fontsize=8, color=TINTA_2)
        ax_l.set_yticks(y)
        ax_l.set_yticklabels(nomes, fontsize=8)
        ax_l.set_xlim(0, max(140, max(pct) * 1.45))
        estilo(ax_l, "")
        ax_l.grid(True, axis="x", color=GRADE, linewidth=0.8)
        ax_l.grid(False, axis="y")
        ax_l.set_xlabel("latencia medida (% do prazo)  -  verde OK, vermelho FALHOU, cinza inconclusivo",
                        color=TINTA_2, fontsize=9)

    nome = titulo or os.path.splitext(os.path.basename(serie_csv))[0]
    if eventos:
        n_f = sum(1 for e in eventos if e["resultado"] == "FALHOU")
        nome += f"  -  {len(eventos) - n_f}/{len(eventos)} regras OK" + ("" if n_f == 0 else f", {n_f} FALHA(S)")
    fig.suptitle(nome, x=0.01, y=0.985, ha="left", fontsize=12, color=TINTA)
    saida = saida or os.path.splitext(serie_csv)[0] + ".png"
    fig.savefig(saida, dpi=110, bbox_inches="tight", facecolor=SUPERFICIE)
    plt.close(fig)
    return saida


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("serie")
    ap.add_argument("eventos", nargs="?")
    ap.add_argument("-o", "--saida")
    ap.add_argument("--titulo")
    a = ap.parse_args()
    print(gerar(a.serie, a.eventos, a.saida, a.titulo))
    return 0


if __name__ == "__main__":
    sys.exit(main())
