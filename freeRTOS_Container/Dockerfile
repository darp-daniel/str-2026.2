FROM espressif/idf:release-v5.3

# Dependências para compilar o FreeRTOS no modo Simulador POSIX/Linux e comunicar via Sockets/ZeroMQ
RUN apt-get update && apt-get install -y \
  build-essential \
  cmake \
  libzmq3-dev \
  iproute2 \
  && rm -rf /var/lib/apt/lists/*

WORKDIR /workspace

# Para compilar e rodar o FreeRTOS simulado em Linux dentro do container:
CMD ["/bin/bash", "-c", "idf.py --preview set-target linux && idf.py build && ./build/str_telemetria.elf"]
