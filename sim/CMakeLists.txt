# Simulador / ponte HIL — build do lado do PC (independente do ESP-IDF).
#
#   cmake -S sim -B sim/build -G Ninja
#   cmake --build sim/build
#   ctest --test-dir sim/build --output-on-failure
cmake_minimum_required(VERSION 3.16)
project(str_simulador CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release)
endif()

set(RAIZ ${CMAKE_CURRENT_SOURCE_DIR}/..)

# Núcleo: modelos, cenário, verificador, enlaces, ESP virtual.
# Usa o código REAL do firmware para o alarme (sensors/alarm.cpp).
add_library(str_nucleo STATIC
  nucleo/modelos.cpp
  nucleo/cenario.cpp
  nucleo/esp_virtual.cpp
  nucleo/block_host.cpp
  nucleo/enlace.cpp
  nucleo/planta.cpp
  nucleo/verificador.cpp
  nucleo/simulacao.cpp
  ${RAIZ}/sensors/alarm.cpp
)
target_include_directories(str_nucleo PUBLIC nucleo ${RAIZ}/include)
if(MSVC)
  target_compile_options(str_nucleo PUBLIC /W4 /utf-8)
else()
  target_compile_options(str_nucleo PUBLIC -Wall -Wextra)
endif()
if(WIN32)
  target_link_libraries(str_nucleo PUBLIC ws2_32 winmm)
else()
  find_package(Threads REQUIRED)
  target_link_libraries(str_nucleo PUBLIC Threads::Threads)
endif()
if(MINGW)
  # executáveis sem depender das DLLs do MinGW no PATH
  target_link_options(str_nucleo PUBLIC -static)
endif()

add_executable(sim_bus apps/sim_bus.cpp)
target_link_libraries(sim_bus PRIVATE str_nucleo)

add_executable(esp_virtual apps/esp_virtual_main.cpp)
target_link_libraries(esp_virtual PRIVATE str_nucleo)

# ------------------------------------------------------------------ testes
enable_testing()

foreach(t protocolo modelos cenario esp_virtual verificador)
  add_executable(teste_${t} testes/teste_${t}.cpp)
  target_link_libraries(teste_${t} PRIVATE str_nucleo)
  add_test(NAME ${t} COMMAND teste_${t})
endforeach()
target_compile_definitions(teste_cenario PRIVATE STR_DIR_CENARIOS="${CMAKE_CURRENT_SOURCE_DIR}/cenarios")

# Integração: cada cenário C1..C6 rodando com a ESP virtual deve ser APROVADO.
file(GLOB CENARIOS ${CMAKE_CURRENT_SOURCE_DIR}/cenarios/C*.txt)
foreach(c ${CENARIOS})
  get_filename_component(nome ${c} NAME_WE)
  add_test(NAME cenario_${nome}
           COMMAND sim_bus --silencioso --cenario ${c} --csv ${CMAKE_CURRENT_BINARY_DIR}/${nome}.csv
                   --eventos ${CMAKE_CURRENT_BINARY_DIR}/${nome}_eventos.csv)
endforeach()

# Testes negativos: com falha injetada o verificador TEM que reprovar (saída 1).
add_test(NAME falha_comando_lento_reprova
         COMMAND sim_bus --silencioso --falha-atraso-comando 50
                 --cenario ${CMAKE_CURRENT_SOURCE_DIR}/cenarios/C4_comando_usuario.txt)
add_test(NAME falha_sem_alarme_reprova
         COMMAND sim_bus --silencioso --falha-ignora-painel
                 --cenario ${CMAKE_CURRENT_SOURCE_DIR}/cenarios/C2_violacao_painel.txt)
add_test(NAME falha_sem_bateria_reprova
         COMMAND sim_bus --silencioso --falha-sem-bateria
                 --cenario ${CMAKE_CURRENT_SOURCE_DIR}/cenarios/C3_bateria.txt)
set_tests_properties(falha_comando_lento_reprova falha_sem_alarme_reprova falha_sem_bateria_reprova
                     PROPERTIES WILL_FAIL TRUE)

# Enlace UDP de verdade (dois processos), em tempo real.
find_package(Python3 COMPONENTS Interpreter)
if(Python3_FOUND)
  add_test(NAME enlace_udp_dois_processos
           COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/testes/teste_udp.py
                   $<TARGET_FILE:esp_virtual> $<TARGET_FILE:sim_bus>
                   ${CMAKE_CURRENT_SOURCE_DIR}/testes/cenario_curto.txt)
  # Ponte completa até o adaptador do CoppeliaSim, com um CoppeliaSim falso.
  add_test(NAME ponte_coppelia_simulador_falso
           COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/testes/teste_ponte_coppelia.py
                   ${CMAKE_CURRENT_SOURCE_DIR}/coppelia/ponte_coppelia.py $<TARGET_FILE:sim_bus>
                   ${CMAKE_CURRENT_SOURCE_DIR}/testes/cenario_curto.txt)
  # Teste extenso (conferencia fisica + tempo) com a cena falsa: prova a ferramenta.
  add_test(NAME teste_extenso_cena_falsa
           COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/ferramentas/teste_extenso.py --falso
                   --sem-grafico --cenario ${CMAKE_CURRENT_SOURCE_DIR}/testes/cenario_curto.txt
                   --saida ${CMAKE_CURRENT_BINARY_DIR}/teste_extenso)
  set_tests_properties(enlace_udp_dois_processos ponte_coppelia_simulador_falso teste_extenso_cena_falsa
                       PROPERTIES TIMEOUT 120)
endif()
