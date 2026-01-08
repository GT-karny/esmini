# トラブルシューティング

このドキュメントでは、GT_esminiの使用中に発生する可能性のある問題と解決方法を説明します。

## ビルドエラー

### 問題: CMakeが見つからない

**症状:**
```
'cmake' is not recognized as an internal or external command
```

**解決策:**

**Windows:**
```powershell
# Visual Studio 2022のCMakeパスを確認
Test-Path "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"

# PATHに追加
$env:Path = "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin;" + $env:Path
```

**Linux:**
```bash
sudo apt-get install cmake  # Ubuntu/Debian
sudo dnf install cmake      # Fedora/RHEL
```

**macOS:**
```bash
brew install cmake
```

### 問題: エンコーディングエラー

**症状:**
```
error C2001: newline in constant
warning C4819: The file contains a character that cannot be represented in the current code page
```

**原因:** ソースコードに日本語コメントが含まれている

**解決策:**
すべてのソースコードのコメントを英語に変更してください。GT_esminiのソースコードは、すべて英語のコメントを使用しています。

### 問題: "storyBoard_" 未定義識別子

**症状:**
```
error C2065: 'storyBoard_': undeclared identifier
```

**原因:** Phase 1で修正済みの古いコードを使用している

**解決策:**
最新のGT_esminiコードを使用してください。`ExtraAction.cpp`が`storyBoard_`の代わりに`parent_`を使用していることを確認してください。

### 問題: ActionType::PRIVATE が見つからない

**症状:**
```
error C2039: 'PRIVATE': is not a member of 'scenarioengine::OSCAction::ActionType'
```

**原因:** Phase 1で修正済みの古いコードを使用している

**解決策:**
最新のGT_esminiコードを使用してください。`ExtraAction.cpp`が`ActionType::PRIVATE`の代わりに`ActionType::USER_DEFINED`を使用していることを確認してください。

### 問題: リンクエラー（未解決の外部シンボル）

**症状:**
```
error LNK2019: unresolved external symbol "public: __cdecl gt_esmini::VehicleLightExtension::VehicleLightExtension(class scenarioengine::Vehicle *)"
```

**原因:**
- GT_esminiLibが正しくリンクされていない
- ビルド順序の問題

**解決策:**
1. クリーンビルドを実行：
```powershell
# Windows
Remove-Item -Recurse -Force build
mkdir build
cd build
cmake .. -G "Visual Studio 17 2022" -A x64
cmake --build . --config Release
```

2. `CMakeLists.txt`でGT_esminiLibがリンクされていることを確認：
```cmake
target_link_libraries(MyApp GT_esminiLib)
```

## ランタイムエラー

### 問題: GT_Initが失敗する

**症状:**
```
Failed to initialize GT_esmini
```

**原因:**
- シナリオファイルが見つからない
- シナリオファイルのパースエラー

**解決策:**
1. ファイルパスを確認：
```cpp
// 絶対パスを使用
GT_Init("E:/Repository/GT_esmini/esmini/resources/xosc/cut-in.xosc", 0);

// または、相対パスを確認
GT_Init("../../resources/xosc/cut-in.xosc", 0);
```

2. シナリオファイルが有効なXMLであることを確認
3. esmini本体で読み込めるか確認：
```bash
./esmini --osc scenario.xosc
```

### 問題: ライトが点灯しない

**症状:**
- AutoLightを有効にしても、ライトが点灯しない
- `GT_GetLightState`が常に0（OFF）を返す

**原因と解決策:**

#### 原因1: AutoLightが有効化されていない

**解決策:**
```cpp
GT_Init("scenario.xosc", 0);
GT_EnableAutoLight(); // この行を追加
```

#### 原因2: 車両が減速していない（ブレーキランプ）

**解決策:**
減速度が-0.1G以下であることを確認：
```cpp
// 車両の速度を確認
SE_ScenarioObjectState state;
SE_GetObjectState(0, &state);
std::cout << "Speed: " << state.speed << std::endl;
```

#### 原因3: 車両がジャンクション内にいない（ウインカー）

**解決策:**
車両がジャンクション内にいるか確認：
```cpp
int junctionId = vehicle->pos_.GetJunctionId();
std::cout << "Junction ID: " << junctionId << std::endl;
```

#### 原因4: VehicleLightExtensionが登録されていない

**解決策:**
`GT_GetLightState`の戻り値を確認：
```cpp
int state = GT_GetLightState(0, 6);
if (state == -1)
{
    std::cerr << "Error: VehicleLightExtension not registered" << std::endl;
}
```

### 問題: LightStateActionがパースされない

**症状:**
- XOSCファイルに`LightStateAction`を記述しても、ライトが制御されない

**原因と解決策:**

#### 原因1: SE_Initを使用している

**解決策:**
`GT_Init`を使用してください：
```cpp
// NG: SE_Initを使用
SE_Init("scenario.xosc", 0, 0, 0, 0);

// OK: GT_Initを使用
GT_Init("scenario.xosc", 0);
```

#### 原因2: XOSCファイルの構文エラー

