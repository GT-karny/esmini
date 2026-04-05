# Scenario Variables (SV) Bridge User Guide

GT_Sim はシミュレーション実行中に、OpenSCENARIO の `VariableDeclarations` で宣言された変数の値を **UDP マルチキャスト** でリアルタイム配信します。外部ツール（Unreal Engine、Unity、Python スクリプトなど）からこのストリームを受信することで、シナリオの状態をリアルタイムに監視・連携できます。

## 概要

GT_Sim.exe を起動すると、内部で SV Bridge が自動的に開始されます。
シミュレーション実行中にシナリオ変数が存在する場合、その値が UDP マルチキャストで配信されます。

```
┌──────────────────────────────────────────────────┐
│ GT_Sim.exe (Electron)                            │
│                                                  │
│  ┌─────────┐  UDP unicast   ┌──────────┐        │
│  │ シミュ   │ ─────────────> │ SV Bridge│──┐     │
│  │ レーション│  127.0.0.1     │          │  │     │
│  │ エンジン  │  :48200        │          │  │     │
│  └─────────┘                 └──────────┘  │     │
│                                   │        │     │
│                              WebSocket     │     │
│                                   v        │     │
│                              ┌────────┐    │     │
│                              │ GUI    │    │     │
│                              │ パネル  │    │     │
│                              └────────┘    │     │
└──────────────────────────────────────────────────┘
                                             │
                                      UDP multicast
                                      239.0.0.1:48201
                                             │
                                             v
                                   ┌──────────────────┐
                                   │ 外部ツール        │
                                   │ (UE/Unity/Python) │
                                   └──────────────────┘
```

1. GT_Sim.exe を起動するだけで SV Bridge は自動的に動作します（追加設定不要）
2. シミュレーション実行中、エンジンが毎ステップで全変数を JSON 化し SV Bridge へ送信
3. SV Bridge が以下の 2 経路で再配信:
   - **UDP マルチキャスト** — 外部ツール向け（本ガイドのメイン対象）
   - **WebSocket** — GT_Sim 画面内の「Scenario Variables」パネル

## ネットワーク設定

| 項目 | デフォルト値 | 環境変数 |
|------|-------------|----------|
| マルチキャストグループ | `239.0.0.1` | `GT_SIM_SV_MULTICAST_GROUP` |
| マルチキャストポート | `48201` | `GT_SIM_SV_MULTICAST_PORT` |
| ユニキャストポート (内部) | `48200` | `GT_SIM_SV_PORT` |

- マルチキャストグループ・ポートはシミュレーション画面の Scenario Variables パネル上部にも表示されます
- 通常はデフォルト値のまま使用できます。変更が必要な場合は、GT_Sim.exe 起動前に環境変数を設定してください

## データ形式

### パケット構造

マルチキャストで配信されるデータは **生の JSON テキスト** (UTF-8) です。
ヘッダは付与されず、受信バッファの内容がそのまま JSON になります。

### JSON スキーマ

```json
{
  "sim_time": 3.000,
  "variables": {
    "warningActive": true,
    "collisionRisk": 1,
    "speedLimit": 60.0,
    "message": "Approaching slow vehicle"
  }
}
```

| フィールド | 型 | 説明 |
|------------|-----|------|
| `sim_time` | `number` | シミュレーション時刻 (秒) |
| `variables` | `object` | シナリオ変数のキー・値ペア |

### 変数の型マッピング

OpenSCENARIO の `VariableDeclaration` で宣言した型に応じて、JSON の値型が決まります。

| OpenSCENARIO `variableType` | JSON の型 | 例 |
|----|----|----|
| `boolean` | `true` / `false` | `"warningActive": true` |
| `integer` | `number` (整数) | `"collisionRisk": 1` |
| `double` | `number` (浮動小数点) | `"speedLimit": 60.0` |
| `string` | `string` | `"message": "hello"` |

### 更新頻度

シミュレーション毎ステップ (通常 100 Hz) で配信されます。受信側でフレームレートに合わせた間引きを行ってください。

## 外部ツールからの受信方法

GT_Sim.exe でシミュレーションを実行している間、同一マシンまたは同一ネットワーク上の外部プログラムから以下のようにマルチキャストを受信できます。

### Python

```python
import json
import socket
import struct

MULTICAST_GROUP = "239.0.0.1"
MULTICAST_PORT = 48201

sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM, socket.IPPROTO_UDP)
sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
sock.bind(("", MULTICAST_PORT))

# マルチキャストグループに参加
mreq = struct.pack("4s4s",
    socket.inet_aton(MULTICAST_GROUP),
    socket.inet_aton("0.0.0.0"))
sock.setsockopt(socket.IPPROTO_IP, socket.IP_ADD_MEMBERSHIP, mreq)

print(f"Listening on {MULTICAST_GROUP}:{MULTICAST_PORT}")

while True:
    data, addr = sock.recvfrom(65535)
    msg = json.loads(data)
    print(f"[t={msg['sim_time']:.3f}] {msg['variables']}")
```

