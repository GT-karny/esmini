#include <iostream>
#include <vector>
#include "esminiLib.hpp"
#include "reporter/StubReporter.hpp"

int main(int argc, char* argv[]) {
    // 1. レポーターの初期化 (現在はスタブ)
    StubReporter reporter;

    // 2. esmini の初期化
    // 引数がない場合はデフォルトのシナリオを使用する例
    if (argc < 2) {
        std::cout << "Usage: MySimulator <osc_path>" << std::endl;
        // テスト用に esmini のリソースを参照
        const char* default_args[] = {
            "MySimulator",
            "--osc", "../resources/xosc/cut-in.xosc",
            "--headless"
        };
        if (SE_InitWithArgs(4, default_args) != 0) {
            return 1;
        }
    } else {
        if (SE_InitWithArgs(argc, (const char**)argv) != 0) {
            return 1;
        }
    }

    std::cout << "Simulation started..." << std::endl;

    // 3. シミュレーションループ
    while (SE_GetQuitFlag() == 0) {
        // ステップ実行 (デルタタイムは esmini 内部で計算)
        SE_Step();

        // 車両状態の取得とレポート
        int num_objects = SE_GetNumberOfObjects();
        std::vector<SE_ScenarioObjectState> states;
        for (int i = 0; i < num_objects; ++i) {
            SE_ScenarioObjectState state;
            SE_GetObjectState(i, &state);
            states.push_back(state);
        }

        reporter.report(states);

        // 必要に応じて終了判定
        if (SE_GetSimulationTime() > 20.0f) {
            break;
        }
    }

    // 4. 終了処理
    SE_Close();
    std::cout << "Simulation finished." << std::endl;

    return 0;
}
