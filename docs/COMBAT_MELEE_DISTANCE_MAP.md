# Mapa do Fluxo de Combate — Melee & Distance Fighting

**Projeto:** TFS 1.5 Nekiro downgrade para Tibia 7.72  
**Fonte:** `D:\tibia-oldschool\sources\nekiro-tfs-1.5-7.72\src`  
**Grafo codebase-memory:** 13.228 nós / 51.890 arestas (reindexado em 2026-08-07)

---

## 1. Visão Geral da Arquitetura

```
┌─────────────────────────────────────────────────────────────┐
│                    CLIENTE (Tibia 7.72)                      │
│   Ataque → ProtocolGame::parseAttack() (opcode 0xA1)        │
│   Seguir → ProtocolGame::parseFollow() (opcode 0xA2)        │
└──────────────────────────┬──────────────────────────────────┘
                           │
┌──────────────────────────▼──────────────────────────────────┐
│                   PROTOCOL GAME                               │
│   handleAttackTargetRequest(creatureId)                       │
│   → rate-limit 30ms entre attacks                             │
│   addGameTask(&Game::playerSetAttackedCreature, ...)          │
└──────────────────────────┬──────────────────────────────────┘
                           │
┌──────────────────────────▼──────────────────────────────────┐
│                         GAME                                  │
│   Game::playerSetAttackedCreature(playerId, creatureId)       │
│     → Combat::canTargetCreature(player, target)              │
│     → player->setAttackedCreature(target)                    │
│     → g_dispatcher.addTask(Game::updateCreatureWalk...)      │
└──────────────────────────┬──────────────────────────────────┘
                           │
┌──────────────────────────▼──────────────────────────────────┐
│                     PLAYER (LOOP DE ATAQUE)                   │
│   Player::doAttacking(uint32_t interval)                     │
│     ├── Verifica attack speed (OTSYS_TIME - lastAttack >= AS)│
│     ├── CONDITION_PACIFIED? skip                            │
│     ├── classicSpeed?                                      │
│     │   YES: use weapon immediately                         │
│     │   NO: check canDoAction()/getNextActionTime()         │
│     │                                                        │
│     ├── Gets weapon: getWeapon() → item                     │
│     ├── Gets Weapon* via: g_weapons->getWeapon(item)        │
│     │                                                        │
│     │   se weapon != nullptr:                                │
│     │     se !weapon->interruptSwing():                     │
│     │       result = weapon->useWeapon(this, item, target)  │
│     │     senão:                                            │
│     │       result = weapon->useWeapon(...)                 │
│     │   senão:                                              │
│     │     result = Weapon::useFist(this, target)            │
│     │                                                        │
│     ├── Cria SchedulerTask(delay = getAttackSpeed())         │
│     │   → Game::checkCreatureAttack(getID())                │
│     └── lastAttack = OTSYS_TIME() (se result==true)         │
│                                                             │
│   Game::checkCreatureAttack(creatureId)                     │
│     → creature->onAttacking(0)                              │
│       → onAttacked()                                        │
│       → attackedCreature->onAttacked()                     │
│       → doAttacking(interval)  [recursivo, cada tick]      │
└─────────────────────────────────────────────────────────────┘
```

---

## 2. Hierarquia de Classes de Armas

