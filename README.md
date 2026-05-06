# Simulator Battle (Headless)

高性能 C++ 无头（Headless）割草战斗模拟器。  
目标是把“海量单位逻辑结算”从渲染中解耦，做可重复、可批量、可量化的离线推演。

## 1. 项目目标

- 用固定步长 Tick（`fixed_dt`）运行纯逻辑模拟，不依赖渲染帧。
- 通过对象池 + 空间网格，把大规模碰撞从全量扫描降到局部查询。
- 通过 JSON 配置驱动战斗（角色构筑 + 关卡时间轴），支持快速扫参与 A/B 测试。
- 输出 CSV 指标，便于后续 Python/BI 可视化分析。

## 2. 目录结构

```text
simulator_battle/
├─ config/
│  ├─ player_build.json
│  └─ level_design.json
├─ src/
│  ├─ core/       # 核心数据结构（配置/技能/波次/Hitbox）
│  ├─ ecs/        # 实体池（SoA + dense/sparse）
│  ├─ spatial/    # 空间划分（Uniform Grid）
│  ├─ systems/    # 各系统（Wave/Movement/Skill/Combat/GameDirector）
│  ├─ io/         # 配置读取、JSON 解析、指标落盘
│  └─ main.cpp    # 程序入口
└─ CMakeLists.txt
```

## 3. 架构说明

### 3.1 Tick 执行顺序

每个 Tick 按固定顺序执行：

1. `WaveSystem`：按时间轴激活怪物到对象池。
2. `MovementSystem`：怪物朝玩家移动并结算贴脸伤害。
3. `UniformGrid::Rebuild`：重建空间桶链表。
4. `SkillSystem`：技能 CD 递减并产出 Hitbox 事件。
5. `CombatSystem`：仅查询命中域覆盖的网格单元并结算伤害/死亡。
6. `MetricsLogger`：更新峰值、时间线、累计伤害与击杀。

### 3.2 ECS / DOD 实现重点

- **SoA 存储**：`MonsterPool` 按字段拆向量存储（`x/y/hp/speed/...`）。
- **对象池复用**：`Activate/Deactivate` O(1)，避免运行期频繁 `new/delete`。
- **Dense 遍历**：只遍历活跃实体，减少分支和无效内存访问。
- **网格桶链表**：每格只存头节点，实体通过 `next_in_cell` 串联，降低容器开销。

## 4. 配置文件

## `config/player_build.json`

控制角色基础属性与技能表。

字段说明：

- `player_hp`：初始生命值（`> 0`）。
- `max_survival_time_sec`：最大生存时间上限（秒，`> 0`）。
- `skills[]`：技能数组。
- `skills[].id`：技能标识（内置示例：`garlic`、`sword_ring`）。
- `skills[].cooldown`：技能冷却（秒，`> 0`）。
- `skills[].radius`：判定半径（`> 0`）。
- `skills[].damage`：单次伤害（`>= 0`）。
- `skills[].max_targets`：单次最多命中目标数（`> 0`）。

## `config/level_design.json`

控制模拟步长、对象池容量、空间网格参数，以及 CSV 刷怪源。

字段说明：

- `fixed_dt`：固定步长（秒，`> 0`）。
- `pool.max_monsters`：怪物池容量（`> 0`）。
- `grid.half_extent`：网格世界半径（`> 0`）。
- `grid.cell_size`：网格单元边长（`> 0`）。
- `spawn_csv.file`：CSV 路径（默认 `config/spawn_events.csv`）。
- `spawn_csv.scenario_id`：场景筛选键（如 `baseline_horde`）。
- `spawn_csv.variant_id`：方案筛选键（如 `A/B`）。
- `spawn_csv.world_units_per_px`：像素到世界坐标缩放（`> 0`）。
- `spawn_csv.max_spawn_per_tick_budget`：同 Tick 预算上限（导入校验）。
- `monster_csv.file`：怪物基础能力表（`config/monster_def.csv`）。
- `difficulty_csv.file`：难度系数表（`config/level_difficulty.csv`）。
- 详细事件字段见 `docs/spawn_developer_manual.md` 与 `config/spawn_events.csv`。

## 多表串联规则

- 生成事件来自 `spawn_events.csv`，执行主键为 `trigger_tick`。
- `spawn_events.monster_id` 外键关联 `monster_def.monster_id`。
- 运行时按当前时间命中 `level_difficulty.csv` 的全局行（`monster_id=0`）和类型行（`monster_id>0`）。
- 最终参数按手册公式叠乘：`怪物基础 × 全局难度 × 类型难度 × 事件系数`。
- 玩家受击采用离散攻击模型：怪物进入射程且攻击冷却到期时造成一次伤害。

## 5. 构建与运行

> 需要本机可用 C++20 编译工具链与 CMake。

```bash
cmake -S . -B build
cmake --build build -j
./build/simulator_battle
```

Windows 下可执行文件通常为：

```powershell
.\build\simulator_battle.exe
```

## 6. 输出结果

程序运行后会生成：

- `metrics_summary.csv`：总览指标（模拟时长、总击杀、峰值存活怪等）。
- `metrics_timeline.csv`：时间线指标（每秒存活怪/累计击杀）。
- `metrics_waves.csv`：波次阶段总结（每波计划/实际刷怪、阶段累计击杀、存活怪数量等）。

可用于：

- 不同 Build（技能、装备）对存活率影响对比。
- 关卡难度曲线调优（波次密度、怪物强度）。
- 技能贡献度分析（伤害占比、触发频率）。

## 7. 性能与工程建议

- 优先调 `grid.cell_size`，平衡“每格实体数”与“查询格子数”。
- 保持 `max_monsters` 贴近上限场景，避免频繁容量不足导致刷怪失败。
- 若要继续提升吞吐，可增加：
  - SIMD 批量移动/距离判断。
  - Job System 分块并行（按 Cell/Chunk 分区）。
  - 更紧凑的位标记和批量死亡回收。

## 8. 常见问题

- **启动报配置错误**：检查 `spawn_csv` 必填字段、CSV 列完整性、`trigger_tick` 及预算校验。
- **技能统计异常**：检查 `player_build.json` 是否出现重复 `skill_id`（已在加载时校验）。
- **刷怪不足**：检查 `pool.max_monsters` 是否过小，或战斗节奏导致池持续占满。

## 9. 后续扩展方向

- 引入经验/升级/自动选词条系统。
- 支持多技能形状（扇形、矩形、穿透投射）。
- 支持随机种子批量实验与回放文件导出。
- 增加单元测试与基准测试（不同实体规模的吞吐曲线）。
# skill_effect_simulator
