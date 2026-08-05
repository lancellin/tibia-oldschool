# NPC Keywords Clicaveis

Referencia curta para reutilizar a funcionalidade de keywords clicaveis em
falas de NPC no client `7.72`.

## Objetivo

Permitir que certas palavras da fala do NPC aparecam em azul no console e
possam ser clicadas pelo jogador para reenviar a keyword automaticamente.

Exemplo:

- fala do NPC: `Would you like to know about {basic} or {advanced} functions?`
- efeito no client: `basic` e `advanced` aparecem clicaveis;
- clique do jogador: o client envia `basic` ou `advanced` como fala normal.

## Como escrever a fala do NPC

Sempre que uma palavra ou expressao precisar ser clicavel, envolva o trecho
com chaves:

- `{basic}`
- `{advanced}`
- `{deposit}`
- `{withdraw}`
- `{yes}`
- `{no}`

Isso vale para textos enviados por `npcHandler:say(...)` ou equivalentes.

## Regra de implementacao adotada

No nosso projeto, a feature e tratada no client atual, sem alterar protocolo e
sem exigir modo antigo de chat privado de NPC.

Arquivos-base:

- `sources/otclient-redemption/modules/game_console/console.lua`
- `sources/otclient-redemption/modules/game_npctrade/controllers/npc_dialog.lua`

Logica final:

- o parser visual do console reaproveita a convencao `{keyword}`;
- a palavra destacada vira `text-event` clicavel;
- no protocolo `7.72`, o clique envia `SAY` normal, nao `NpcTo`;
- a deteccao de fala de NPC no fluxo `SAY` usa o speaker real do mapa;
- o client confirma `isNpc()`, nome e posicao antes de tratar a fala como NPC;
- falas acima da cabeca continuam limpas, sem exibir `{}` ao jogador.

## Por que essa abordagem foi escolhida

- o TFS atual envia fala de NPC como `TALKTYPE_SAY`;
- o fluxo `NpcTo/NpcFrom` legado nao e a base correta do nosso `7.72`;
- assim evitamos camada de compatibilidade e evitamos portar comportamento
  legado do RealOTX.

## Como reutilizar nos proximos NPCs

1. Escreva a resposta do NPC normalmente.
2. Marque apenas as keywords relevantes com `{}`.
3. Nao transforme a fala inteira em keywords.
4. Prefira keywords curtas e objetivas.
5. Teste no client `sources/otclient-redemption/otclient.exe`.

## Checklist rapido

- a palavra apareceu azul no console;
- o clique enviou a keyword automaticamente;
- o NPC respondeu como se o jogador tivesse digitado a keyword;
- a fala acima da cabeca nao mostrou chaves;
- nenhum erro Lua apareceu no client.
