
import serial
import time
import threading
import re
import statistics

PORTA = "COM9"
BAUD = 115200

INTERVALO = 0.010
DEADLINE_MS = 30

COMANDOS = [
    "BLOQUEAR",
    "ATUALIZAR",
    "DESBLOQUEAR",
    "BLOQUEAR",
    "ATUALIZAR",
    "DESBLOQUEAR",
    "BLOQUEAR",
    "ATUALIZAR",
    "DESBLOQUEAR",
    "ATUALIZAR"
]

envios = []
resultados = []
encerrar = threading.Event()

padrao = re.compile(
    r"C4 ID=(\d+).*?"
    r"Interpretacao=(\d+) us.*?"
    r"Espera=(\d+) us.*?"
    r"Processamento=(\d+) us.*?"
    r"Resposta=(\d+) us"
)

esp = serial.Serial(
    PORTA,
    BAUD,
    timeout=0.05
)

print("Conectado ao ESP32.")
time.sleep(3)
esp.reset_input_buffer()


def receber():
    while not encerrar.is_set():

        linha = esp.readline().decode(
            "utf-8", errors="replace"
        ).strip()

        instante = time.perf_counter()

        if not linha:
            continue

        print("[ESP]", linha)

        dados = padrao.search(linha)

        if dados:
            identificador, interp, espera, proc, resp = (
                map(int, dados.groups())
            )

            resultados.append({
                "id": identificador,
                "interpretacao": interp,
                "espera": espera,
                "processamento": proc,
                "resposta": resp,
                "recebimento": instante
            })


leitor = threading.Thread(
    target=receber,
    daemon=True
)

leitor.start()

print("\nINICIANDO TESTE C6\n")

inicio = time.perf_counter()

for i, comando in enumerate(COMANDOS):

    alvo = inicio + i * INTERVALO

    while time.perf_counter() < alvo:
        pass

    instante = time.perf_counter()

    envios.append({
        "comando": comando,
        "tempo": instante
    })

    esp.write((comando + "\n").encode("ascii"))

fim_envios = time.perf_counter()

# Aguarda as confirmacoes
time.sleep(3)

encerrar.set()
leitor.join()
esp.close()

janela_ms = (fim_envios - inicio) * 1000

print("\n" + "=" * 60)
print("RESULTADO DO TESTE C6")
print("=" * 60)

print(f"Comandos enviados: {len(envios)}")
print(f"Respostas recebidas: {len(resultados)}")
print(f"Janela de envio: {janela_ms:.3f} ms")

print(f"Respostas faltantes: "
      f"{max(0, len(envios) - len(resultados))}")

print("\nTEMPOS INTERNOS POR COMANDO")

for r in resultados:

    resposta_ms = r["resposta"] / 1000

    status = (
        "OK"
        if resposta_ms <= DEADLINE_MS
        else "VIOLADO"
    )

    print(
        f"ID={r['id']:3d} | "
        f"Espera={r['espera']:5d} us | "
        f"Processamento={r['processamento']:5d} us | "
        f"Resposta={resposta_ms:8.3f} ms | "
        f"{status}"
    )

if resultados:

    tempos = [
        r["resposta"] / 1000
        for r in resultados
    ]

    violacoes = sum(
        t > DEADLINE_MS for t in tempos
    )

    print("\nESTATISTICAS INTERNAS")

    print(f"Minimo: {min(tempos):.3f} ms")
    print(f"Media: {statistics.mean(tempos):.3f} ms")
    print(f"Maximo: {max(tempos):.3f} ms")
    print(f"Violacoes: {violacoes}")

    if len(resultados) == len(envios):

        print("\nLATENCIA PC - ESP32 - PC")
        print("(Ate o recebimento do log C4)")

        latencias = []

        for envio, resposta in zip(envios, resultados):

            latencia = (
                resposta["recebimento"] - envio["tempo"]
            ) * 1000

            latencias.append(latencia)

            print(
                f"{envio['comando']:12s} | "
                f"{latencia:.3f} ms"
            )

        print(f"Latencia maxima: "
              f"{max(latencias):.3f} ms")

    else:
        print("\nLatencia individual nao calculada:")
        print("Quantidade de respostas divergente.")

    if (
        len(resultados) == len(envios)
        and violacoes == 0
        and janela_ms <= 100
    ):
        print("\nC6: APROVADO NO TESTE INTERNO")
    else:
        print("\nC6: NECESSITA ANALISE")

else:
    print("Nenhuma resposta temporal recebida.")
