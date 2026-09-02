# 2FA (TOTP) no AAC — arquitetura, segurança e teste manual

Implementação de autenticação em duas etapas no login do site (MyAAC),
compatível com Google Authenticator / Microsoft Authenticator (RFC 6238:
SHA-1, período 30s, 6 dígitos, janela ±1).

## Arquivos

| Arquivo | Papel |
|---|---|
| `website/system/src/Totp.php` | TOTP puro: base32, código, verificação (`hash_equals`), URI otpauth |
| `website/system/src/TwoFactor.php` | Segredo cifrado (AES-256-GCM), pending login, dispositivos confiáveis, cookie |
| `website/system/pages/account/login_functions.php` | `myaac_complete_login()` compartilhado entre step 1 e step 2 |
| `website/system/pages/account/login.php` | Gate 2FA após credenciais válidas |
| `website/system/pages/account/twofactor.php` | Segunda tela (código + "trust this device") |
| `website/system/pages/account/twofactor-setup.php` | Ativação (QR/secret + confirmação), disable, revogar devices |
| `website/system/templates/account.twofactor.html.twig` | Template da segunda tela |
| `website/system/templates/account.twofactor.setup.html.twig` | Template do setup/gerenciamento |
| `website/tools/ext/qrcode/qrcode.min.js` | QR renderizado no cliente (apresentação; o segredo já está na tela durante o setup) |
| Templates `account.management.html.twig` (system + tibiacom) | Link "Security: Two-Factor Authentication" no manage |

## Banco

```sql
ALTER TABLE `accounts` ADD COLUMN `totp_secret` VARCHAR(512) NULL DEFAULT NULL AFTER `password`;
CREATE TABLE IF NOT EXISTS `myaac_trusted_devices` (
  `id` INT NOT NULL AUTO_INCREMENT,
  `account_id` INT NOT NULL,
  `token_hash` CHAR(64) NOT NULL,
  `label` VARCHAR(120) NOT NULL DEFAULT '',
  `created_at` INT NOT NULL DEFAULT 0,
  `expires_at` INT NOT NULL DEFAULT 0,
  `last_used_at` INT NOT NULL DEFAULT 0,
  PRIMARY KEY (`id`), UNIQUE KEY `token_hash` (`token_hash`), KEY `account_id` (`account_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8;