### C# (Unity)

```csharp
using System.Net;
using System.Net.Sockets;
using System.Text;
using UnityEngine;

public class SvReceiver : MonoBehaviour
{
    UdpClient client;
    IPEndPoint ep;

    void Start()
    {
        ep = new IPEndPoint(IPAddress.Any, 48201);
        client = new UdpClient();
        client.Client.SetSocketOption(
            SocketOptionLevel.Socket,
            SocketOptionName.ReuseAddress, true);
        client.Client.Bind(ep);
        client.JoinMulticastGroup(IPAddress.Parse("239.0.0.1"));
        client.BeginReceive(OnReceive, null);
    }

    void OnReceive(System.IAsyncResult result)
    {
        byte[] data = client.EndReceive(result, ref ep);
        string json = Encoding.UTF8.GetString(data);
        Debug.Log($"SV: {json}");
        // JsonUtility.FromJson<T>() 等でパースしてください
        client.BeginReceive(OnReceive, null);
    }

    void OnDestroy()
    {
        client?.Close();
    }
}
```

### C++ (Unreal Engine / ネイティブ)

```cpp
#include <WinSock2.h>
#include <WS2tcpip.h>
#pragma comment(lib, "Ws2_32.lib")

// ソケット作成
SOCKET sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
int reuse = 1;
setsockopt(sock, SOL_SOCKET, SO_REUSEADDR,
           (char*)&reuse, sizeof(reuse));

sockaddr_in addr{};
addr.sin_family = AF_INET;
addr.sin_port = htons(48201);
addr.sin_addr.s_addr = INADDR_ANY;
bind(sock, (sockaddr*)&addr, sizeof(addr));

// マルチキャストグループに参加
ip_mreq mreq{};
inet_pton(AF_INET, "239.0.0.1", &mreq.imr_multiaddr);
mreq.imr_interface.s_addr = INADDR_ANY;
setsockopt(sock, IPPROTO_IP, IP_ADD_MEMBERSHIP,
           (char*)&mreq, sizeof(mreq));

// 受信ループ
char buf[65535];
while (true) {
    int len = recv(sock, buf, sizeof(buf) - 1, 0);
    if (len > 0) {
        buf[len] = '\0';
        // buf は JSON 文字列: {"sim_time":...,"variables":{...}}
        // nlohmann::json 等でパースしてください
    }
}
```

## シナリオ側の設定

SV Bridge が配信するのは、OpenSCENARIO の `<VariableDeclarations>` で宣言された変数のみです。
変数が未宣言のシナリオでは、SV ストリームは何も送信しません。

### 変数宣言の例

```xml
<VariableDeclarations>
    <VariableDeclaration name="warningActive"
                         variableType="boolean" value="false"/>
    <VariableDeclaration name="collisionRisk"
                         variableType="integer" value="0"/>
    <VariableDeclaration name="speedLimit"
                         variableType="double"  value="60.0"/>
    <VariableDeclaration name="message"
                         variableType="string"  value="Normal"/>
</VariableDeclarations>
```

### 変数値の変更

シナリオ中で `VariableAction` を使って値を変更できます:

```xml
<GlobalAction>
    <VariableAction variableRef="warningActive">
        <SetAction value="true"/>
    </VariableAction>
</GlobalAction>
```

変更はただちに次のステップの SV ストリームに反映されます。

テスト用サンプルシナリオ: `resources/xosc/sv_bridge_test.xosc`

## GT_Sim 画面での確認

シミュレーション実行中、GT_Sim 画面右下の **Scenario Variables** パネルにリアルタイムで変数名・型・値が表示されます。マルチキャストの接続先アドレスもパネル上部に表示されるため、外部ツールの設定時に参照できます。

## トラブルシューティング

| 症状 | 原因と対処 |
|------|-----------|
| 受信データが空 | シナリオに `VariableDeclarations` があるか確認 |
| マルチキャストが届かない | ファイアウォールで UDP 48201 を許可。同一マシン内では通常問題なし |
| 複数の受信プログラムで同時受信したい | マルチキャストなので複数リスナーが同時受信可能。`SO_REUSEADDR` を設定すること |
| JSON が途中で切れる | 1 パケットの最大サイズは 8192 バイト。変数の数や文字列長が多すぎる場合は変数名を短くする |
