function init()
    _G.prepararGeracaoMapa = prepararGeracaoMapa
    _G.salvarMapaGerado = salvarMapaGerado
    _G.iniciarVarredura = iniciarVarredura

    if commandEnv then
        commandEnv.prepararGeracaoMapa = prepararGeracaoMapa
        commandEnv.salvarMapaGerado = salvarMapaGerado
        commandEnv.iniciarVarredura = iniciarVarredura
    end

    print("=== GERADOR DE MAPA INSTALADO ===")
    print("1) No client, rode prepararGeracaoMapa()")
    print("2) No jogo, use /scanmap start")
    print("3) Ao terminar, rode salvarMapaGerado() no client")
end

function prepararGeracaoMapa()
    if g_minimap and g_minimap.clean then
        g_minimap.clean()
    end

    print("Cache de minimap limpo. Agora use /scanmap start no jogo.")
end

function salvarMapaGerado()
    if not g_minimap or not g_minimap.saveOtmm then
        print("Erro: g_minimap.saveOtmm nao esta disponivel.")
        return
    end

    g_minimap.saveOtmm('/minimap.otmm')
    print("Minimap salvo em /minimap.otmm.")
end

function iniciarVarredura()
    prepararGeracaoMapa()
    print("A varredura agora e feita pelo servidor. Use /scanmap start no jogo.")
end

function terminate()
    _G.prepararGeracaoMapa = nil
    _G.salvarMapaGerado = nil
    _G.iniciarVarredura = nil

    if commandEnv then
        commandEnv.prepararGeracaoMapa = nil
        commandEnv.salvarMapaGerado = nil
        commandEnv.iniciarVarredura = nil
    end
end
