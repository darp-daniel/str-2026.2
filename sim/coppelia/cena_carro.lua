--[[
cena_carro.lua — carro elétrico + painel de controle para o HIL do projeto STR.

Script de CUSTOMIZAÇÃO (Lua) do CoppeliaSim 4.6 ou mais novo. Como usar:
  1. Arquivo > Nova cena.
  2. Adicionar > Dummy; renomeie para  CarroSTR
  3. Botão direito no dummy > Adicionar > Script > Customization script (Lua)
     (ou deixe o ponte_coppelia.py criar tudo sozinho: ele faz isso se não
     encontrar /CarroSTR).
  4. Cole este arquivo inteiro no script. Ao fechar o editor, o carro e o
     painel aparecem. Salve a cena (ex.: sim/coppelia/carro_str.ttt).

O que tem na cena:
  - chassi de 1500 kg, 4 rodas; tração traseira por juntas em modo velocidade
    com força máxima = torque pedido (torque 0 = roda livre);
  - pista longa (2 km) e câmera que acompanha o carro;
  - 6 esferas no teto (da frente para trás):
      ignição (verde) | painel (verde ok / vermelho violado) | alarme (laranja
      piscando) | bloqueio (vermelho) | bateria (verde/amarelo/vermelho) |
      enlace com a ESP (azul = recebendo respostas)
  - janela "Carro STR" com chave de ignição, violação do painel, acelerador,
    descarga rápida e os botões de comando do usuário (vão para a ESP como T4).

A ponte (sim_bus + ponte_coppelia.py) chama str_trocar() uma vez por passo.
Sem a ponte, a cena anda sozinha (torque direto do acelerador, SEM bloqueio):
serve para conferir a cena antes de ligar a ESP.
]]

sim = require('sim')
simUI = require('simUI')

local SENTIDO = 1          -- troque para -1 se o carro andar de ré
local RAIO = 0.31          -- m (igual ao modelo C++)
local TORQUE_SEM_PONTE = 2000 -- N·m nas rodas com o pedal no fundo (modo autônomo)

-- Como o carro se move (vale ao (re)criar a cena: ponte_coppelia.py --recriar):
--  'cinematico' (padrão): anda pelas MESMAS equações do modelo 1D do sim_bus
--     (tração T/r, arrasto, rolamento; massa 1500 kg). Determinístico; o
--     CoppeliaSim mostra o carro, gira as rodas e dá a janela de controle.
--  'bullet': corpo rígido com rodas e contato no motor de física. Serve para
--     estudar, mas no 4.10 o pneu do Bullet patina 50-60 %, rola com ~4x a
--     resistência do modelo e o carro sem direção precisa de um "motorista"
--     (veja sim/README.md, limitações).
local MODO = 'cinematico'
local VEIC = {m = 1500, cd = 0.30, area = 2.2, rho = 1.2, crr = 0.012, g = 9.81} -- = ParametrosVeiculo (C++)

local C = {
    ign = false, pai = true, ped = 0.0, desc = false, camera = true, cmd = 0, motorista = true,
    ultimaTroca = -1, carroAnt = nil, ui = nil, h = {},
    v = 0.0, ang = 0.0, T = 0.0, x0 = {0, 0, 0.75}, -- estado do modo cinemático
}

-- ------------------------------------------------------------ compatibilidade
-- A ordem dos argumentos de setObjectPosition/Orientation mudou na 4.7.
local function setPos(h, p)
    if not pcall(sim.setObjectPosition, h, p, sim.handle_world) then
        sim.setObjectPosition(h, sim.handle_world, p)
    end
end
local function setOri(h, o)
    if not pcall(sim.setObjectOrientation, h, o, sim.handle_world) then
        sim.setObjectOrientation(h, sim.handle_world, o)
    end
end
local function cor(h, rgb)
    sim.setShapeColor(h, nil, sim.colorcomponent_ambient_diffuse, rgb)
end
local function existe(caminho)
    local ok, h = pcall(sim.getObject, caminho, {noError = true})
    return ok and h ~= nil and h ~= -1, h
end

-- ------------------------------------------------------------ construção
local OPT_RESPONDABLE, OPT_STATIC = 8, 16

