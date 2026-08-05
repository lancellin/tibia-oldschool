# Backup privado do projeto `C:\tibia-oldschool`

## Objetivo

Este backup foi preparado para permitir a recuperacao do projeto e do servidor mesmo em caso de perda total deste PC.

## Estado do backup em 2026-06-01

- Pasta raiz do projeto: `C:\tibia-oldschool`
- Processo do servidor verificado com `tasklist | findstr /I tfs.exe`
- Nenhum `tfs.exe` apareceu na verificacao antes do dump
- Git raiz inicializado em `C:\tibia-oldschool\.git`
- Metadados Git internos de `sources\...` renomeados temporariamente para `.git.root-backup-disabled` para permitir backup em um unico Git raiz
- Arquivo sensivel incluido no plano de versionamento: `C:\tibia-oldschool\PASSWORDALL.txt`
- Dump SQL gerado em `C:\tibia-oldschool\backups\db\oldschool772db-20260601-081706.sql`
- Zip completo gerado em `C:\tibia-oldschool\backups\full\tibia-oldschool-full-20260601-083301.zip`
- Repositorio em nuvem: `origin -> https://github.com/Lancellin/tibia-oldschool-private-backup.git`
- Visibilidade do repositorio remoto: privada

## O que entra no Git raiz

- `server` com runtime, `config.lua`, `schema.sql`, `key.pem` e `data`
- `sources\nekiro-tfs-1.5-7.72`
- `sources\otclient-redemption`
- `sources\otacademy-otclientv8`
- `sources\tibialegacyserver`
- `docs`
- `tools` utilitarios leves e scripts
- `experiments` relevantes para preservar historico tecnico
- `otclientv8-master`
- `PASSWORDALL.txt`
- arquivos da raiz necessarios para reconstrucao

## Observacao sobre `sources\...\ .git`

As pastas abaixo originalmente tinham repositorios Git proprios. Para permitir um backup centralizado no Git raiz, os diretorios `.git` internos foram renomeados temporariamente para `.git.root-backup-disabled` e ficaram ignorados pelo Git raiz:

- `sources\nekiro-tfs-1.5-7.72\.git.root-backup-disabled`
- `sources\otclient-redemption\.git.root-backup-disabled`
- `sources\otacademy-otclientv8\.git.root-backup-disabled`
- `sources\tibialegacyserver\.git.root-backup-disabled`

Se no futuro voce quiser voltar a usar esses repositorios internos separadamente, basta restaurar o nome de cada pasta para `.git`.

## O que fica fora do Git, mas continua no backup bruto

Esses itens foram ignorados no Git por serem regeneraveis, pesados ou logs, mas continuam protegidos pelo dump SQL e pelo zip completo:

- `backups`
- `builds`
- `server\*.log`
- `server\data\logs`
- `sources\otclient-redemption\build`
- `sources\otclient-redemption\RelWithDebInfo`
- `sources\otacademy-otclientv8\vcpkg_installed`
- `sources\otacademy-otclientv8\vc17\otclient\OpenGL`
- `sources\otacademy-otclientv8\tests.7z`
- `tools\vcpkg`
- `tools\vcpkg-otacademy`
- `tools\downloads`
- `tools\__pycache__`
- `experiments\tibialegacy-world-extract`
- logs de `experiments` e `otclientv8-master`

No zip completo, `backups\full` e `backups\work` ficaram fora do proprio arquivo compactado para evitar autorreferencia durante a criacao do backup.
Alguns artefatos grandes foram removidos do Git para viabilizar o push para o GitHub privado, mas continuam preservados no zip completo local.

## Observacao importante sobre privacidade

Este backup inclui credenciais e arquivos sensiveis, incluindo `PASSWORDALL.txt` e configuracoes com senha. O repositorio remoto deve ser obrigatoriamente privado antes de qualquer push.

## Restauracao em um PC novo

### Opcao 1: restauracao mais rapida pelo zip completo

1. Copiar o zip completo privado para o novo PC.
2. Extrair o conteudo para `C:\tibia-oldschool`.
3. Instalar Git e MariaDB.
4. Restaurar o banco com o dump SQL mais recente.
5. Verificar `server\config.lua`, `PASSWORDALL.txt`, `server\data\world` e demais configs locais.
6. Subir o servidor com `server\tfs.exe` ou `server\tfs.bat`.

### Opcao 2: restauracao pelo repositorio Git privado

1. Clonar o repositorio privado em `C:\tibia-oldschool`.
2. Restaurar o banco com o dump SQL mais recente salvo em backup privado.
3. Regerar os itens ignorados do Git quando necessario:
   - `builds`
   - `tools\vcpkg`
   - `tools\vcpkg-otacademy`
   - `sources\otclient-redemption\build`
   - `sources\otacademy-otclientv8\vcpkg_installed`
4. Conferir as configuracoes sensiveis.
5. Recompilar ou reutilizar binarios conforme necessidade.

## Restauracao do banco

Criar o banco e importar o dump mais recente:

```bat
"C:\Program Files\MariaDB 10.11\bin\mariadb.exe" -u root -p123456 -e "CREATE DATABASE IF NOT EXISTS oldschool772db;"
"C:\Program Files\MariaDB 10.11\bin\mariadb.exe" -u root -p123456 oldschool772db < "C:\tibia-oldschool\backups\db\oldschool772db-20260601-081706.sql"
```

## GitHub privado

Remote configurado:

- `origin`
- `https://github.com/lancellin/tibia-oldschool-private-backup.git`

Status atual:

- repositorio remoto confirmado como privado
- primeiro push do `main` concluido com o commit `2c84f0e406ccdc2717543d64af965d9f0298ca77`

Fluxo para pushes futuros:

1. Confirmar que o repositorio `Lancellin/tibia-oldschool-private-backup` continua privado.
2. Fazer login na conta GitHub local, se necessario.
3. Verificar a branch atual e o status.
4. Fazer `git push`.

Comandos para pushes futuros:

```bat
git remote -v
git branch --show-current
git status
git push
```

## Checklist rapido

- `PASSWORDALL.txt` incluido no backup privado
- dump SQL do banco `oldschool772db` gerado
- Git raiz criado em `C:\tibia-oldschool`
- `.git` internos de `sources` renomeados para `.git.root-backup-disabled` e ignorados no Git raiz
- `backups` fora do Git para nao poluir o historico
- repositorio remoto somente apos confirmacao de privacidade