```
class Weapon : public Event (src/weapons.h)
├── bool configureEvent(xml_node)    // parse XML <melee>/<distance>/<wand>
├── virtual void configureWeapon(ItemType& it)
├── virtual bool interruptSwing() const   // DEFAULT: false
├── int32_t playerWeaponCheck(Player*, Creature*, uint8_t shootRange)
├── bool useWeapon(Player*, Item*, Creature*)   // ORCHESTRATOR
├── static bool useFist(Player*, Creature*)
├── virtual getWeaponDamage(...) = 0    // ABSTRACT
├── virtual getElementDamage(...) = 0   // ABSTRACT
├── virtual getElementType() = 0        // ABSTRACT
│
├── class WeaponMelee : public Weapon
│   ├── configureWeapon(ItemType&)      // extrai elementType + elementDamage das abilities
│   ├── useWeapon(...)                  // wrapper simples pra playerWeaponCheck + internalUseWeapon
│   ├── getWeaponDamage(...)            // fórmula melee ( knight oldschool vs normal)
│   ├── getElementDamage(...)           // dano elemental da arma (swords com element dmg)
│   ├── getSkillType(...)               // Sword→SKILL_SWORD, Club→SKILL_CLUB, Axe→SKILL_AXE
│   └── params: blockedByArmor=true, blockedByShield=true, combatType=COMBAT_PHYSICALDAMAGE
│
├── class WeaponDistance : public Weapon
│   ├── interruptSwing() = true         // DISTANCE INTERRUPTE swing atual!
│   ├── configureWeapon(ItemType&)      // set distanceEffect=it.shootType
│   ├── useWeapon(...)                  // COMPLEXO — hit chance, ammo combo, miss logic
│   ├── getWeaponDamage(...)            // fórmula distance (old school attack mode factor)
│   ├── getElementDamage(...)           // dano elemental + bonus do bow (ammo soma attack)
│   ├── getSkillType(...)               // SKILL_DISTANCE (skill gain diferente p/ hit/miss)
│   └── params: blockedByArmor=true, combatType=COMBAT_PHYSICALDAMAGE (sem shield!)
│
└── class WeaponWand : public Weapon
    ├── configureEvent(xml_node)        // min/max/type
    ├── configureWeapon(ItemType&)      // distanceEffect=shootType
    ├── getWeaponDamage(...)            // random(minChange, maxChange)
    └── params: combatType = elemento (earth/fire/etc.)
```

---

## 3. Fluxo Detalhado — MELEE Attack

### 3.1 Entrada
```
Client envia ataque → ProtocolGame::parseAttack → handleAttackTargetRequest
→ Game::playerSetAttackedCreature → Creature::setAttackedCreature → Player::doAttacking
```

### 3.2 No loop do Player::doAttacking
1. **Timer check:** `(OTSYS_TIME() - lastAttack) >= getAttackSpeed()`
2. **Pacified?** Se sim, retorna sem atacar
3. **Obter arma:** `Item* tool = getWeapon()` → slot left/right; `Weapon* weapon = g_weapons->getWeapon(tool)`
4. **Se WeaponMelee:**
   - `!interruptSwing()` (melee retorna falso) → chama diretamente `weapon->useWeapon(this, tool, target)`
5. **Scheduler:** agenda `Game::checkCreatureAttack(id)` com delay = attack speed
   - classicSpeed=false: usa scheduler personalizado (`setNextActionTask`)
   - classicSpeed=true: scheduler padrão + `nextClassicAttackCheck`

### 3.3 WeaponMelee::useWeapon
```cpp
damageModifier = playerWeaponCheck(player, target, range);
if (damageModifier == 0) return false;
internalUseWeapon(player, item, target, damageModifier);
return true;
```

### 3.4 WeaponMelee::getWeaponDamage (dano calculado)
```
attackSkill = player->getWeaponSkill(item)     // SKILL_SWORD/CLUB/AXE
attackValue = item->getAttack()                 // valor fixo do item
attackFactor = player->getAttackFactor()        // 1.0 (attack), 1.2 (balanced), 2.0 (defense)

// Knight formula (vocations 4, 8) com attackValue > 16:
if (attackValue > 16 && isKnightMeleeFormulaVocation):
    attackModeFactor = getOldschoolAttackModeFactor(attackFactor)
    // >= 1.9 → 0.5, >= 1.1 → 0.75, else 1.0
    baseDamage = (level/5) + (attackValue*1.5) + ((skill²/1620)*attackValue)
    maxValue = floor(baseDamage * attackModeFactor)
    minValue = floor(((level/4) + maxValue*0.18) * attackModeFactor)
    return random(minValue, maxValue)

// Formula padrão:
maxValue = ceil((attackSkill * (attackValue*0.05) + (attackValue*0.5)) * vocation->meleeDamageMultiplier)
finalMax = Weapons::getMaxWeaponDamage(level, attackSkill, attackValue, attackFactor) * multiplier
return random(0, finalMax)
```

