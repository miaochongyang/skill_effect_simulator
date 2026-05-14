# 主角属性模板框架（JSON v1）

本文档定义一个可扩展的主角属性配置模板，用于把“角色成长、战斗参数、流派构筑、经济与生存”统一到一份 JSON 中。
该模板和当前项目的无头 Tick 模拟兼容，支持先最小接入、再逐步扩展。

## 1. 设计目标

- 单文件描述主角全部可用信息，便于 A/B 配置与批量仿真。
- 与现有配置兼容：可映射到 `player_build.json` 的最小字段。
- 具备数学可控性：成长曲线与乘区边界明确，避免数值爆炸。
- 便于分析：字段结构直接对应输出指标（DPS、生存时长、击杀效率）。

## 2. 文件与版本

- 推荐路径：`config/player_profile_template.json`
- `schema_version`：当前为 `v1`
- 时间单位：`second`
- 距离单位：`world_unit`

## 3. 顶层结构

```json
{
  "schema_version": "v1",
  "profile_id": "default_survivor_v1",
  "display_name": "Default Survivor",
  "simulation": {},
  "base_stats": {},
  "combat": {},
  "defense": {},
  "movement": {},
  "resource": {},
  "progression": {},
  "loadout": {},
  "rules": {},
  "analysis_tags": []
}
```

## 4. 字段说明

### 4.1 基础身份

- `profile_id`：配置唯一 ID（字符串，非空）。
- `display_name`：显示名（字符串，非空）。

### 4.2 simulation（模拟边界）

- `player_hp`：初始生命值（`> 0`）。
- `max_survival_time_sec`：最大生存时长上限（`> 0`）。

说明：这两个字段与当前引擎 `player_build.json` 可直接一一映射。

### 4.3 base_stats（基础属性）

- `max_hp`：基础最大生命（`>= 1`）。
- `base_attack_power`：基础攻击强度（`>= 0`）。
- `base_attack_speed`：基础攻速倍率（`> 0`）。
- `base_cooldown_reduction`：基础冷却缩减（`[0, 0.9]`）。
- `base_crit_chance`：基础暴击率（`[0, 1)`）。
- `base_crit_multiplier`：基础暴击伤害倍率（`>= 1`）。
- `base_luck`：掉落/抽样修正（`>= 0`）。

### 4.4 combat（输出乘区）

- `damage_multiplier`：全局伤害倍率（`> 0`）。
- `projectile_count_bonus`：额外投射物数量（整数，`>= 0`）。
- `aoe_radius_multiplier`：范围倍率（`> 0`）。
- `status_power_multiplier`：异常强度倍率（`> 0`）。当前为仅参考字段，游戏本体暂不接入结算。
- `final_damage_taken_multiplier`：主角受伤最终倍率（`> 0`，低于 1 表示减伤）。

### 4.5 defense（防御）

- `armor`：护甲值（`>= 0`）。当前为仅参考字段，游戏本体暂不接入结算。
- `evasion`：闪避率（`[0, 0.95]`）。当前为仅参考字段，游戏本体暂不接入结算。
- `block_chance`：格挡率（`[0, 0.95]`）。当前为仅参考字段，游戏本体暂不接入结算。
- `block_flat_reduction`：格挡减伤值（`>= 0`）。当前为仅参考字段，游戏本体暂不接入结算。
- `regen_hp_per_sec`：生命回复（`>= 0`）。
- `life_steal`：吸血比例（`[0, 1]`）。

### 4.5.1 当前开发范围属性计算公式（对应 4.3~4.5）

说明：以下仅覆盖当前“需要开发”的字段。已标注“仅参考”的字段不在本节结算范围内。

设：

- `dt`：固定步长秒数（`fixed_dt`）。
- `D_skill`：技能或武器的基础伤害（来自 `loadout.skills[].damage` 或武器配置）。
- `CD_skill`：技能或武器基础冷却。
- `R_skill`：技能基础半径（来自 `loadout.skills[].radius`）。
- `N_proj_base`：武器基础投射物数量（或默认 1）。
- `u`：`[0,1)` 均匀随机数。

1. 生命相关（`max_hp`）

```text
final_max_hp = max_hp
initial_hp = min(simulation.player_hp, final_max_hp)
```

2. 攻击与暴击（`base_attack_power`、`base_crit_chance`、`base_crit_multiplier`、`damage_multiplier`）

```text
crit_flag = (u < base_crit_chance) ? 1 : 0
crit_factor = crit_flag ? base_crit_multiplier : 1.0

outgoing_hit_damage = D_skill
                    * base_attack_power
                    * damage_multiplier
                    * crit_factor
```

3. 攻速与冷却（`base_attack_speed`、`base_cooldown_reduction`）

```text
cdr = clamp(base_cooldown_reduction, 0.0, 0.9)
spd = max(base_attack_speed, 1e-6)

final_cooldown = CD_skill * (1.0 - cdr) / spd
final_cooldown = max(final_cooldown, min_cooldown_sec)
```

实现建议：`min_cooldown_sec` 可取 `fixed_dt`，避免同 Tick 多次触发抖动。

4. 投射物与范围（`projectile_count_bonus`、`aoe_radius_multiplier`）

```text
final_projectiles_per_shot = max(1, N_proj_base + projectile_count_bonus)
final_aoe_radius = R_skill * aoe_radius_multiplier
```

