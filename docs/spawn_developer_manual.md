# 刷怪配置开发手册（CSV v1）

本文档定义 3 张配置表的字段语义、校验规则与运行时串联逻辑，用于无渲染、固定步长（Tick）战斗模拟。

## 1. 设计目标

- 多表驱动：策划通过 CSV 调整节奏与怪物能力，不改 C++ 主循环。
- 可重复回放：同一输入配置 + 同一随机种子必得同一结果。
- 可分析：直接关联输出指标（击杀、存活怪峰值、技能效率）。
- 可扩展：通过 `schema_version` 兼容后续字段演进。

## 2. 文件格式与约定

- 文件路径：
  - `config/spawn_events.csv`（刷怪时间轴）
  - `config/monster_def.csv`（怪物基础能力）
  - `config/level_difficulty.csv`（关卡难度修正）
- 横向（列）：参数名。
- 纵向（行）：配置记录。
- `spawn_events.csv` 一行代表一个“事件窗口”，可生成多个单位（`count`）。
- 推荐编码：UTF-8（无 BOM）。

## 3. `spawn_events.csv` 字段说明（CSV v1）

| 字段名 | 类型 | 取值/范围 | 默认值 | 说明 |
|---|---|---|---|---|
| `schema_version` | string_enum | `v1` | `v1` | Schema 版本号，解析器可据此做兼容分支。 |
| `scenario_id` | string | 非空 | `default` | 场景标识，用于不同关卡或实验集。 |
| `variant_id` | string | 非空 | `A` | 方案标识（A/B 测试），如 `A` 线性、`B` 波段。 |
| `level_duration_sec` | int | `> 0` | `180` | 关卡总时长（秒），可配置。建议同一 `scenario_id+variant_id` 下保持一致。 |
| `event_id` | int | `>= 1` | 必填 | 事件唯一 ID。建议全局唯一，便于日志追踪。 |
| `trigger_time_sec` | float | `>= 0` | 必填 | 事件触发秒数（可读性字段）。 |
| `trigger_tick` | int | `>= 0` | 必填 | 事件触发 Tick（执行字段）。建议以该字段为准。 |
| `monster_id` | int | `>= 1` | 必填 | 怪物类型 ID（外键，关联 `monster_def.csv`）。 |
| `count` | int | `>= 1` | `1` | 本事件总生成数量。 |
| `spawn_angle_clock` | int | `[1, 12]` | `12` | 生成方向（时钟方向）。`12` 表示前方，`3` 右侧，`6` 后方，`9` 左侧。 |
| `spawn_distance_px` | float | `> 0` | `900` | 生成中心点距离玩家的半径（像素）。 |
| `spawn_random_radius_px` | float | `>= 0` | `120` | 以生成中心点为圆心的随机扰动半径（像素）。`0` 表示精确点生成。 |
| `interval_tick` | int | `>= 1` | `1` | 同事件内相邻单位的生成间隔（Tick）。 |
| `spawn_zone_id` | int | `>= 0` | `0` | 生成区域 ID（映射到网格/边界规则）。 |
| `seed_offset` | int | `>= 0` | `0` | 事件级随机偏移，和全局 seed 共同决定采样结果。 |
| `attr_hp_scale` | float | `[1.0, +inf)` | `1.0` | HP 修正系数（无上限）。 |
| `attr_atk_scale` | float | `[1.0, +inf)` | `1.0` | 攻击修正系数（如 touch_dps 乘子，无上限）。 |
| `attr_spd_scale` | float | `[1.0, +inf)` | `1.0` | 速度修正系数（无上限）。 |
| `phase_tag` | string_enum | `build_up/sustain/burst/rest` | `sustain` | 节奏标签，用于分析和可视化分段。 |
| `notes` | string | 可空 | 空 | 备注，不参与战斗逻辑。 |

## 4. `monster_def.csv` 字段说明（新增）