### 3.5 Weapon::internalUseWeapon (comum a todas as armas)
```
se scripted (tem onUseWeapon Lua):
    executeUseWeapon(player, var)  // chama Lua onUseWeapon(player, variant)
senão:
    damage.type = params.combatType
    damage.value = (getWeaponDamage(player, target, item) * damageModifier) / 100
    damage.maxValue = (getWeaponDamage(..., maxDamage) * damageModifier) / 100
    damage.secondary.type = getElementType()
    damage.secondary.value = getElementDamage(...)
    
    Combat::doTargetCombat(player, target, damage, params)
    
    onUsedWeapon(player, item, targetTile)
```

### 3.6 onUsedWeapon (custos e pós-dano)
```
se !hasFlag(NotGainSkill):
    getSkillType(...) → player->addSkillAdvance(skillType, skillPoint)
    // Melee: Sword/Club/Axe com 1 ponto quando BLOCK_NONE

manaCost = getManaCost(player) → changeMana(-manaCost)
healthCost = getHealthCost(player) → changeHealth(-healthCost)
soulCost = soul → changeSoul(-soul)

se breakChance random <= breakChance:
    decrementItemCount(item)  // destrói ou reduz count

switch action:
    REMOVECOUNT: remove ammo se config permite
    REMOVECHARGE: decrementa charges
    MOVE: move arma para tile alvo (throwing weapons)
```

---

## 4. Fluxo Detalhado — DISTANCE (Bow/Crossbow) Attack

### 4.1 Diferenças-chave do Melee
- `WeaponDistance::interruptSwing() = true` — cancela ataques melee pendentes
- Hit chance calculado dinamicamente (baseado em distância + skill)
- Ammo system: dano é combinado do bow + arrow/bolt
- Max hit chance limitado: 75% (one-handed), 90% (two-handed), 100% (explicito)
- Não bloqueia por shield (`!blockedByShield`)

### 4.2 WeaponDistance::useWeapon (fluxo completo)

#### A) Damage Modifier
```
se item é AMMO:
    mainWeapon = player->getWeapon(true)  // pega o bow
    mainWeapon = g_weapons->getWeapon(mainWeapon)
    se mainWeapon:
        damageModifier = mainWeapon->playerWeaponCheck(player, target, mainRange)
    senão se mainWeaponItem:
        damageModifier = playerWeaponCheck(player, target, mainRange)
senão:
    damageModifier = playerWeaponCheck(player, target, item->getShootRange())
```

#### B) Hit Chance Calculation
```
se ItemType.hitChance == 0 (calculado dinamicamente):
    skill = player->getSkillLevel(SKILL_DISTANCE)
    distance = max(|playerX - targetX|, |playerY - targetY|)
    
    maxHitChance = ItemType.maxHitChance
                 ?? (item.ammoType != NONE ? 90 : 75)  // fallback
    
    switch maxHitChance:
        75 (one-handed): fórmula específica por distância
        90  (two-handed): fórmula mais permissiva
        100: fórmula máxima
        
    Fórmula example (75%, dist=3):
        chance = min(skill, 45) * 1.55 + 6
    
    Fórmula example (90%, dist=3):
        chance = min(skill, 45) * 2

se item é AMMO:
    bow = player->getWeapon(true)
    se bow && bow->hitChance != 0:
        chance += bow->hitChance  // bônus do bow

se chance >= random(1,100):
    // HIT
    Weapon::internalUseWeapon(player, item, target, damageModifier)
senão:
    // MISS — efeito no tile ao redor do alvo
    destTile = target->tile (ou tile adjacente válido)
    Weapon::internalUseWeapon(player, item, destTile)  // path vai ao tile
```