面积放大关系（用于平衡评估）：

```text
aoe_area_gain = aoe_radius_multiplier^2
```

5. 受击倍率（`final_damage_taken_multiplier`）

```text
incoming_damage_to_player = incoming_raw_damage * final_damage_taken_multiplier
```

6. 回复与吸血（`regen_hp_per_sec`、`life_steal`）

```text
hp_after_regen = min(final_max_hp, hp_t + regen_hp_per_sec * dt)

life_steal_gain = damage_dealt_to_hp * clamp(life_steal, 0.0, 1.0)
hp_after_lifesteal = min(final_max_hp, hp_after_regen + life_steal_gain)
```

7. 幸运（`base_luck`，用于掉落/抽样修正）

推荐权重修正（归一化前）：

```text
w_i' = w_i * (1 + k_luck * base_luck)
P_i = w_i' / sum_j(w_j')
```

其中 `k_luck` 为策划参数，建议初始 `0.02 ~ 0.1`。

8. 单次命中结算顺序（建议）

```text
先算 final_cooldown / 发射事件
-> 再算 outgoing_hit_damage（含暴击）
-> 应用目标侧减伤与死亡判定
-> 结算 life_steal_gain
-> Tick 末结算 regen_hp_per_sec
```

### 4.6 movement（移动）

- `move_speed`：基础移速（`>= 0`）。当前为仅参考字段，游戏本体暂不接入结算。
- `move_speed_multiplier`：移速倍率（`> 0`）。当前为仅参考字段，游戏本体暂不接入结算。
- `pickup_radius`：拾取半径（`>= 0`）。当前为仅参考字段，游戏本体暂不接入结算。
- `dash`：冲刺子结构，包含 `enabled`、`cooldown_sec`、`duration_sec`、`distance`。当前为仅参考字段，游戏本体暂不接入结算。

### 4.7 resource（资源）

- `xp_gain_multiplier`：经验获取倍率（`> 0`）。当前为仅参考字段，游戏本体暂不接入结算。
- `gold_gain_multiplier`：金币获取倍率（`> 0`）。当前为仅参考字段，游戏本体暂不接入结算。
- `reroll_count`：重随次数（整数，`>= 0`）。当前为仅参考字段，游戏本体暂不接入结算。
- `banish_count`：移除词条次数（整数，`>= 0`）。当前为仅参考字段，游戏本体暂不接入结算。
- `revive_count`：复活次数（整数，`>= 0`）。当前为仅参考字段，游戏本体暂不接入结算。

### 4.8 progression（成长）

- `level_curve`：等级经验曲线参数。
- `stat_growth`：随时间或等级变化的属性增长参数。

推荐线性增长模型：

```text
H_t = H_base * (1 + k_hp * t)
A_t = A_base * (1 + k_atk * t)
```

推荐等级增长模型：

```text
stat(level) = stat_base * (1 + k1*(level-1) + k2*(level-1)^2)
```

### 4.9 loadout（构筑）

- `skills`：主动技能数组。字段兼容当前 `player_build.json`：`id/cooldown/radius/damage/max_targets`。
- `passives`：被动列表（每条含 `id`、`stacks`、`params`）。
- `weapon_bindings`：绑定远程武器配置（例如 `weapon_projectile_standard.json`）。

### 4.10 rules（战斗规则）

- `target_priority`：索敌优先级（如 `nearest`, `lowest_hp`, `densest_cluster`）。
- `collision_policy`：碰撞策略（如 `continuous`, `discrete_tick`）。
- `rng_stream_id`：角色 RNG 流 ID，保证可回放。

### 4.11 analysis_tags（分析标签）

- 用于实验分层，例如：`["baseline", "glass_cannon", "aoe_focus"]`。

## 5. 最小兼容映射（当前引擎）

当前项目直接生效字段可按如下映射：

- `simulation.player_hp` -> `player_build.player_hp`
- `simulation.max_survival_time_sec` -> `player_build.max_survival_time_sec`
- `loadout.skills[]` -> `player_build.skills[]`
- 以下字段当前降级为“仅参考”，默认不进入游戏本体结算：`status_power_multiplier`、`armor`、`evasion`、`block_chance`、`block_flat_reduction`、`move_speed`、`move_speed_multiplier`、`pickup_radius`、`dash`、`xp_gain_multiplier`、`gold_gain_multiplier`、`reroll_count`、`banish_count`、`revive_count`

其余字段可先用于离线分析与未来接入，不影响现有主循环。

## 6. 校验规则（导入前）

- 生命、冷却、时长类字段必须满足正值约束。
- 概率字段限制在 `[0,1]` 或 `[0,1)` 指定区间。
- 倍率字段必须定义合理上下界（建议 `0.1 ~ 10`）防止爆炸。
- `skills[].id` 不重复，且引用的武器/技能定义必须存在。
- 任何增长模型在 `t = max_survival_time_sec` 处需通过上限检查。

## 7. 建议接入步骤

1. 新增 `io` 加载器读取 `player_profile_template.json`。
2. 先把最小兼容字段投影到 `player_build` 运行。
3. 逐步把 `defense/movement/resource/progression` 接入系统结算。
4. 指标侧增加分层统计：按 `profile_id` 与 `analysis_tags` 汇总。
