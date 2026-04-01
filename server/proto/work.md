# MMO 分布式通信协议设计（Protobuf）

## 1. 设计目标

- Client ↔ Gateway：UDP + KCP，承载客户端统一封包
- Gateway ↔ Battle：TCP，承载实时战斗消息转发与批量下行
- Gateway ↔ Logic：TCP，承载非实时逻辑请求与回包
- Gateway 无状态转发，路由依赖 `player_id -> battle_id`
- 序列化统一使用 protobuf

## 2. 分层协议模型

- 外层（客户端链路）：`ClientEnvelope`
  - 头部：`cmd/seq/player_id/timestamp/qos`
  - 包体：业务消息二进制 `bytes body`
- 内层（服务间链路）：`GwToBattle/GwToLogic/BattleToGwBatch/LogicToGwBatch`
  - 路由头必须包含 `player_id`
  - Battle/Logic 到 Gateway 支持批量聚合，降低发送开销

## 2.1 浮点与量化双结构

- 低频链路与管理类消息保留浮点结构：`Vector3`、`Rotation`
- 高频链路（移动/状态帧/技能目标点）优先使用量化结构：
  - `QuantizedVector3`：`sint32 + scale`，默认建议 `scale=100`（厘米精度）
  - `PackedLookAngles`：`yaw16/pitch16` 打包为 `uint32`
- 灰度迁移期允许同时携带浮点字段与量化字段，收包侧优先读取量化字段
- 推荐规则：
  - `yaw` 使用 `[0, 360)` 映射到 16 位
  - `pitch` 使用业务定义范围（常见 `[-90, 90]`）映射到 16 位

## 3. message_id 划分方案

采用 32 位整数编码：

`message_id = (direction << 24) | (module << 16) | action`

- direction（8 bit）
  - `0x01` C2G
  - `0x02` G2C
  - `0x11` GW2BATTLE
  - `0x12` BATTLE2GW
  - `0x21` GW2LOGIC
  - `0x22` LOGIC2GW
- module（8 bit）
  - `0x01` AUTH
  - `0x02` SESSION
  - `0x03` MOVE
  - `0x04` STATE
  - `0x05` SKILL
  - `0x06` LOGIC
  - `0x0F` GATEWAY
  - `0x1F` INTERNAL
- action（16 bit）
  - 由各模块独立维护，从 `0x0001` 递增

示例：

- `C2G.AUTH_BIND_REQ`：`0x01010001`
- `G2C.AUTH_BIND_ACK`：`0x02010002`
- `C2G.MOVE_INPUT_REQ`：`0x01030001`
- `BATTLE2GW.STATE_FRAME_NTF`：`0x12040003`

## 4. Proto 模块拆分

- `proto/common.proto`：向量、基础类型、QoS
- `proto/common.proto`：向量、旋转、量化坐标、量化朝向、QoS
- `proto/ids.proto`：方向/模块/动作枚举
- `proto/envelope.proto`：客户端封包
- `proto/forward.proto`：网关转发与批量帧
- `proto/auth.proto`：登录、心跳、踢人、鉴权联动
- `proto/move.proto`：移动输入、纠偏、传送
- `proto/move.proto`：移动输入、纠偏、传送（含量化字段）
- `proto/state.proto`：AOI 进出、状态帧增量（含量化字段）
- `proto/skill.proto`：施法、技能事件、打断（目标点支持量化字段）
- `proto/logic.proto`：背包与任务同步

## 5. 关键流程映射

- 登录：
  - Client 发 `C2G_AuthBind`
  - Gateway 发 `GW2LOG_VerifyTokenReq`
  - Logic 回 `LOG2GW_VerifyTokenRsp`
  - Gateway 回 `G2C_AuthResult`，并通知 Battle `GW2BATTLE_PlayerAttach`
- 移动：
  - Client 发 `C2S_MoveInput`
  - Battle 计算后回 `S2C_MoveDelta`
  - AOI 广播放入 `S2C_StateFrame`
- 技能：
  - Client 发 `C2S_CastStart`
  - Battle 回 `S2C_CastStarted`
  - 后续事件推送 `S2C_SkillEvent`
- 非实时逻辑：
  - Client 请求 `C2S_RequestInventory`
  - Logic 回 `S2C_InventorySnapshot` / `S2C_ItemAdded` / `S2C_QuestUpdate`

## 6. 网关转发约束

- 所有 Gateway 内部转发消息必须带 `player_id`
- 转发头建议包含 `session_id/zone_id/battle_id/gateway_id/cmd/trace_id`
- 网关仅做路由与连接管理，不持有业务状态