#### C) Hit Chance Formulas (distância 1-7)

**One-handed (max 75%):**
| Dist | Fórmula |
|------|---------|
| 1,5 | min(skill,74)+1 |
| 2 | min(skill,28)*2.4+8 |
| 3 | min(skill,45)*1.55+6 |
| 4 | min(skill,58)*1.25+3 |
| 6 | min(skill,90)*0.8+3 |
| 7 | min(skill,104)*0.7+2 |

**Two-handed (max 90%):**
| Dist | Fórmula |
|------|---------|
| 1,5 | min(skill,74)*1.2+1 |
| 2 | min(skill,28)*3.2 |
| 3 | min(skill,45)*2 |
| 4 | min(skill,58)*1.55 |
| 6,7 | min(skill,90) |

#### D) Dano à Distancia

**WeaponDistance::getWeaponDamage:**
```
attackValue = item->getAttack()
se item é AMMO:
    bow = player->getWeapon(true)
    attackValue += bow->getAttack()  // SOMA attack do bow + ammo

attackSkill = player->getSkillLevel(SKILL_DISTANCE)
attackFactor = player->getAttackFactor()

// Oldschool (attackValue > 10):
if attackValue > 10:
    attackModeFactor = getOldschoolAttackModeFactor(attackFactor)
    minValue = floor(getOldschoolDistanceMinDamage(level, attackValue) * factor)
    maxValue = floor(getOldschoolDistanceMaxDamage(level, skill, attackValue) * factor)
    return random(minValue, maxValue)

// Normal:
maxValue = Weapons::getMaxWeaponDamage(...) * vocation->distDamageMultiplier
minValue = player ? (targetPlayer ? level*0.1 : level*0.2) : 0
return random(minValue, maxValue)
```

**getOldschoolDistanceMaxDamage:**
```
base = (level/4) + 10 + attackValue * ((skill/15) + pow(skill,1.5)/3100)
```

**getOldschoolDistanceMinDamage:**
```
base = (level/3) + attackValue
```

---

## 5. Sistema de Defesa (blockHit chain)

### 5.1 Flow geral
```
Combat::doTargetCombat(caster, target, damage, params)
  → applyCharmCritical(caster, target, damage)     // charm system
  → g_game.combatBlockHit(damage, attacker, target, 
                            params.blockedByShield, 
                            params.blockedByArmor, ...)
    → target->blockHit(...)
  → applyCharmDodge(caster, target, damage)         // dodge charms
  → combatChangeHealth(caster, target, damage)      // aplica dano final
```

### 5.2 Game::combatBlockHit
```
se primary.type == COMBAT_NONE && secondary.type == COMBAT_NONE:
    return true (ignora)

rangedIgnoresDefense = (damage.origin == ORIGIN_RANGED)  // SIM para distance!
criticalCreatureHit = critical && target é monster && não é player combating

se criticalCreatureHit:
    verifica armor ANTES de call blockHit
    blockType = target->blockHit(attacker, type, damage, 
                                 false, false, field, ignoreRes)
senão:
    // KEY: ranged ignora defense no game.cpp!
    blockType = target->blockHit(attacker, type, damage,
                                 rangedIgnoresDefense ? false : checkDefense,
                                 checkArmor, field, ignoreRes)

se secondary.type != COMBAT_NONE:
    secondaryBlockType = target->blockHit(attacker, secondary.type, secondary.value,
                                          false, false, field, ignoreRes)

damage.blockType = primaryBlockType
return primaryBlockType != BLOCK_NONE || secondaryBlockType != BLOCK_NONE
```

**Importante:** `rangedIgnoresDefense = true` significa que **ataques à distância ignoram defesa (shield skill)** no `game.cpp`, mesmo que `params.blockedByShield` seja passado como false para WeaponDistance. O ataque distance nunca passa pelo bloco de defense — só armadura e resistências.

