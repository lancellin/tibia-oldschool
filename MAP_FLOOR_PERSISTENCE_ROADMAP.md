# Roadmap da persistência incremental do chão

## Estado das etapas

1. Classificação, política e `instance_id`: concluída.
2. Dirty tracking e containers: concluída.
3. Snapshots incrementais e checkpoints coordenados: concluída.
4. Seleção e bloqueio de recuperação: concluída.
5. Recuperação, reconciliação, quarentena e replay: concluída.
6. Server save diário e encerramento controlado: concluída.
7. Weekly reset manual: concluída.
8. Emergency manual: concluída e aprovada em teste.
9. Observabilidade, desempenho e testes de carga: parcialmente concluída e
   aprovada nos testes locais. Foram validados 0, 1 e 5 jogadores, incluindo
   movimento em cadeia e combate simultâneo com arqueiros, sem crescimento de
   memória ou custo relevante no TFS. Escala de 50 a 200 jogadores permanece
   para o closed beta porque clients completos excedem a RAM da máquina local.
10. Preparação e validação para produção: não iniciada.

## Pontos conscientemente adiados

- Medir os pacotes extras da CAM forense enviados pelo TFS a um personagem
  completamente parado, incluindo frequência, bytes por segundo e repetição de
  evidências quando há muitos itens visíveis no chão. Confirmar que itens já
  conhecidos não são retransmitidos continuamente sem mudança de estado.
- Gatilho automático do emergency baseado em ausência global de pacotes. Deve
  diferenciar queda real de conexão, startup, server save, manutenção e servidor
  de testes naturalmente vazio. Não usar apenas `players_online == 0`.
- Integração do `floor_last_actor_guid` com Player Shop. Uma compra concluída
  deverá atribuir ao comprador o GUID investigativo dos itens realmente
  entregues, incluindo containers e conteúdo conforme a política já usada em
  trade e mail. Operações recusadas ou revertidas não podem alterar o GUID.
- Containers fixos do OTBM preenchidos por jogadores.
- Reconciliação de houses e market.
- Recuperação administrativa de corpses por comando.
- Lista especial de rares para investigação.
- Decisão, devolução ou descarte de quarentena pela interface web.
