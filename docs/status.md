# 開発の記録と、次にやること

最終更新: 2026-10-12（v0.3.0 公開時点）。いったん開発を止めるので、再開するときはここから読んでください。

## 1. いまの状態

* **公開中の最新版: v0.3.0**（https://github.com/naosyke/rt-doctor/releases/tag/v0.3.0）
* 1行インストール: `curl -fsSL https://github.com/naosyke/rt-doctor/releases/latest/download/install.sh | sh`
* ライセンス: Apache-2.0（オープンコア方針。下の「6. 決めたこと」を参照）
* 最初に決めたロードマップは、すべて完了しています。
* テスト: 45個（単体テストと、実際に ftrace を使う実地テスト）。CI は push のたびに x86-64 と arm64 で実行しています。

## 2. ここまでにやったこと

| 版 | 内容 |
|---|---|
| フェーズ1 | `rtdoctor check`: リアルタイム性能に関わる設定を13項目診断。影響の大きい順に並べ、一番の要因を先頭に表示 |
| フェーズ2 | 影響の大きさ（致命的・大・中・小）の導入。スワップは実際の使用量で判定 |
| フェーズ3 | `rtdoctor trace`: 指定したプログラムが待たされていた間に、その CPU で何が動いていたかを ftrace で調べる |
| フェーズ4 | `rtdoctor report`: 1枚で完結する HTML レポート。`--lang ja` で日本語 |
| v0.1.0 | x86-64 / arm64 の静的バイナリ、CI、タグでの自動リリース、1行インストール（改ざんチェックつき） |
| v0.2.0 | 対話モード（`sudo rtdoctor`）。コンテナ内での `--pid` に対応。README と使い方ガイドを日本語化 |
| v0.3.0 | `rtdoctor latency`: CPU ごとの起床遅延の測定（cyclictest と同じ方式）。`--trace` で遅れの原因も表示。レポートと対話モードにも組み込み |

### 確かめた結果

* **trace のアドバイスは効く**: 邪魔役（hog）と同じ CPU の 10 ms 周期プログラムで、期限（4 ms）切れが 500 回中 3 回 → `chrt -f 50` を付けると 0 回。
  hog に待たされた時間は 460 ms → 3.3 ms。
* **latency の値は cyclictest と一致する**: 同じ条件で2回ずつ比べ、最小・最大とも同じ範囲（Docker Desktop 上で最大 6〜7 ms）。
* **配布物はどの Linux でも動く**: x86-64 / arm64 × Ubuntu、Debian 11、Fedora、Amazon Linux 2、Alpine、BusyBox の10通り。
* **check の指摘は実際の問題と一致した**: vehicle-resource-manager で起きた問題（仮想マシンの停止、優先度の権限なし、CPU の共有）を、
  check が指摘していた。ただし THP とスワップを直しても、仮想マシンの影響が大きすぎて差は出なかった。

### 測定はすべて仮想マシン上

ここまでの数字は、すべて Mac の Docker Desktop（仮想マシン）で測ったものです。仮想マシンでは「一番の要因: 仮想マシン」としか
言えず、**設定を直して遅延が良くなる様子は、まだ一度も実機で示せていません**。これが一番の課題です。

## 3. 次にやること（優先順）

### (1) 実機での実証（最優先）

売り込みの説得力が大きく変わるので、最初にやります。

1. 実機の Linux を用意する。Raspberry Pi 5（1万円台）か、使っていない PC。
2. 標準の状態で測る。
   ```bash
   sudo rtdoctor report --lang ja --latency 300 -o before.html
   ```
   このとき、`stress-ng --cpu 4 --io 2 --vm 1` などで負荷をかけておく。
3. check の指摘を、影響の大きいものから1つずつ直し、そのたびに `latency` を測り直す。
   直す順番の例: PREEMPT_RT カーネル → CPU の隔離（`isolcpus` など）→ 割り込みの移動 → 周波数の固定 → 深い休止の制限
4. 「対策ごとに最大遅延が ○ us → ○ us」という表を作り、README と記事に載せる。
5. 周期プログラム＋邪魔役での trace のビフォー・アフターも、実機で撮り直す。

### (2) 知ってもらう

* Qiita / Zenn に記事を書く（例:「ROS 2 のリアルタイム性能を1コマンドで診断する」）。(1) の実機データを使う。
* README の先頭に、レポートのスクリーンショットを載せる。
* ROS Discourse など、ロボットのコミュニティで紹介する。