-- O CoppeliaSim 4.10 ignora os bits respondable/static de createPrimitiveShape
-- (tudo nascia estático: o carro ficava congelado no ar). Então define de novo,
-- explicitamente, pelo parâmetro antigo e pelas propriedades do 4.8+.
local function fisica(h, opcoes)
    local resp = (opcoes or 0) & OPT_RESPONDABLE ~= 0
    local estatica = (opcoes or 0) & OPT_STATIC ~= 0
    pcall(sim.setObjectInt32Param, h, sim.shapeintparam_static, estatica and 1 or 0)
    pcall(sim.setObjectInt32Param, h, sim.shapeintparam_respondable, resp and 1 or 0)
    if sim.setBoolProperty then
        pcall(sim.setBoolProperty, h, 'dynamic', not estatica)
        pcall(sim.setBoolProperty, h, 'respondable', resp)
    end
    pcall(sim.resetDynamicObject, h)
end

-- Atrito pneu-asfalto. Com o padrão do Bullet (~0,7 x 0,7 = 0,5 combinado) as
-- rodas traseiras patinavam com o torque do C7 (teste extenso: patinagem
-- mediana de 119 %) e, sem aderência lateral, o carro escorregava para fora
-- da pista. Define em todos os motores, cada um pelo nome que ele usa.
local MU_PNEU = 1.0
local function atrito(h, mu)
    for _, nome in ipairs({'bullet_body_friction', 'ode_body_friction', 'newton_body_staticfriction',
                           'newton_body_kineticfriction'}) do
        if sim[nome] then pcall(sim.setEngineFloatParam, sim[nome], h, mu) end
    end
    if sim.setFloatProperty then
        for _, nome in ipairs({'bullet.friction', 'ode.friction', 'newton.staticFriction',
                               'newton.kineticFriction'}) do
            pcall(sim.setFloatProperty, h, nome, mu)
        end
    end
    if sim.setVector3Property then pcall(sim.setVector3Property, h, 'mujoco.friction', {mu, 0.005, 0.0001}) end
    pcall(sim.resetDynamicObject, h)
end

-- Peças do carro não colidem entre si, só com a pista. A máscara padrão
-- (0xffff) faz peças do mesmo carro colidirem; com a margem de colisão do
-- Bullet a roda raspava no chassi e freava o carro (teste extenso: 42 -> 13
-- km/h em 4 s sem torque, contra 57 -> 54 no modelo). 0xff00 = só a parte global.
local function semColisaoInterna(h)
    pcall(sim.setObjectInt32Param, h, sim.shapeintparam_respondable_mask, 0xff00)
    if sim.setIntProperty then pcall(sim.setIntProperty, h, 'respondableMask', 0xff00) end
    pcall(sim.resetDynamicObject, h)
end

local function forma(tipo, tam, pos, rgb, massa, nome, opcoes)
    local h = sim.createPrimitiveShape(tipo, tam, opcoes)
    fisica(h, opcoes)
    setPos(h, pos)
    if rgb then cor(h, rgb) end
    if massa then sim.setShapeMass(h, massa) end
    sim.setObjectAlias(h, nome)
    return h
end

local function roda(chassi, nome, x, y, motriz)
    local d, l = 2 * RAIO, 0.22
    local cin = MODO == 'cinematico'
    local modo_j = cin and (sim.jointmode_kinematic or sim.jointmode_passive) or sim.jointmode_dynamic
    local j = sim.createJoint(sim.joint_revolute, modo_j, 0, {0.15, 0.05})
    setPos(j, {x, y, RAIO})
    setOri(j, {-math.pi / 2, 0, 0})       -- eixo z da junta = +y do carro
    sim.setObjectAlias(j, 'junta_' .. nome)
    if cin then
        -- girada por setJointPosition a cada passo
    elseif motriz then
        -- Modo força/torque (4.5+): a junta aplica exatamente o torque pedido.
        -- No modo velocidade o motor segue um perfil até a velocidade-alvo e,
        -- com o alvo mudando a cada passo, segurava a roda em vez de empurrar.
        local ok = sim.jointdynctrl_force ~= nil and
                   pcall(sim.setObjectInt32Param, j, sim.jointintparam_dynctrlmode, sim.jointdynctrl_force)
        if not ok then
            sim.setObjectInt32Param(j, sim.jointintparam_dynctrlmode, sim.jointdynctrl_velocity)
            pcall(sim.setObjectInt32Param, j, sim.jointintparam_motor_enabled, 1)
            sim.setJointTargetVelocity(j, 0)
        end
        sim.setJointTargetForce(j, 0)
    else
        if not pcall(sim.setObjectInt32Param, j, sim.jointintparam_dynctrlmode, sim.jointdynctrl_free) then
            sim.setObjectInt32Param(j, sim.jointintparam_dynctrlmode, sim.jointdynctrl_velocity)
            sim.setJointTargetForce(j, 0)
        end
    end
    local r = forma(sim.primitiveshape_cylinder, {d, d, l}, {x, y, RAIO}, {0.1, 0.1, 0.1}, 20,
                    'roda_' .. nome, cin and OPT_STATIC or OPT_RESPONDABLE)
    atrito(r, MU_PNEU)
    semColisaoInterna(r)
    setOri(r, {-math.pi / 2, 0, 0})
    sim.setObjectParent(r, j, true)
    sim.setObjectParent(j, chassi, true)
    return j
