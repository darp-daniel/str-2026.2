# STR — teste de alarme e bloqueio (bancada)

- GPIO18 -> botao ignicao para GND (pressionado = ON)
- GPIO19 -> botao painel para GND (pressionado = CONFORME)
- GPIO26 -> IN do modulo rele (hipotese: ativo em nivel BAIXO)
- Relé VCC -> 5V/VIN somente se pino fornecer 5V pelo USB; GND comum
- LED nos contatos COM/NO com alimentacao propria de baixa tensao e resistor em serie.

**Importante:** mantenha o botao do painel pressionado durante a inicializacao. Solte-o para simular transicao CONFORME -> VIOLADO. O alarme aguarda 5 segundos e chama Block::lockByOverride(). Nao ha desbloqueio automatico. Reinicie a ESP32 para novo teste. Se o relé ligar no boot, desligue e verifique se o modulo e ativo em nivel alto (RELAY_ACTIVE_LEVEL em commands/block.cpp). Nao use cargas automotivas reais.

No terminal ESP-IDF, na pasta que contem CMakeLists.txt:

    idf.py set-target esp32
    idf.py build
    idf.py -p COM5 flash monitor

Substitua COM5 pela porta correta. Componentes nao usados do repositorio original foram preservados, mas ainda nao participam da compilacao.