| 字段名 | 类型 | 取值/范围 | 默认值 | 说明 |
|---|---|---|---|---|
| `schema_version` | string_enum | `v1` | `v1` | Schema 版本号。 |
| `monster_id` | int | `>= 1` | 必填 | 怪物类型主键。 |
| `monster_name` | string | 非空 | 必填 | 怪物名称。 |
| `base_attack` | float | `>= 0` | 必填 | 基础攻击（单次伤害）。 |
| `base_attack_interval_sec` | float | `> 0` | 必填 | 基础攻击间隔（秒）。值越小，攻速越高。 |
| `base_attack_range_px` | float | `>= 0` | 必填 | 基础攻击射程（像素）。接触怪可设较小值。 |
| `base_hp` | float | `>= 1` | 必填 | 基础血量。 |
| `base_move_speed_px_sec` | float | `>= 0` | 必填 | 基础移动速度（像素/秒）。 |
| `notes` | string | 可空 | 空 | 备注，不参与战斗逻辑。 |

## 5. `level_difficulty.csv` 字段说明（新增）

说明：采用“全局 + 类型叠乘”模型。`monster_id=0` 表示全局行，`monster_id>0` 表示该怪物类型附加行。

| 字段名 | 类型 | 取值/范围 | 默认值 | 说明 |
|---|---|---|---|---|
| `schema_version` | string_enum | `v1` | `v1` | Schema 版本号。 |
| `scenario_id` | string | 非空 | 必填 | 场景标识。 |
| `variant_id` | string | 非空 | 必填 | 方案标识（A/B）。 |
| `phase_id` | int | `>= 1` | 必填 | 难度阶段 ID。 |
| `start_time_sec` | float | `>= 0` | 必填 | 阶段起始秒（含）。 |
| `end_time_sec` | float | `> start` | 必填 | 阶段结束秒（不含）。 |
| `monster_id` | int | `>= 0` | `0` | `0` 全局行，`>0` 类型行。 |
| `global_atk_scale` | float | `>= 1` | `1.0` | 攻击系数。 |
| `global_atk_interval_scale` | float | `> 0` | `1.0` | 攻击间隔系数。`<1` 为攻速加快。 |
| `global_atk_range_scale` | float | `>= 1` | `1.0` | 射程系数。 |
| `global_hp_scale` | float | `>= 1` | `1.0` | 血量系数。 |
| `global_move_speed_scale` | float | `>= 1` | `1.0` | 移速系数。 |
| `type_atk_scale` | float | `>= 1` | `1.0` | 类型攻击附加系数。 |
| `type_atk_interval_scale` | float | `> 0` | `1.0` | 类型攻速附加系数。`<1` 为更快。 |
| `type_atk_range_scale` | float | `>= 1` | `1.0` | 类型射程附加系数。 |
| `type_hp_scale` | float | `>= 1` | `1.0` | 类型血量附加系数。 |
| `type_move_speed_scale` | float | `>= 1` | `1.0` | 类型移速附加系数。 |
| `notes` | string | 可空 | 空 | 备注，不参与战斗逻辑。 |

## 6. 运行时串联公式（核心）

### 6.1 时间转换

给定 `fixed_dt`（秒）：

```text
trigger_tick = round(trigger_time_sec / fixed_dt)
```

示例：`fixed_dt = 0.05` 时，`15.0s -> 300 tick`。

### 6.2 事件内逐个生成时刻

```text
spawn_tick(i) = trigger_tick + i * interval_tick
i in [0, count - 1]
```

### 6.3 事件坐标生成（角度 + 距离 + 随机半径）

```text
theta = (spawn_angle_clock % 12) * 30deg
cx = px + cos(theta) * spawn_distance_px
cy = py + sin(theta) * spawn_distance_px
r = sqrt(U[0,1]) * spawn_random_radius_px
phi = U[0, 2pi)
x = cx + cos(phi) * r
y = cy + sin(phi) * r
```

### 6.4 怪物最终战斗参数（怪物基础 + 难度 + 事件）

记：