### 5.3 Creature::blockHit (base)
```
se immune(combatType): damage=0, return BLOCK_IMMUNITY

se checkDefense || checkArmor:
    hasDefense = false
    se checkDefense && blockCount > 0:
        --blockCount; hasDefense = true
    
    se hasDefense && canUseDefense && checkDefense:
        damage -= getDefenseReduction(attacker, combatType)
        se damage <= 0: damage=0, blockType=BLOCK_DEFENSE
    
    se checkArmor:
        damage -= getArmorReduction(attacker, combatType)
        se damage <= 0: damage=0, blockType=BLOCK_ARMOR

attacker->onAttackedCreature(this)
attacker->onAttackedCreatureBlockHit(blockType)
onAttacked()
```

### 5.4 Player::blockHit (override — resistências de equipamento)
```
blockType = Creature::blockHit(...)  // chamada base (def+armor)

se blockType != BLOCK_NONE:
    sendCreatureSquare(attacker, SQ_COLOR_BLACK)
    return blockType

se damage <= 0:
    damage=0; return BLOCK_ARMOR

// Loop por todos os slots de equipamento:
se !ignoreResistances:
    para cada slot CONST_SLOT_FIRST até CONST_SLOT_AMMO:
        item = inventory[slot]
        abilities = item->abilities
        absorve % do combatType contra damage
        se charges > 0: decrementa charge
        se field: also aplica fieldAbsorbPercent

se damage <= 0:
    damage=0; blockType=BLOCK_ARMOR
return blockType
```

### 5.5 GetDefense (Player)
```
defenseSkill = SKILL_FIST
defenseValue = 5
sheld, weapon = getShieldAndWeapon()

se shield:
    defenseValue = shield->getDefense()
    defenseSkill = SKILL_SHIELD
senão se weapon:
    defenseValue = weapon->getDefense() + weapon->extraDefense
    defenseSkill = weaponSkill(weapon)

se defenseSkill == 0:
    FIGHTMODE_ATTACK/BALANCED → returns 1
    FIGHTMODE_DEFENSE → returns 2

return (defenseSkill/4 + 2.23) * defenseValue * 0.15 * defenseFactor * vocation.defenseMultiplier
```

### 5.6 GetArmor (Player)
```
armor = soma armor de head+necklace+armor+legs+feet+ring
return armor * vocation.armorMultiplier
```

### 5.7 Armor Reduction (fórmula customizada Nekiro)
```
armorValue = getArmor()
minBlock = getCustomArmorMinBlock(armorValue)
maxBlock = getCustomArmorMaxBlock(armorValue)
se maxBlock <= 0: return 0
return random(min(minBlock,maxBlock), maxBlock)
```

---

## 6. Attack Speed

```
Player::getAttackSpeed():
    weapon = getWeapon(true)
    se !weapon || weapon->attackSpeed == 0:
        return vocation->getAttackSpeed()
    return weapon->getAttackSpeed()

Player::getAttackFactor():
    FIGHTMODE_ATTACK    → 1.0
    FIGHTMODE_BALANCED  → 1.2
    FIGHTMODE_DEFENSE   → 2.0

Player::getDefenseFactor():
    Durante attack window (tempo < attackSpeed desde último swing):
        ATTACK → 0.5
        BALANCED → 0.75
    Sempre:
        DEFENSE → 1.0
        default → 1.0
```

---

## 7. Skill Gain

**Melee (WeaponMelee::getSkillType):**
```
se !player->getAddAttackSkill() → skillpoint = 0
se player->getLastAttackBlockType() == BLOCK_IMMUNITY → skillpoint = 0

skill = SKILL_SWORD/CLUB/AXE (dependendo weaponType)
skillPoint = 1  // sempre 1 point when valid
```