end

local function construirCarro()
    local ja, h = existe('/CarroEV')
    if ja then return h end

    -- pista de 2 km ao longo de +x (o piso padrão da cena tem só 5 m)
    if not existe('/Pista') then
        local pista = forma(sim.primitiveshape_cuboid, {2000, 12, 0.1}, {995, 0, -0.049}, {0.35, 0.35, 0.38}, nil,
                            'Pista', OPT_RESPONDABLE + OPT_STATIC)
        atrito(pista, MU_PNEU)
        -- faixas brancas a cada 10 m e marcos coloridos a cada 100 m: sem elas,
        -- com a câmera seguindo o carro, a pista lisa parece não se mover
        local faixas = {}
        for x = 0, 1990, 10 do
            local largo = (x % 100 == 0)
            local f = forma(sim.primitiveshape_cuboid, {largo and 0.6 or 3, largo and 12 or 0.15, 0.01},
                            {x, 0, 0.006}, largo and {0.95, 0.75, 0.1} or {0.95, 0.95, 0.95}, nil, 'faixa',
                            OPT_STATIC)
            faixas[#faixas + 1] = f
        end
        local ok, grupo = pcall(sim.groupShapes, faixas) -- 1 objeto em vez de 200
        local marcas = ok and grupo or faixas[1]
        if ok then sim.setObjectAlias(grupo, 'Faixas') end
        sim.setObjectParent(marcas, pista, true)
    end

    local chassi = forma(sim.primitiveshape_cuboid, {4.2, 1.8, 0.5}, {0, 0, 0.75}, {0.10, 0.35, 0.75}, 1500,
                         'CarroEV', MODO == 'cinematico' and OPT_STATIC or OPT_RESPONDABLE)
    semColisaoInterna(chassi)
    -- visual: estática presa ao chassi (dinâmica solta cairia do carro)
    local cabine = forma(sim.primitiveshape_cuboid, {2.0, 1.6, 0.45}, {-0.3, 0, 1.22}, {0.75, 0.85, 0.95}, nil,
                         'cabine', OPT_STATIC)
    sim.setObjectParent(cabine, chassi, true)

    roda(chassi, 'TE', -1.35, 1.05, true)
    roda(chassi, 'TD', -1.35, -1.05, true)
    roda(chassi, 'DE', 1.35, 1.05, false)
    roda(chassi, 'DD', 1.35, -1.05, false)

    local nomes = {'led_ignicao', 'led_painel', 'led_alarme', 'led_bloqueio', 'led_bateria', 'led_enlace'}
    for i, n in ipairs(nomes) do
        local led = forma(sim.primitiveshape_spheroid, {0.22, 0.22, 0.22}, {0.55 - (i - 1) * 0.3, 0, 1.58},
                          {0.3, 0.3, 0.3}, nil, n, OPT_STATIC)
        sim.setObjectParent(led, chassi, true)
    end
    return chassi
end

-- A câmera da vista principal muda de nome entre versões: tenta pelo nome e,
-- se não achar, pega a primeira câmera da cena.
local function acharCamera()
    local ok, cam = existe('/DefaultCamera')
    if ok then return cam end
    for _, tipo in ipairs({'sceneobject_camera', 'object_camera_type'}) do
        if sim[tipo] then
            local okL, lista = pcall(sim.getObjectsInTree, sim.handle_scene, sim[tipo])
            if okL and lista and #lista > 0 then return lista[1] end
        end
    end
    return nil
end

local function pegarHandles()
    local h = C.h
    h.chassi = sim.getObject('/CarroEV')
    h.te = sim.getObject('/CarroEV/junta_TE')
    h.td = sim.getObject('/CarroEV/junta_TD')
    h.de = sim.getObject('/CarroEV/junta_DE')
    h.dd = sim.getObject('/CarroEV/junta_DD')
    C.x0 = sim.getObjectPosition(h.chassi, sim.handle_world)
    for _, n in ipairs({'ignicao', 'painel', 'alarme', 'bloqueio', 'bateria', 'enlace'}) do
        h[n] = sim.getObject('/CarroEV/led_' .. n)
    end
    h.camera = acharCamera()
    local ok, modo = pcall(sim.getObjectInt32Param, h.te, sim.jointintparam_dynctrlmode)
    C.modo_forca = ok and sim.jointdynctrl_force ~= nil and modo == sim.jointdynctrl_force
end

-- ------------------------------------------------------------ interface

local XML = [[
<ui title="STR - Telemetria Veicular"
    closeable="false"
    resizable="true"
    placement="relative"
    position="20,20"
    layout="vbox">

    <label
        text="STR | TELEMETRIA VEICULAR"
        style="font-size:20px;font-weight:bold;color:#2563eb;"/>

    <label text="ESP32 | FreeRTOS | CoppeliaSim"/>

    <group layout="vbox">

        <label text="TELEMETRIA DO VEICULO"
            style="font-size:14px;font-weight:bold;"/>

        <label id="20"
            text="Aguardando dados do veiculo..."
            word-wrap="true"
            style="font-size:14px;padding:8px;"/>

    </group>

    <group layout="vbox">

        <label text="ESTADO DO SISTEMA"
            style="font-size:14px;font-weight:bold;"/>

        <label id="30" text="Ignicao: DESLIGADA"/>
        <label id="31" text="Painel: CONFORME"/>
        <label id="32" text="Alarme: SEM DADOS"/>
        <label id="33" text="Bloqueio: SEM DADOS"/>
        <label id="34" text="Bateria: SEM DADOS"/>

        <label id="35"
            text="ESP32: DESCONECTADO"
            style="font-weight:bold;color:#dc2626;"/>

        <label id="21"
            text="Aguardando comunicacao..."
            word-wrap="true"/>

    </group>

    <group layout="vbox">

        <label text="CONTROLE DA SIMULACAO"
            style="font-size:14px;font-weight:bold;"/>

        <checkbox id="1"
            text="Ignicao ligada"
            on-change="ui_ignicao"/>

        <checkbox id="2"
            text="Painel conforme"
            on-change="ui_painel"/>

        <checkbox id="4"
            text="Descarga rapida da bateria"
            on-change="ui_descarga"/>

        <checkbox id="5"
            text="Camera acompanha o carro"
            on-change="ui_camera"/>

        <label id="36" text="Acelerador: 0%"/>

        <hslider id="3"
            minimum="0"
            maximum="100"
            on-change="ui_pedal"/>

    </group>

    <group layout="vbox">

        <label text="COMANDOS DO USUARIO - T4"
            style="font-size:14px;font-weight:bold;"/>

        <group layout="hbox" flat="true">

            <button id="10"
                text="BLOQUEAR"
                on-click="ui_bloquear"
                style="background-color:#b91c1c;color:white;padding:8px;"/>

            <button id="11"
                text="DESBLOQUEAR"
                on-click="ui_desbloquear"
                style="background-color:#15803d;color:white;padding:8px;"/>

            <button id="12"
                text="ATUALIZAR"
                on-click="ui_atualizar"
                style="background-color:#2563eb;color:white;padding:8px;"/>

        </group>

    </group>

    <group layout="vbox">

        <label text="MONITORAMENTO TEMPORAL"
            style="font-size:14px;font-weight:bold;"/>

        <label text="Deadline T4: 30 ms"/>
        <label id="40" text="Ultima resposta: -- ms"/>
        <label id="41" text="Tempo maximo: -- ms"/>
        <label id="42" text="Comandos confirmados: --"/>
        <label id="43" text="Violacoes de deadline: --"/>

    </group>

    <label
        text="Universidade de Brasilia - Sistemas de Tempo Real"
        word-wrap="true"
        style="font-size:10px;color:#64748b;"/>

</ui>
]]


local function sincronizarUi()
    if not C.ui then return end
    simUI.setCheckboxValue(C.ui, 1, C.ign and 2 or 0, true)
    simUI.setCheckboxValue(C.ui, 2, C.pai and 2 or 0, true)
    simUI.setSliderValue(C.ui, 3, math.floor(C.ped * 100 + 0.5), true)
    simUI.setCheckboxValue(C.ui, 4, C.desc and 2 or 0, true)
    simUI.setCheckboxValue(C.ui, 5, C.camera and 2 or 0, true)
end

function ui_ignicao(ui, id, v) C.ign = (v ~= 0) end
function ui_painel(ui, id, v) C.pai = (v ~= 0) end
function ui_pedal(ui, id, v) C.ped = v / 100 end
function ui_descarga(ui, id, v) C.desc = (v ~= 0) end
function ui_camera(ui, id, v) C.camera = (v ~= 0) end
function ui_bloquear() C.cmd = 1 end
function ui_desbloquear() C.cmd = 2 end
function ui_atualizar() C.cmd = 3 end

-- ------------------------------------------------------------ física/atuação
local function aplicarTorque(T)
    local h = C.h
    if MODO == 'cinematico' then
        C.T = T -- aplicado em passoCinematico, dentro do passo da simulação
        return
    end
    local lin = sim.getObjectVelocity(h.chassi)
    local w_carro = math.sqrt(lin[1] ^ 2 + lin[2] ^ 2) / RAIO
    if T < 0 and w_carro < 0.5 then T = 0 end -- regeneração não dá ré
    for _, j in ipairs({h.te, h.td}) do
        if C.modo_forca then
            sim.setJointTargetForce(j, SENTIDO * T / 2) -- torque com sinal, por roda
        else
            -- modo velocidade (CoppeliaSim antigo): alvo alto, quem limita é a força
            sim.setJointTargetVelocity(j, T > 0 and SENTIDO * 300 or 0)
            sim.setJointTargetForce(j, math.abs(T) / 2)
        end
    end
end

local function velocidadeCarro()
    if MODO == 'cinematico' then return C.v end
    local lin = sim.getObjectVelocity(C.h.chassi)
    return math.sqrt(lin[1] ^ 2 + lin[2] ^ 2)
end

local function wRoda()
    if MODO == 'cinematico' then return C.v / RAIO end
    return SENTIDO * (sim.getJointVelocity(C.h.te) + sim.getJointVelocity(C.h.td)) / 2
end

-- Mesmo integrador de ModeloVeiculo::passo (sim/nucleo/modelos.cpp).
local function passoCinematico(dt)
    local h, v = C.h, C.v
    local F = C.T / RAIO - 0.5 * VEIC.rho * VEIC.cd * VEIC.area * v * v
              - (v > 0 and VEIC.crr * VEIC.m * VEIC.g or 0)
    C.v = math.max(0, v + F / VEIC.m * dt) -- o atrito para o carro, não o empurra para trás
    local p = sim.getObjectPosition(h.chassi, sim.handle_world)
    setPos(h.chassi, {p[1] + C.v * dt, C.x0[2], C.x0[3]})
    C.ang = (C.ang + C.v / RAIO * dt) % (2 * math.pi)
    for _, j in ipairs({h.te, h.td, h.de, h.dd}) do sim.setJointPosition(j, SENTIDO * C.ang) end
end

local function reiniciarCinematico()
    if MODO ~= 'cinematico' then return end
    C.v, C.ang, C.T = 0.0, 0.0, 0.0
    setPos(C.h.chassi, C.x0)
    for _, j in ipairs({C.h.te, C.h.td, C.h.de, C.h.dd}) do sim.setJointPosition(j, 0) end
end

local CINZA, VERDE, VERMELHO = {0.3, 0.3, 0.3}, {0.1, 0.9, 0.1}, {0.95, 0.1, 0.1}
local LARANJA, AMARELO, AZUL = {1.0, 0.55, 0.0}, {1.0, 0.9, 0.0}, {0.1, 0.4, 1.0}

local function atualizarLeds(s)
    local h = C.h
    cor(h.ignicao, C.ign and VERDE or CINZA)
    cor(h.painel, C.pai and VERDE or VERMELHO)
    local pisca = math.floor(sim.getSimulationTime() * 4) % 2 == 0
    cor(h.alarme, (s.alarme == 1 and pisca) and LARANJA or CINZA)
    cor(h.bloqueio, s.bloqueio == 1 and VERMELHO or CINZA)
    local nb = s.nivel or 0
    cor(h.bateria, nb == 2 and VERMELHO or (nb == 1 and AMARELO or VERDE))
    cor(h.enlace, s.tem_rx == 1 and AZUL or CINZA)
end

local MOTIVO = {[0] = 'nenhum', [1] = 'comando do usuario', [2] = 'violacao do painel', [3] = 'bateria critica'}
local NIVEL = {[0] = 'normal', [1] = 'BAIXA', [2] = 'CRITICA'}


local function atualizarTexto(s)

    if not C.ui then return end

    -- Telemetria do veiculo

    local l1 = string.format(
        'Velocidade: %.1f km/h\n' ..
        'Torque: %.0f N.m\n' ..
        'Bateria: %.1f %%\n' ..
        'Tensao: %.2f V\n' ..
        'Corrente: %.0f A',
        velocidadeCarro() * 3.6,
        s.torque or 0,
        (s.soc or 0) * 100,
        s.v_pino or 0,
        s.corrente or 0
    )

    simUI.setLabelText(C.ui, 20, l1)

    -- Estados das entradas simuladas

    simUI.setLabelText(
        C.ui, 30,
        'Ignicao: ' ..
        (C.ign and 'LIGADA' or 'DESLIGADA')
    )

    simUI.setLabelText(
        C.ui, 31,
        'Painel: ' ..
        (C.pai and 'CONFORME' or 'VIOLADO')
    )

    simUI.setLabelText(
        C.ui, 36,
        string.format('Acelerador: %.0f %%', C.ped * 100)
    )

    -- Estados recebidos do ESP32

    if s.tem_rx == 1 then

        simUI.setLabelText(
            C.ui, 32,
            'Alarme: ' ..
            (s.alarme == 1 and 'ATIVO' or 'INATIVO')
        )

        simUI.setLabelText(
            C.ui, 33,
            'Bloqueio: ' ..
            (s.bloqueio == 1 and 'ATIVO' or 'INATIVO')
        )

        simUI.setLabelText(
            C.ui, 34,
            'Bateria: ' ..
            (NIVEL[s.nivel] or 'DESCONHECIDO')
        )

        simUI.setLabelText(
            C.ui, 35,
            'ESP32: RECEBENDO DADOS'
        )

        simUI.setLabelText(
            C.ui, 21,
            'Motivo do bloqueio: ' ..
            (MOTIVO[s.motivo] or '?') ..
            '\n' .. (s.status or '')
        )

    else

        simUI.setLabelText(C.ui, 32, 'Alarme: SEM DADOS')
        simUI.setLabelText(C.ui, 33, 'Bloqueio: SEM DADOS')
        simUI.setLabelText(C.ui, 34, 'Bateria: SEM DADOS')
        simUI.setLabelText(C.ui, 35, 'ESP32: DESCONECTADO')

        simUI.setLabelText(
            C.ui, 21,
            'Aguardando resposta do ESP32...'
        )

    end

end

-- Chamada pela ponte (ponte_coppelia.py) uma vez por passo, ANTES de sim.step().
-- Ajustes para o diagnóstico: str_config({motorista = 0|1}); devolve o estado.
function str_config(c)
    c = c or {}
    if c.motorista ~= nil then C.motorista = c.motorista ~= 0 end
    return {motorista = C.motorista and 1 or 0, modo_forca = C.modo_forca and 1 or 0, modo = MODO}
end

function str_trocar(s)
    s = s or {}
    if s.f_ign and s.f_ign >= 0 then C.ign = s.f_ign > 0.5 end
    if s.f_pai and s.f_pai >= 0 then C.pai = s.f_pai > 0.5 end
    if s.f_ped and s.f_ped > -2 then C.ped = s.f_ped end
    if (s.f_ign and s.f_ign >= 0) or (s.f_pai and s.f_pai >= 0) or (s.f_ped and s.f_ped > -2) then
        sincronizarUi()
    end
    C.ultimaTroca = sim.getSystemTime()
    aplicarTorque(s.torque or 0)
    atualizarLeds(s)
    atualizarTexto(s)
    local cmd = C.cmd
    C.cmd = 0
    local p = sim.getObjectPosition(C.h.chassi, sim.handle_world) -- para o teste extenso
    return {
        ign = C.ign and 1 or 0, pai = C.pai and 1 or 0, ped = C.ped, desc = C.desc and 1 or 0,
        w = wRoda(), vel = velocidadeCarro(), cmd = cmd,
        t = sim.getSimulationTime(), -- evita uma chamada ZeroMQ a mais por passo
        x = p[1], y = p[2], z = p[3],
    }
end

-- ------------------------------------------------------------ callbacks
function sysCall_init()
    construirCarro()
    pegarHandles()
    C.ui = simUI.create(XML)
    C.camera = true
    sincronizarUi()
end

function sysCall_cleanup()
    if C.ui then simUI.destroy(C.ui) end
end

-- cada simulação começa com o carro parado na largada
function sysCall_beginSimulation() reiniciarCinematico() end
function sysCall_afterSimulation() reiniciarCinematico() end

function sysCall_actuation()
    local h = C.h
    -- Modo autônomo: sem a ponte há mais de 0,5 s, o acelerador age direto.
    if C.ultimaTroca < 0 or sim.getSystemTime() - C.ultimaTroca > 0.5 then
        local T = C.ign and C.ped * TORQUE_SEM_PONTE or 0
        aplicarTorque(T)
        atualizarLeds({alarme = 0, bloqueio = 0, nivel = 0, tem_rx = 0})
        if C.ui then
            simUI.setLabelText(C.ui, 20, string.format('Velocidade %.1f km/h (cena sem a ponte)', velocidadeCarro() * 3.6))
            simUI.setLabelText(C.ui, 21, 'Ponte desligada: sem ESP, sem bloqueio.')
        end
    end
    if MODO == 'cinematico' then
        passoCinematico(sim.getSimulationTimeStep())
        return
    end

    -- arrasto aerodinâmico (o motor de física não tem): F = -1/2 rho Cd A |v| v
    local lin = sim.getObjectVelocity(h.chassi)
    local k = 0.5 * 1.2 * 0.30 * 2.2
    local vh = math.sqrt(lin[1] ^ 2 + lin[2] ^ 2)
    sim.addForceAndTorque(h.chassi, {-k * vh * lin[1], -k * vh * lin[2], 0}, nil)

    -- Motorista virtual: o carro não tem direção, e qualquer assimetria no
    -- contato das rodas o desvia (sem isto ele saiu 37 m da faixa e caiu da
    -- pista). Ele ESTERÇA, como quem segura o volante: mira um ponto no centro
    -- da pista alguns metros à frente e corrige só o rumo (torque de guinada).
    -- Empurrar o chassi de lado arrastava os pneus e freava o carro.
    if C.motorista then
        local p = sim.getObjectPosition(h.chassi, sim.handle_world)
        local m = sim.getObjectMatrix(h.chassi, sim.handle_world)
        local guinada = math.atan(m[5], m[1])            -- eixo x do carro no plano
        local _, ang = sim.getObjectVelocity(h.chassi)
        local olhar = math.max(8.0, 1.5 * vh)             -- distância do ponto mirado (m)
        local rumo = math.atan(-p[2], olhar)              -- rumo que leva de volta ao centro
        local Iz = 2600                                   -- inércia de guinada (aprox.)
        local Tz = Iz * (-4.0 * (guinada - rumo) - 4.0 * ang[3]) -- wn = 2 rad/s
        sim.addForceAndTorque(h.chassi, nil, {0, 0, Tz})
    end

end

function sysCall_sensing()
    -- câmera acompanha o carro (desloca junto, mantendo o ângulo escolhido)
    local h = C.h
    if not (C.camera and h.camera) then
        C.carroAnt = nil
        return
    end
    local p = sim.getObjectPosition(h.chassi, sim.handle_world)
    if C.carroAnt then
        local c = sim.getObjectPosition(h.camera, sim.handle_world)
        setPos(h.camera, {c[1] + p[1] - C.carroAnt[1], c[2] + p[2] - C.carroAnt[2], c[3]})
    end
    C.carroAnt = p
end

function sysCall_afterSimulation()
    C.carroAnt = nil
    C.ultimaTroca = -1
    atualizarLeds({alarme = 0, bloqueio = 0, nivel = 0, tem_rx = 0})
end
