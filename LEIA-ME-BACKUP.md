# Tibia Oldschool - Backup recompilavel

Snapshot criado e validado em 2026-06-10.

Leia nesta ordem:

1. `docs/RETOMADA_APOS_FORMATACAO_2026-06-10.md`
2. `docs/BACKUP_RECOMPILAVEL_VALIDADO_2026-06-10.md`
3. `docs/COMO_CRIAR_BACKUP_RECOMPILAVEL.md`
4. `docs/CONTEXTO_CONTINUACAO_2026-06-02.md`
5. `tools/backup/Build-All.ps1`

Executaveis prontos:

- Servidor: `server/tfs.exe`
- Client: `sources/otclient-redemption/otclient.exe`
- RME: `sources/rme-otacademy/rme.exe`

Este backup usa um banco de teste separado:

- `oldschool772db_backup_test_20260610`

O `config.lua` original foi preservado em:

- `backup-extras/config.production-original.lua`

Importante:

- os sources e as bibliotecas atuais permitem recompilar offline;
- Visual Studio, CMake e MariaDB precisam estar instalados;
- os instaladores desses programas nao estao dentro deste backup;
- o snapshot exato dos assets ativos esta em
  `backup-extras/Sprites Permanentes/estado-ativo-2026-06-10`;
- antes de formatar, copie este backup para outro disco fisico.

Nao publique este backup sem revisar credenciais, dump SQL e arquivos privados.