**Distance (WeaponDistance::getSkillType):**
```
se BLOCK_NONE → skillPoint = 2  // hit puro ganha 2 points
se BLOCK_DEFENSE ou BLOCK_ARMOR → skillPoint = 1
se BLOCK_* demais → skillPoint = 0

se !getAddAttackSkill() → skillPoint = 0

skill = SKILL_DISTANCE (sempre)
```

**Fist (Weapon::useFist):**
```
se !hasFlag(NotGainSkill) → addSkillAdvance(SKILL_FIST, 1)
```

---

## 8. Configuração dos Itens (ItemType fields)

Campos relevantes em `items.h`:
```cpp
uint32_t attackSpeed;       // tempo entre swings (0=voc default)
int32_t attack;             // dano base do item
int32_t defense;            // defesa base
int32_t extraDefense;       // defesa adicional
int32_t armor;              // armadura base
uint8_t shootRange;         // range máximo (default 1)
int8_t hitChance;           // +bônus de acerto fixo (0=calculado dinâmico)
int32_t maxHitChance;       // teto de acerto (-1=default 75/90)
Ammo_t ammoType;            // tipo de munição
WeaponType_t weaponType;    // WEAPON_* enum
ShootType_t shootType;      // animação de disparo
```

**Ammo types (const.h):**
```
AMMO_NONE, BOLT, ARROW, SPEAR, THROWINGSTAR, THROWINGKNIFE, STONE, SNOWBALL
```

**Weapon types (const.h):**
```
WEAPON_AXE, WEAPON_SWORD, WEAPON_CLUB, WEAPON_SHIELD,
WEAPON_DISTANCE, WEAPON_WAND, WEAPON_AMMO
```

---

## 9. Pontos de Partilha e Divergência: Melee vs Distance

| Aspecto | Melee | Distance |
|---------|-------|----------|
| **Class** | `WeaponMelee` | `WeaponDistance` |
| **interruptSwing** | false | **true** |
| **Blocked by Shield** | **true** | false |
| **Blocked by Armor** | true | **true** |
| **Ranged ignores defense** | N/A | **SIM** (no game.cpp) |
| **Hit chance** | Always hits (range≤1) | **Calculado dinamicamente** |
| **Miss behavior** | Sem miss | Efeito visual no tile adjacente |
| **Damage source** | Only weapon | **Weapon + Ammo combinados** |
| **Skill gain on hit** | 1 point | **2 points** |
| **Skill gain on block** | 0 points | 1 point |
| **Oldschool formula** | Knights only (atk>16 voc 4/8) | **Todo mundo** (atk>10) |
| **Ammo consumption** | N/A | Configurable (ConfigManager) |
| **Elemental damage** | From sword abilities | From item abilities + bow bonus |
| **Damage multiplier** | `vocation->meleeDamageMultiplier` | `vocation->distDamageMultiplier` |
| **Shot animation** | None | `ItemType.shootType` |

---

## 10. Eventos, Callbacks e Dependências

### 10.1 Eventos Scriptados (Lua)
- `onUseWeapon(player, variant)` — chamado quando weapon é scripted (XML tem tag child)
  - Variant type = `VARIANT_NUMBER` (target ID) para single-target
  - Variant type = `VARIANT_TARGETPOSITION` (tile position) para area shots

### 10.2 Creature Events
- `onAttackedCreature(Creature* target)` — notifica quem atacou
- `onAttackedCreatureBlockHit(BlockType_t)` — notifica bloqueio
- `onAttackedCreatureDrainHealth(Creature* target, int32_t points)` — notifica drain
- `onAttacked()` — notifica que foi atacado

### 10.3 Charm System
- `applyCharmCritical(player, target, damage)` — potencial crítico de charms
- `applyCharmDodge(caster, target, damage)` — dodge via charms (pula o damage)
- `accumulateCharmLeech` / `applyCharmLeechTotals` — life/mana leech via charms

