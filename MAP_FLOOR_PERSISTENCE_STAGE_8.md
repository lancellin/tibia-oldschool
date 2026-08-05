# Persistência do chão — Etapa 8 emergency manual

## Escopo

O modo emergency é exclusivamente manual e somente GOD pode acioná-lo.
Não existe gatilho automático baseado em zero jogadores.

## Gatilho automático adiado

O gatilho automático permanece conscientemente adiado.

A proposta futura é detectar uma interrupção global de pacotes recebidos pelo
servidor, possivelmente por cerca de dois segundos, em vez de observar apenas a
quantidade de jogadores online. Antes de implementar, será necessário distinguir:

- servidor de testes naturalmente vazio;
- inicialização antes da entrada dos jogadores;
- server save e encerramento controlado;
- manutenção ou fechamento administrativo;
- indisponibilidade real da conexão com jogadores anteriormente ativos.

Nenhuma dessas regras automáticas faz parte da Etapa 8 atual.

## Comandos

- `!emergency` ou `!emergency start`: ativa o modo.
- `!emergency finish`: autoriza o encerramento da emergência e inicia o clean
  save coordenado.

## Ativação

Ao ativar:

1. O servidor entra em estado fechado.
2. Jogadores comuns são desconectados e salvos pelo fluxo normal de logout.
3. GMs com `CanAlwaysLogin` permanecem conectados e podem entrar novamente.
4. Todos os itens atualmente em decay são retirados temporariamente das filas.
5. Itens cujo decay começar durante a emergência também ficam pausados.
6. Server saves automáticos e manuais são recusados.
7. `/openserver` não pode liberar o acesso comum.

O tempo restante de decay não diminui durante a pausa.

## Finalização

`!emergency finish`:

1. Retoma todos os decays pausados.
2. Acrescenta 50 minutos somente aos corpses de jogadores.
3. Não acrescenta tempo a outros itens.
4. Inicia o clean save coordenado.
5. Desconecta inclusive o GOD que confirmou a finalização.
6. Mantém o login bloqueado até o processo ser reiniciado.

Se o clean save falhar, o servidor permanece fechado e não deve ser encerrado
como se a confirmação tivesse sido concluída.

## Teste mínimo

1. Deixe online um GOD e um jogador comum.
2. Crie um corpse de jogador e um item comum com decay; anote seus tempos.
3. Execute `!emergency`.
4. Confirme que o jogador comum foi removido e não consegue retornar.
5. Confirme que o GOD permanece e consegue entrar novamente.
6. Aguarde mais que o decay normal de teste e confirme que ambos permanecem.
7. Tente `/openserver` e um `/floorsnapshot cleansave`; ambos devem ser recusados.
8. Execute `!emergency finish`.
9. Confirme no log que os decays foram retomados, somente o corpse recebeu
   `+50m` e o clean save foi confirmado.
10. Reinicie o TFS e confirme que jogadores comuns voltam a entrar.
