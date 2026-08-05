# Floor Quarantine Review

Painel web privado e somente leitura para investigar a quarentena da persistência
do chão. Ele nunca altera itens, jogadores, tiles ou estados da quarentena.

## Visões

- `/` e `/items`: agrupamento inicial por tipo de stackable, com unidades, pilhas,
  tiles, containers, fontes e atualização.
- `/items/{item_id}`: ocorrências por coordenada, com quantidade na pilha, no
  container imediato e no tile inteiro, último personagem relacionado e
  proximidade conservadora do crash.
- `/players`: resumo por último manipulador registrado, incluindo cobertura de
  GUID, itens, tiles e faixas de risco temporal.
- `/players/{guid}`: investigação do personagem por tipo de item e ocorrência,
  com filtros de fonte, risco e busca. O GUID é tratado como pista para CAM,
  nunca como prova automática de propriedade ou duplicação.
- `/quarantine`: consulta técnica por tile.
- `/quarantine/{id}`: árvore completa do snapshot e evidência original.

## Execução local

1. Crie um ambiente virtual Python 3.12.
2. Instale `requirements.txt`.
3. Copie `.env.example` para `.env` e preencha as senhas.
4. Execute `python run.py`.

Em Linux, mantenha o processo atrás de VPN e proxy HTTPS. Use um usuário MariaDB
dedicado com permissão `SELECT` apenas nas tabelas
`floor_persistence_quarantine`, `floor_persistence_quarantine_items`,
`floor_persistence_checkpoints` e nas colunas `id`/`name` de `players`.
O arquivo `sql/reader-user.sql.example` contém o modelo dessas permissões.

Registros antigos continuam visíveis, mas exibem “Manifesto ainda não disponível”
até serem materializados por uma versão nova do TFS.
