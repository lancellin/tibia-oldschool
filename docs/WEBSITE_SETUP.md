# Website (MyAAC) — setup e operação

Site de criação de contas/personagens do Tibia Oldschool 7.72, no ar em
`http://127.0.0.1` (porta 80). Substituiu o fluxo in-game do Account Manager
(conta 1/1 + NPC Account Clerk), desativado na v0.2.5 por não escalar com
vários jogadores criando conta ao mesmo tempo.

## Stack (portátil, em D:\tibia-dev-tools)

| Componente | Versão | Caminho |
|---|---|---|
| PHP | 8.3.33 x64 Thread Safe | `D:\tibia-dev-tools\php-8.3.33` (php.ini próprio) |
| nginx | 1.30.4 | `D:\tibia-dev-tools\nginx-1.30.4` (conf próprio, porta 80, fastcgi 127.0.0.1:9123) |
| MyAAC | v1.9.3 | `D:\tibia-oldschool\website` |

Scripts: `D:\tibia-dev-tools\Start-Website.ps1` (sobe php-cgi + nginx) e
`Stop-Website.ps1`. Não são serviços do Windows: se a máquina reiniciar,
rodar o Start de novo.

Dependências do MyAAC instaladas sem Node/Composer-global:
- `composer.phar` em `D:\tibia-dev-tools\php-8.3.33\composer.phar`
  (`php composer.phar install --no-dev --working-dir=D:\tibia-oldschool\website`);
- assets frontend (bootstrap/jquery/jquery-ui/tinymce) em `website\tools\ext`,
  montados manualmente a partir dos tgz do registry npm
  (script de referência: `D:\tibia-dev-tools\temp\setup-myaac-ext.ps1`).

## Banco

Mesmo banco do servidor (`server\config.lua`): o MyAAC lê as credenciais e o
schema direto do config.lua no instalador. Cria as tabelas `myaac_*` e a
coluna `players.created` (migração própria). Não mexe nas tabelas de jogo.

## Configurações aplicadas (myaac_settings, plugin core)

- `character_samples = 0=Rook Sample` (personagem novo copia o Rook Sample:
  vocação 0, level 1; sem escolha de vocação no site)
- `character_towns = 11` (Rookgaard; personagens nascem com pos 0,0,0 e o
  servidor resolve para o templo da town no login — iologindata.cpp)
- `create_character_name_max_length = 29` (mínimo 3 já é default; paridade
  com o antigo clerk)
- prefixos/nomes bloqueados incluem `account manager`, `character manager` e
  os nomes dos samples
- `date_timezone = America/Sao_Paulo`, `anonymous_usage_statistics = false`
- e-mail desligado (sem verificação de conta por e-mail)

Toda validação (nomes, senhas, unicidade) é server-side no MyAAC
(`system/src/Validator.php`, `CreateCharacter.php`) — o HTML é só formulário;
editar o frontend não bypassa nada.

## Segurança

- Instalador bloqueado: `install/ip.txt` ausente (desabilita p/ todos) +
  `install/install.lock` presente. `/install/` só exibe aviso.
- `config.local.php` (credenciais) é PHP executado pelo nginx — não é servido
  como texto; `website/` está no .gitignore para nunca subir credencial.
- CSRF protection ativa (default).
- Admin panel em `/admin` — conta admin pré-existente no banco ("Guilherme",
  do experimento anterior ao site; type 6).

## Jogadores

- Criar conta/personagem: site (conta numérica no padrão clássico).
- Entrar no jogo: client normal, login clássico na porta 7171 (o site não
  faz login de jogo; são fluxos separados).
- Para acesso externo, liberar a porta 80 no firewall/roteador da máquina
  (mesma providência já necessária para 7171/7172).

## Reinstalar/atualizar o MyAAC

Baixar o release em github.com/slawkens/myaac, extrair por cima de
`website/`, rodar composer install + montar `tools/ext` de novo, limpar
`website\system\cache`. O instalador só reabre se `install/ip.txt` for
recriado com o IP autorizado e o `install.lock` removido — fazer isso só em
manutenção, e remover os dois arquivos ao terminar.
