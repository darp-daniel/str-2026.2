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

local C = {
    ign = false, pai = true, ped = 0.0, desc = false, camera = true, cmd = 0, motorista = true,
    ultimaTroca = -1, carroAnt = nil, ui = nil, h = {},
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
    local j = sim.createJoint(sim.joint_revolute, sim.jointmode_dynamic, 0, {0.15, 0.05})
    setPos(j, {x, y, RAIO})
    setOri(j, {-math.pi / 2, 0, 0})       -- eixo z da junta = +y do carro
    sim.setObjectAlias(j, 'junta_' .. nome)
    if motriz then
        sim.setObjectInt32Param(j, sim.jointintparam_dynctrlmode, sim.jointdynctrl_velocity)
        pcall(sim.setObjectInt32Param, j, sim.jointintparam_motor_enabled, 1)
        sim.setJointTargetVelocity(j, 0)
        sim.setJointTargetForce(j, 0)
    else
        if not pcall(sim.setObjectInt32Param, j, sim.jointintparam_dynctrlmode, sim.jointdynctrl_free) then
            sim.setObjectInt32Param(j, sim.jointintparam_dynctrlmode, sim.jointdynctrl_velocity)
            sim.setJointTargetForce(j, 0)
        end
    end
    local r = forma(sim.primitiveshape_cylinder, {d, d, l}, {x, y, RAIO}, {0.1, 0.1, 0.1}, 20,
                    'roda_' .. nome, OPT_RESPONDABLE)
    atrito(r, MU_PNEU)
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
                         'CarroEV', OPT_RESPONDABLE)
    -- visual: estática presa ao chassi (dinâmica solta cairia do carro)
    local cabine = forma(sim.primitiveshape_cuboid, {2.0, 1.6, 0.45}, {-0.3, 0, 1.22}, {0.75, 0.85, 0.95}, nil,
                         'cabine', OPT_STATIC)
    sim.setObjectParent(cabine, chassi, true)

    roda(chassi, 'TE', -1.35, 1.02, true)
    roda(chassi, 'TD', -1.35, -1.02, true)
    roda(chassi, 'DE', 1.35, 1.02, false)
    roda(chassi, 'DD', 1.35, -1.02, false)

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
    for _, n in ipairs({'ignicao', 'painel', 'alarme', 'bloqueio', 'bateria', 'enlace'}) do
        h[n] = sim.getObject('/CarroEV/led_' .. n)
    end
    h.camera = acharCamera()
end

-- ------------------------------------------------------------ interface
local XML = [[
<ui title="Carro STR" closeable="false" resizable="true" placement="relative" position="20,20" layout="vbox">
    <label text="&lt;b&gt;Sensores do carro (vao para a ESP)&lt;/b&gt;"/>
    <checkbox id="1" text="Ignicao ligada" on-change="ui_ignicao"/>
    <checkbox id="2" text="Painel conforme (desmarque = violacao)" on-change="ui_painel"/>
    <label text="Acelerador"/>
    <hslider id="3" minimum="0" maximum="100" on-change="ui_pedal"/>
    <checkbox id="4" text="Descarga rapida da bateria" on-change="ui_descarga"/>
    <checkbox id="5" text="Camera acompanha o carro" on-change="ui_camera"/>
    <label text="&lt;b&gt;Comandos do usuario (T4)&lt;/b&gt;"/>
    <group layout="hbox" flat="true">
        <button id="10" text="Bloquear" on-click="ui_bloquear"/>
        <button id="11" text="Desbloquear" on-click="ui_desbloquear"/>
        <button id="12" text="Atualizar" on-click="ui_atualizar"/>
    </group>
    <label id="20" text="" wordwrap="true"/>
    <label id="21" text="" wordwrap="true"/>
    <label text="Teto, da frente p/ tras: ignicao | painel | alarme | bloqueio | bateria | enlace ESP" wordwrap="true"/>
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
    local por_roda = math.abs(T) / 2
    local alvo = 0
    if T > 0 then alvo = SENTIDO * 300 end -- velocidade alta: quem limita é a força
    for _, j in ipairs({h.te, h.td}) do
        sim.setJointTargetVelocity(j, alvo)
        sim.setJointTargetForce(j, por_roda)
    end
end

local function velocidadeCarro()
    local lin = sim.getObjectVelocity(C.h.chassi)
    return math.sqrt(lin[1] ^ 2 + lin[2] ^ 2)
end

local function wRoda()
    return SENTIDO * (sim.getJointVelocity(C.h.te) + sim.getJointVelocity(C.h.td)) / 2
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
    local l1 = string.format('Velocidade %.1f km/h   torque %.0f N.m\nBateria: SOC %.1f %%   V_pino %.2f V   %.0f A',
        velocidadeCarro() * 3.6, s.torque or 0, (s.soc or 0) * 100, s.v_pino or 0, s.corrente or 0)
    local l2
    if s.tem_rx == 1 then
        l2 = string.format('ESP: bloqueio=%d (%s)  alarme=%d  bateria %s\n%s', s.bloqueio, MOTIVO[s.motivo] or '?',
            s.alarme, NIVEL[s.nivel] or '?', s.status or '')
    else
        l2 = 'ESP: aguardando a primeira resposta...'
    end
    simUI.setLabelText(C.ui, 20, l1)
    simUI.setLabelText(C.ui, 21, l2)
end

-- Chamada pela ponte (ponte_coppelia.py) uma vez por passo, ANTES de sim.step().
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

function sysCall_actuation()
    local h = C.h
    -- arrasto aerodinâmico (o motor de física não tem): F = -1/2 rho Cd A |v| v
    local lin = sim.getObjectVelocity(h.chassi)
    local k = 0.5 * 1.2 * 0.30 * 2.2
    local vh = math.sqrt(lin[1] ^ 2 + lin[2] ^ 2)
    sim.addForceAndTorque(h.chassi, {-k * vh * lin[1], -k * vh * lin[2], 0}, nil)

    -- Motorista virtual: o carro não tem direção, e qualquer assimetria no
    -- contato das rodas o desvia (no teste extenso ele saiu 37 m da faixa e
    -- caiu da pista). Um controle PD suave faz o papel de quem segura o volante:
    -- mantém y = 0 (centro da pista) e o carro apontado para +x.
    if C.motorista then
        local p = sim.getObjectPosition(h.chassi, sim.handle_world)
        local m = sim.getObjectMatrix(h.chassi, sim.handle_world)
        local guinada = math.atan(m[5], m[1])           -- eixo x do carro no plano
        local _, ang = sim.getObjectVelocity(h.chassi)
        local M, Iz = 1580, 2600                        -- massa e inércia de guinada (aprox.)
        local Fy = M * (-1.0 * p[2] - 2.0 * lin[2])     -- wn = 1 rad/s, amortecimento crítico
        local Tz = Iz * (-4.0 * guinada - 4.0 * ang[3]) -- wn = 2 rad/s
        sim.addForceAndTorque(h.chassi, {0, Fy, 0}, {0, 0, Tz})
    end

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
