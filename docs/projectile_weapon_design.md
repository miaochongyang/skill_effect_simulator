# 远程弹道武器 JSON 设计（Roguelike）

## 目标

本文档定义了一个数据驱动的远程弹道技能 JSON 格式，用于肉鸽游戏。
该格式只关注模拟层逻辑：时间、几何、命中规则和状态变更。
不包含渲染、音频或特效字段。

## 设计原则

1. 稳定的 Schema 键名：保持字段命名长期稳定，便于后续新增强化内容且不破坏旧配置。
2. 确定性模拟：所有随机行为使用可复现的 RNG 流 ID。
3. 明确单位：每个数值字段都有单位约定（`world_unit`、`second`、`radian`）。
4. 强化通道安全：所有强化都写入命名通道，并定义叠加规则与钳制边界。
5. 运行时最小状态：运行时只保留 CD/充能/RNG 快照，静态参数都留在配置中。

## 文件

- 示例文件：`config/weapon_projectile_standard.json`
- Schema 版本：`v1`

## 顶层字段说明

- `schema_version`：配置 Schema 版本号。
- `weapon_id`：武器唯一 ID。
- `weapon_type`：该 Schema 下应为 `projectile_ranged`。
- `tags`：用于掉落池、成就、平衡分析的标签集合。
- `runtime_units`：声明数值单位，方便校验和工具链统一。
- `base`：发射节奏与连发（burst）行为。
- `projectile`：弹体移动与寿命行为。
- `collision`：碰撞形状与碰撞掩码。
- `damage`：命中载荷与每目标命中频率限制。
- `status_effects`：状态效果列表（如减速、中毒等）。
- `targeting`：发射前的目标选择策略。
- `rng`：随机数可复现配置。
- `upgrade_channels`：可被强化修改的通道及其边界/叠加策略。
- `upgrade_options`：肉鸽随机强化项（权重、稀有度、最大可选次数）。
- `evolution`：武器进化条件与结果映射。
- `network_snapshot`：回滚/回放所需的最小状态字段集合。

## 强化兼容模型

所有强化仅修改 `upgrade_channels` 中声明的通道。
每个通道定义：

- `base`：强化前基准值。
- `min`/`max`：数值安全边界。
- `stack_mode`：多条强化叠加方式。

推荐叠加顺序（每个通道一致）：

1. 先应用所有 `add`。
2. 再应用所有 `mul`。
3. 最后钳制到 `[min, max]`。

该策略可避免叠加顺序歧义，并保证跨平台结果一致。

## 修饰器（Modifier）规范

`upgrade_options[].modifiers[]` 每项包含：

- `channel`：目标通道，必须存在于 `upgrade_channels`。
- `op`：操作类型，仅支持 `add` 或 `mul`。
- `value`：操作数值。

示例：

- `{"channel":"cooldown","op":"mul","value":0.92}` 表示冷却乘 0.92（约提升 8% 攻速）。
- `{"channel":"projectiles_per_shot","op":"add","value":1}` 表示单次发射弹体数 +1。

## 肉鸽扩展策略

在不破坏旧 Schema 的前提下新增内容，建议：

1. 在 `upgrade_channels` 中新增通道定义。
2. 在 `upgrade_options` 中新增引用该通道的强化项。
3. 保持旧通道语义不变，保证旧种子/回放可复现。
4. 通过 `max_pick`、稀有度和钳制边界限制极端组合。
5. 通过 `evolution.result_weapon_id` 做进化分支跳转，避免改旧武器结构。

## 建议校验规则

1. `cooldown > 0`。
2. `projectile.speed >= 0`。
3. 至少开启一个终止条件：`max_lifetime > 0` 或 `max_travel_distance > 0`。
4. `collision.radius >= 0`。
5. `crit_chance` 在 `[0, 1)`。
6. `slow_ratio` 在 `[0, 1]`。
7. `upgrade_options[].modifiers[].channel` 必须存在于 `upgrade_channels`。
8. `upgrade_options[].max_pick >= 1`。
9. `weight > 0`。
10. `schema_version` 必须为引擎支持的版本。

## 在本仓库中的接入建议

- 当前项目主要通过 `player_build.json` 加载简化 AoE 技能。
- 该弹道 Schema 属于前向兼容设计，可独立演进。
- 具体接入步骤：
  - 为 `weapon_type == projectile_ranged` 增加加载分支。
  - 将 JSON 字段映射到内部弹道 Def POD。
  - 复用现有确定性 Tick/碰撞流程。
  - 状态效果剩余时长放在怪物状态中，不放在武器运行时状态中。