```

Chave do servidor: `config.local.php` → `$config['totp_key']` (64 hex = 256 bits).
**Não commitar** `config.local.php` (já está fora do git). Trocar a chave invalida
todos os 2FA (os segredos deixam de abrir).

## Fluxo de login

1. POST login (conta + senha + captcha) → validações existentes (rate limit, captcha).
2. Conta **sem** 2FA → comportamento antigo (`myaac_complete_login`).
3. Conta **com** 2FA:
   - cookie de device confiável válido (hash no DB, conta certa, não expirado) → login direto;
   - senão → **nenhuma sessão de login é criada**; a sessão ganha apenas o estado
     `pending` (account id, timestamp 5 min, remember, admin, pwhash) + redirect
     para `account/twofactor`.
4. `account/twofactor`: sem pending → redirect (acesso direto não funciona). Com
   pending → form de código. POST: rate limit `twofactor` (5 tentativas / 15 min por
   IP) + `Totp::verify` server-side. Sucesso → `myaac_complete_login` (mesmo caminho
   do step 1) + opcionalmente `trustDevice`. Falha → erro genérico "Invalid or
   expired code." e incrementa o limiter.

## Ativação (setup)

- Logado → `account/twofactor-setup` → "Enable" gera segredo e guarda **só na
  sessão** (`2fa_setup_secret`); a página mostra QR + secret + form de confirmação.
- Só ativa após `Totp::verify` contra o segredo da sessão (`action=confirm`);
  então o segredo é cifrado e gravado em `accounts.totp_secret` e devices antigos
  são revogados.
- O segredo/QR nunca mais são exibidos depois da ativação.

## Dispositivos confiáveis

- Token = `random_bytes(32)` hex no cookie `myaac_trusted_device`; no DB só
  `sha256(token)`. TTL 30 dias; `last_used_at` atualizado a cada uso.
- Cookie: `HttpOnly`, `SameSite=Lax`, `Secure` quando o site estiver em HTTPS
  (hoje HTTP, então Secure fica off para não quebrar o login).
- Revogação: por device (`action=revoke`), todos (`revoke_all`, exige código
  TOTP) e ao desativar o 2FA. Expirado → tratado como ausente e removido.
- `label` = User-Agent truncado, **somente cosmético** (nunca usado na decisão).

## Análise de segurança (pontos pedidos)

- **Roubo/reuso do cookie**: token aleatório de 256 bits; DB tem só o hash;
  revogável; expira em 30 dias; HttpOnly (JS não lê); SameSite=Lax (CSRF cross-site
  não envia). Roubo do cookie = acesso sem 2FA até revogar/expirar — inerente ao
  conceito de "trusted device"; mitigado por expiração + revogação + lista visível.
- **Fixation/replay de sessão**: `session_regenerate_id()` no complete_login e ao
  entrar no pending; pending expira em 5 min; pwhash fica na sessão pending (mesmo
  nível de exposição que o design antigo já tinha para sessões logadas).
- **Bypass da segunda etapa**: pending não define `account`/`password` na sessão →
  não está logado; `account/twofactor` sem pending redireciona; complete_login só é
  chamado após `Totp::verify` ou cookie confiável válido.
- **Brute force do TOTP**: RateLimit 5/15min por IP + janela ±1 + pending de 5 min;
  erro genérico (não distingue conta/código).
- **CSRF**: `csrfProtect()` em login, twofactor e twofactor-setup (todos os POSTs).
- **XSS**: templates novos só usam escape padrão do Twig; QR usa `json_encode` do
  URI (string JS segura); label de device escapado no Twig.
- **Expiração/revogação**: TTL 30 dias no DB; expirado remove e limpa cookie;
  revogação por device/todos/disable.
- **Corrida no login**: dois POSTs de step 2 válidos criariam no máx. dois devices
  (se "trust" marcado) — sem ganho de acesso além do legítimo.
- **Exposição do segredo**: cifrado no DB (AES-256-GCM, chave fora do banco); em
  claro apenas na sessão durante o setup e na página de setup (HTTPS recomendado).
- **Enumeração de contas**: step 2 só existe após credenciais válidas; mensagens
  genéricas; setup exige login; nenhum endpoint novo responde "conta existe".
- **Acesso direto à página seguinte**: `account/twofactor` sem pending → redirect;
  `account/twofactor-setup` sem login → mostra form de login (base.php).

## Limitações conhecidas

- Site hoje em HTTP: cookie sem `Secure` (ativa automaticamente com HTTPS) e
  segredo/senha trafegam em claro na rede — colocar HTTPS quando possível.
- `label` do device é só o User-Agent (cosmético).
- Sem HTTPS, "trust this device" é a principal defesa contra re-prompt; ok.

## Teste manual (passo a passo)

Pré: site no ar (`Start-Site.cmd`).

1. **Conta sem 2FA continua igual**: logar com uma conta sem 2FA → entra direto,
   sem tela de código.
2. **Ativação**: logar → manage → link "Security: Two-Factor Authentication" →
   "Enable" → escanear QR (ou digitar o secret) no autenticador → digitar o código
   de 6 dígitos → "Enable" → mensagem de ativado. Recarregar: status "enabled",
   QR/secret não aparecem mais.
3. **Código errado na ativação**: repetir o enable e confirmar com código errado →
   erro e 2FA continua desativado.
4. **Login com 2FA**: logout → login (conta+senha+captcha) → deve cair na tela de
   código (não entra direto). Código errado → "Invalid or expired code."; 6 erros
   seguidos → bloqueio temporário ("Too many attempts...").
5. **Login com código certo sem "trust"**: entra. Logout → login de novo → pede o
   código de novo.
6. **Com "trust this device"**: entra; logout → login → **não** pede código (cookie
   confiável). Abrir em outra janela/aba anônima → pede código (cookie não existe lá).
7. **Revogação**: logado → setup → "Remove" no device → próximo login pede código.
   "Remove all trusted devices" exige código. "Disable" exige código e volta ao
   comportamento do item 1.
8. **Acesso direto**: abrir `http://127.0.0.1/account/twofactor` sem ter feito o
   step 1 → redireciona para o manage/login (não mostra form de código).