```text
G_* = 当前时间命中的全局难度系数（monster_id = 0）
T_* = 当前时间命中的类型难度系数（monster_id = 当前怪物；不存在则按 1.0）
```

最终参数：

```text
final_hp = base_hp * G_hp * T_hp * attr_hp_scale
final_attack = base_attack * G_atk * T_atk * attr_atk_scale
final_move_speed = base_move_speed_px_sec * G_move * T_move * attr_spd_scale
final_attack_range = base_attack_range_px * G_range * T_range
final_attack_interval_sec = base_attack_interval_sec * G_interval * T_interval
```

## 7. 两种节奏方案映射

`spawn_events.csv` 已给出两套样例：

- `variant_id = A`（线性递增型）  
  特征：`count` 与属性系数平滑上升，`rest` 极少。  
  目标：持续压测对象池、网格与碰撞吞吐稳定性。

- `variant_id = B`（波段冲击型）  
  特征：`burst` 与 `rest` 交替，峰谷明显。  
  目标：测试 DPS 与 AOE 在不同密度窗口的覆盖效率差异。

## 8. 校验规则（导入前必做）

### 8.1 `spawn_events.csv`

- `level_duration_sec > 0`。
- `trigger_tick >= 0` 且不超过 `level_duration_sec / fixed_dt`。
- `count >= 1`，`interval_tick >= 1`。
- `spawn_angle_clock` 必须在 `[1,12]`。
- `spawn_distance_px > 0`，`spawn_random_radius_px >= 0`。
- `attr_hp_scale >= 1.0`，`attr_atk_scale >= 1.0`，`attr_spd_scale >= 1.0`（无上限）。
- 同一 `scenario_id + variant_id` 下，事件按 `trigger_tick` 非递减排序。
- 单 Tick 生成预算不超标：  
  `sum(spawn_at_same_tick) <= max_spawn_per_tick_budget`。

### 8.2 `monster_def.csv`

- `monster_id` 唯一且必须被 `spawn_events.csv` 引用时存在。
- `base_attack_interval_sec > 0`。
- `base_hp >= 1`。

### 8.3 `level_difficulty.csv`

- 同一 `scenario_id + variant_id` 下，`[start_time_sec, end_time_sec)` 不允许重叠冲突。
- 每个阶段必须且仅有 1 条全局行（`monster_id=0`）。
- 类型行可选；缺失时类型系数按 `1.0`。
- `global_*` 与 `type_*` 的下限按字段约束校验。

## 9. 调参到“玩家体感”的映射（当前仅 DPS/AOE）

- 提升“持续压迫感”：提高 `count`、降低 `interval_tick`、提升 `attr_atk_scale`。
- 提升“清场成就感”：在 `burst` 前给 `rest` 段，拉大峰谷差（`count` 与 `interval_tick` 反向调）。
- 强调 DPS 检验：提高单体威胁（`base_hp`、`base_attack`、`global_hp_scale`、`global_atk_scale`）。
- 强调 AOE 检验：提高并发密度（`count`、小 `interval_tick`）并增大 `spawn_random_radius_px`。

## 10. 解析端建议（给 C++ 开发）

- 以 `trigger_tick` 为执行主键，`trigger_time_sec` 仅作日志显示。
- 每个事件使用 `global_seed ^ seed_offset ^ event_id` 构造局部 RNG，保证重放一致。
- 调度器建议单向游标（`next_event++`），复杂度 O(事件数)。
- 建立 `monster_id -> MonsterDef` 哈希映射，在加载阶段一次性校验外键。
- 难度解析顺序：先全局行，再类型行，运行时做系数叠乘。
- 导入失败要打印 `event_id` + 字段名 + 非法值，方便策划修表。

## 11. 版本演进建议

- `v2` 可新增：`elite_ratio`、`max_alive_cap`、`adaptive_gain`。
- 新增字段应保持向后兼容：旧列缺失时使用默认值，不破坏 `v1` 回放结果。
