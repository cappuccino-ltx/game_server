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



## 7, 具体流程

客户端注册:

- 1,查看是否已经注册
- 2,生成全局唯一 player_id(注册之后不再改变), 以及生成玩家的 初始世界坐标, 存储到 redis 中,
  ```json
  {
    "player_id": 123456,
    "position": {
      "x": 0,
      "y": 0,
      "z": 0,
      "scale": 0 // 这个是坐标的缩放因子, 因为坐标默认是浮点数的,这里用 int 来存储,所以需要将坐标乘 上缩放因子得到 int 型的坐标值, 如果要还原的话, 得除上缩放因子
    }
  }
  // 比如 x,y,z 是 101.23,48.24,10.01, 那 redis 中存储的以及网络中传输的就是 10123,4824,1001
- 3, 根据生成的坐标, 和战斗服的世界区块负责分配, 得到这个坐标对应的 区块 id 和战斗服 id
- 4, 写入这些数据到 mysql 和 redis 中,
  ```

客户端登录流程:

客户端发起 http 请求进行登录, 携带用户名和密码参数

登录服收到 http 请求之后, 
- 1, 验证用户名密码是否正确, 如果错误, 返回登录失败, player_id 返回 0 代表无效
- 2, 验证 redis 中是否存在 {username, session_id}  的映射,
  - 2.1 如果不存在, 则创建一个 session_id(全局唯一) 返回给用户, 并且查询数据库,将该玩家在上次退出的时候所在的区块 id 查出来, 根据区块 id,找到对应的 战斗服 id,进行 redis 的数据填充
  - 2.2 (可以不做, 留着之后扩展, 但是做也不难) 如果存在, 需要创建一个新的 session_id(全局唯一),进行更新 redis 中已有的数据, 并且通过 tcp 内部的 `ServerToGateway` protobuf 结构, 通过tcp内部服务,通知网关,删除缓存中的旧的 player_id 到 session_id 的映射 
- 3, 通过tcp内部服务, 通知对应的战斗服, 有新的玩家登录, 为其创建一个玩家实例在战斗服中,
- 4, 登录成功,需要返回玩家 player_id 和 session_id ,以及网关 域名(应该给域名的, 但是这里为了简单, 给定一个 ip 地址和端口) 给客户端

客户端连接网关:
- 1, 客户端根据网关 ip 和端口,通过 akcp 库发起连接, 连接成功之后, 发送 `envelope.proto` 中的`Envelope`结构, 内部需要填充 player_id 和 token 字段
- 2, 在网关中,根据 player_id 找到信息之后,对比 token 字段是否正确, 如果正确,直接转发给内部服务中的逻辑服或者战斗服, 封装结构为 `forward.proto` 中的  `GatewayToServer`

战斗服收到消息并处理
- 1, 收到网关转发来的消息之后, 反序列化, 根据类型 push 到 world 中进行定时更新, 
- 2, 更新完世界之后, 根据 aoi 进行状态同步, 将要发送的状态封装成`forward.proto` 中的 `ServerToGateway`, 转发给对应的网关, 由网关进行转发, 

网关转发消息给客户端:
- 1, 收到战斗服的消息之后, 反序列化, 封装成`envelope.proto` 中的`Envelope`结构, 然后序列化成二进制数据,
- 2, 根据 player_id 找到对应的客户端的 channel 句柄, 