### (3) 機能の改善候補

* ターミナル表示（`check` / `trace` / `latency`）の日本語化（今はレポートと対話モードだけ日本語）
* 割り込み禁止区間などで**タイマー自体が遅れた分**の原因表示（今の `--trace` は「起こされてから動くまで」しか見えない。
  rtla timerlat が詳しい部分）
* 複数のプログラムを一度に `trace`
* ROS 2 のノード名・コールバック単位での表示

### (4) お金にする（有料版・サービス）

オープンコア方針なので、今の機能は無料のまま、次を有料にする案があります。

* 複数台の結果をまとめて比べる（出荷前の全台検査など）
* 継続して監視し、悪化したら知らせる
* レポートをもとにした、診断・設定代行サービス（「`sudo rtdoctor` を実行してレポートを送ってください」から始められる）

## 4. 開発環境のメモ

* Mac（Apple シリコン = arm64）の Docker でビルドとテストをしています。Docker のコマンドは
  `/Applications/Docker.app/Contents/Resources/bin` にあります。
* ビルドとテスト（ftrace の実地テストまで含める）:
  ```bash
  docker build --target base -t rtdoctor-base .
  docker run --rm --privileged -v "$PWD":/workspace rtdoctor-base bash -c \
    "mount -t tracefs nodev /sys/kernel/tracing 2>/dev/null; cmake -S . -B build -G Ninja -DRTDOCTOR_STATIC=ON && cmake --build build && ctest --test-dir build"
  ```
* `--pid` をコンテナの中で試すときは `docker run --pid=host` にすると、ホストと同じ番号で調べられます。
* x86-64 版を Mac で作るときは `docker build --platform linux/amd64 --target base -t rtdoctor-base-amd64 .`（エミュレーションなので遅い）。
* `gh` コマンドは Mac に入っていません。CI やリリースの状態は GitHub の公開 API（`curl https://api.github.com/repos/naosyke/rt-doctor/...`）で確認しています。
* リリース手順: `CMakeLists.txt` の `VERSION` を上げてコミット → CI の成功を確認 → `git tag -a v0.4.0 -m "..."` → `git push origin v0.4.0`。
  あとは GitHub Actions が作って公開します。

## 5. コードの地図

| ファイル | 役割 |
|---|---|
| `src/checks.cpp` | 診断項目（13個）と、影響の大きさ順の並べ替え |
| `src/system_view.cpp` | `/proc` と `/sys` の読み取り（テストでは偽のディレクトリを読む） |
| `src/ftrace.cpp` | 専用の ftrace インスタンスの作成・読み取り・後片付け |
| `src/trace_event.cpp` / `src/analyzer.cpp` | トレースの1行の解釈と、「待たされた時間を、そのとき動いていたものに振り分ける」計算 |
| `src/latency.cpp` | 起床遅延の測定（CPU ごとのスレッド、ヒストグラム） |
| `src/session.cpp` | trace とレポート作成の共通部分 |
| `src/wizard.cpp` | 対話モードと、調べる候補のプログラムの探し方 |
| `src/output.cpp` / `src/trace_report.cpp` / `src/latency_report.cpp` | ターミナルと JSON の表示 |
| `src/html_report.cpp` / `src/i18n.cpp` | HTML レポートと日本語訳 |
| `tests/` | 単体テスト、偽の `/proc`、実地テスト用のプログラム（周期プログラムと邪魔役） |

## 6. 決めたこと（理由つき）

* **ライセンスは Apache-2.0（オープンコア）**: まず使ってもらって評判を作り、有料機能や診断サービスにつなげるため。
* **GUI はデスクトップアプリにしない**: ロボットや組み込み機器は画面がなく、SSH で操作するのが普通だから。
  代わりに対話モードと HTML レポートで分かりやすくした。ブラウザで操作する画面（Web GUI）は、安全面の配慮が必要なので将来の有料機能の候補。
* **静的バイナリ1つで配る**: お客さんの環境に何も追加せずに動くことが、現場に持ち込む道具として大事だから。
* **ドキュメント（.md）はすべて日本語**。ターミナルの表示は英語のまま（海外の利用者も想定）。
* 勤務先の情報は使わない。公開されている Linux の仕組みと資料だけで作る。