**解決策:**
XOSCファイルが正しい構文であることを確認：
```xml
<PrivateAction>
    <AppearanceAction>
        <LightStateAction>
            <LightType>
                <VehicleLight vehicleLightType="brakeLights"/>
            </LightType>
            <LightState mode="on"/>
        </LightStateAction>
    </AppearanceAction>
</PrivateAction>
```

## OSI出力の問題

### 問題: OSI出力にライト状態が含まれない

**症状:**
- OSI出力を受信しても、`light_state`フィールドが空

**原因と解決策:**

#### 原因1: GT_OSIReporterが使用されていない

**解決策:**
`ScenarioEngine/CMakeLists.txt`を確認：
```cmake
# OSIReporter.cppの代わりにGT_OSIReporter.cppを使用
set(SRC_SOURCEFILES
    # ...
    ../../GT_esmini/GT_OSIReporter.cpp  # この行があるか確認
    # SourceFiles/OSIReporter.cpp  # この行がコメントアウトされているか確認
)
```

クリーンビルドを実行：
```bash
rm -rf build
mkdir build
cd build
cmake ..
cmake --build . --config Release
```

#### 原因2: OSIが無効化されている

**解決策:**
CMake設定でOSIが有効であることを確認：
```bash
cmake .. -DUSE_OSI=ON
```

### 問題: UDPソケットが開けない

**症状:**
```
Failed to open OSI socket
```

**原因と解決策:**

#### 原因1: ポートが既に使用されている

**解決策:**
別のポート番号を試す：
```cpp
SE_OpenOSISocket("127.0.0.1:48199"); // 48198の代わりに48199を使用
```

#### 原因2: ファイアウォールがブロックしている

**解決策:**
ファイアウォール設定を確認し、ポートを開放してください。

## デバッグ方法

### ライト状態のログ出力

```cpp
GT_Init("scenario.xosc", 0);
GT_EnableAutoLight();

for (int i = 0; i < 1000; ++i)
{
    GT_Step(0.05);
    
    // すべてのライト状態を表示
    int vehicleId = 0;
    std::cout << "Time: " << (i * 0.05) << "s" << std::endl;
    
    for (int lightType = 0; lightType <= 12; ++lightType)
    {
        int state = GT_GetLightState(vehicleId, lightType);
        if (state != 0) // OFFでない場合のみ表示
        {
            std::cout << "  Light " << lightType << ": " << state << std::endl;
        }
    }
}

GT_Close();
```

### 車両状態のログ出力

```cpp
SE_ScenarioObjectState state;
SE_GetObjectState(0, &state);

std::cout << "Position: (" << state.x << ", " << state.y << ")" << std::endl;
std::cout << "Speed: " << state.speed << " m/s" << std::endl;
std::cout << "Heading: " << state.h << " rad" << std::endl;
```

### OSI出力の確認

Pythonスクリプトを使用してOSI出力を確認：

```python
#!/usr/bin/env python3
import socket
from osi3.osi_groundtruth_pb2 import GroundTruth

sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock.bind(('127.0.0.1', 48198))

while True:
    data, addr = sock.recvfrom(65536)
    gt = GroundTruth()
    gt.ParseFromString(data)
    
    for obj in gt.moving_object:
        if obj.HasField('vehicle_classification'):
            light = obj.vehicle_classification.light_state
            print(f"Vehicle {obj.id.value}:")
            print(f"  Brake: {light.brake_light_state}")
            print(f"  Indicator: {light.indicator_state}")
```

## よくある質問

### Q: GT_InitとSE_Initの違いは？

**A:** 
- `GT_Init`: GT_ScenarioReaderを使用し、LightStateActionをパース可能
- `SE_Init`: esmini本体のScenarioReaderを使用し、LightStateActionはパース不可

AutoLight機能のみを使用する場合は、`SE_Init`でも可能です。

### Q: AutoLightとLightStateActionを併用できますか？

**A:** はい、可能です。LightStateActionが実行された時点で、AutoLightの状態が上書きされます。詳細は[AutoLight機能](05_auto_light.md)を参照してください。

### Q: ライト状態は視覚的に表示されますか？

**A:** 現在のバージョンでは、OSGビューアーでの視覚的表現はサポートされていません。ライト状態はOSI出力に反映されます。

### Q: カスタムライトタイプを追加できますか？

**A:** はい、可能です。`VehicleLightType`列挙型に新しいタイプを追加し、パース処理とOSI出力処理を追加してください。詳細は[アーキテクチャ](08_architecture.md)を参照してください。

## サポート

問題が解決しない場合は、以下の情報を含めてGitHubのIssueを作成してください：

- GT_esminiのバージョン
- esminiのバージョン
- OS（Windows/Linux/macOS）
- エラーメッセージの全文
- 再現手順
- 使用しているXOSCファイル（可能な場合）

## 次のステップ

- [基本的な使い方](03_basic_usage.md) - GT_esminiの基本
- [サンプルシナリオ](07_examples.md) - 実用的な使用例

## 関連ドキュメント

- [ビルド・インストール](02_build_install.md) - ビルド手順
- [アーキテクチャ](08_architecture.md) - 内部構造