9. **Expiração do pending**: fazer o step 1, esperar >5 min sem enviar o código →
   a página volta a exigir o login completo.
10. **Admin com 2FA**: marcar a checkbox de admin no login (se aplicável) → step 2 →
    entra no painel admin normalmente.

## 2FA no jogo (client/servidor)

Mesmo segredo TOTP e mesma tabela de dispositivos confiáveis do site: o código do
autenticador vale para os dois ao mesmo tempo.

### Protocolo (extensão custom do 7.72)

- Client → servidor: payload opcional acrescentado **após a senha**, dentro do bloco
  RSA, como string com prefixo de tamanho: `2FA1\n<codigo>\n<trust 0|1>\n<token>`.
  Clients antigos não enviam nada (padding RSA aleatório lê como string de tamanho
  zero e é ignorado). Contas sem 2FA não mudam de fluxo.
- Servidor → client:
  - `0x2E` (46): "código 2FA necessário" — o client abre a janela de token em vez
    da charlist;
  - `0x2F` (47): token novo de dispositivo confiável (quando "trust" foi marcado),
    enviado antes da charlist; o client grava em `g_settings`
    (`twofactor_token_<conta>`) e reenvia nos próximos logins.

### Arquivos

| Arquivo | Papel |
|---|---|
| `sources/nekiro.../src/totp.{h,cpp}` | TOTP C++ (RFC 6238, SHA-1, 30s, ±1, compare constant-time) + AES-256-GCM |
| `sources/nekiro.../src/twofactor.{h,cpp}` | segredo (`accounts.totp_secret`) + `myaac_trusted_devices` |
| `sources/nekiro.../src/protocollogin.{h,cpp}` | parse do payload, gate, throttle, opcodes 46/47 |
| `server/config.lua` → `totp_key` | mesma chave do `config.local.php` do site |
| `modules/gamelib/protocollogin.lua` | opcodes 46/47 + `getLoginExtendedData` |
| `modules/client_entergame/entergame.lua` | janela de token + checkbox "Trust this device for 30 days" |

### Segurança

- Throttle server-side: 5 códigos errados / 15 min por conta+IP (em memória).
- Token confiável: 256-bit aleatório; DB guarda só sha256; expira em 30 dias;
  revogável pelo site (mesma tabela).
- Sem 2FA ativado na conta: fluxo de login 100% inalterado (clients antigos ok).
- Segredo nunca sai do servidor; client só recebe token opaco.

### Teste manual (jogo)

1. Ative o 2FA pelo **site** numa conta de teste (seção anterior).
2. Client da pasta "OT CLIENT JOGÁVEL": login com conta+senha → **janela de token**
   aparece (charlist não). Código errado → janela permanece/novo pedido; 5 erros →
   "Too many attempts...".
3. Código certo sem trust → charlist. Logout/login → pede código de novo.
4. Código certo **com** trust → charlist; logout/login → entra direto. Revogar o
   device pelo site → volta a pedir código.
5. Conta **sem** 2FA → login normal, sem janela de token.
6. Site e jogo aceitam o **mesmo código** do autenticador no mesmo instante.