### 10.4 Special Skills (Lifesteal/Mana Steal/Crit)
```
SpecialSkills:
    SPECIALSKILL_CRITICALHITCHANCE      // probabilidade de crit
    SPECIALSKILL_CRITICALHITAMOUNT      // multiplicador de dano crit
    SPECIALSKILL_LIFELEECHCHANCE        // lifesteal prob
    SPECIALSKILL_LIFELEECHAMOUNT        // lifesteal %
    SPECIALSKILL_MANALEECHCHANCE        // manasteal prob
    SPECIALSKILL_MANALEECHAMOUNT        // manasteal %
```

### 10.5 CharacterBonuses (Player)
```
struct CharacterBonuses {
    skillBonus[SKILL_LAST+1]
    totalCriticalChance
    equipmentCriticalChance
    equipmentCriticalDamagePercent
    charmCriticalChance
    hitChance                         // bônus global de hit
    equipmentLifeLeechChance/Amount
    equipmentManaLeechChance/Amount
    charmLifeLeechChance/Amount
    charmManaLeechChance/Amount
    charmManaLeechAreaEfficiency
    charmDodgeChance
}
```

---

## 11. Resumo dos Arquivos-Chave

| Arquivo | Responsabilidade |
|---------|-----------------|
| `src/weapons.h` | Declarações: Weapon, WeaponMelee, WeaponDistance, WeaponWand, Weapons |
| `src/weapons.cpp` | Implementações completas de todas as classes de weapon |
| `src/combat.h` | Classe Combat (doTargetCombat, doAreaCombat, params, areas) |
| `src/combat.cpp` | Aplicação de dano, blockHit na engine, efeitos pós-combate |
| `src/creature.h/.cpp` | Base: setAttackedCreature, onAttacking, doAttacking, blockHit |
| `src/player.h/.cpp` | Player: doAttacking, getAttackSpeed, getWeapon, blockHit, skills |
| `src/game.cpp` | Game: playerSetAttackedCreature, combatBlockHit, checkCreatureAttack |
| `src/protocolgame.cpp` | Input: parseAttack, parseFollow, handleAttackTargetRequest |
| `src/items.h` | ItemType struct: attack, defense, armor, hitChance, attackSpeed, etc. |
| `src/item.h` | Item class: getters de attack, armor, defense, hitChance, shootRange |
| `src/const.h` | Enums: Ammo_t, WeaponType_t, WeaponAction_t, BlockType_t |
| `src/luascript.cpp` | Bindings Lua: luaWeaponAttack, luaWeaponHitChance, etc. |
| `data/weapons/weapons.xml` | Definição XML de todas as armas (melee/distance/wand IDs) |

---

## 12. Observações Importantes Encontradas no Código

1. **Oldschool formulas são ativas**: O código tem fórmulas específicas "oldschool" para knights (melee) e todos (distance). `getOldschoolAttackModeFactor` modula dano baseado no fight mode.

2. **Ranged ignora defense hardcoded**: Em `Game::combatBlockHit`, `rangedIgnoresDefense = (damage.origin == ORIGIN_RANGED)` é sempre verdadeiro. Isso significa que flechas/flechas NÃO sofrem redução de defesa (shield skill), apenas armor e resistências.

3. **Combo Bow+Ammunition**: Dano distance combina `item->getAttack()` do arrow/bolt COM `weapon->getAttack()` do bow. Ambos somados no cálculo.

4. **InterruptSwing do distance**: Quando um paladino ataca com distance, `interruptSwing()` retorna `true`, o que significa que cancela qualquer ataque melee pendente. Mas o ataque real ainda é executado no mesmo turno (só não espera o próximo swing melee terminar).

5. **Attack speed 0 = usar velocidade da vocação**: Se a arma tem `attackSpeed=0`, usa a velocidade da vocação.

6. **Fight mode afeta BOTH offense e defense**: AttackFactor (offense) e DefenseFactor (defense) mudam conforme o fight mode do player.
