# rt-doctor

**リアルタイム Linux の「ときどき期限に間に合わない」原因を見つけるツール**

ロボット（ROS 2）、モーター制御、ドローン、産業機器、オーディオなど、時間に厳しい処理を Linux で動かしていると、
「普段は間に合うのに、ときどき遅れる。原因がわからない」という問題がよく起きます。
rt-doctor は、その原因を3つの方向から調べます。

| | 調べること | たとえ |
|---|---|---|
| **システムの診断**（`check`） | カーネル、CPU の隔離、割り込み、省電力、メモリ、権限、仮想マシンなど、遅れの原因になる**設定** | 健康診断 |
| **起床遅延の測定**（`latency`） | リアルタイムのスレッドが、約束の時刻から**実際に何 us 遅れて起きるか**を CPU ごとに測る（cyclictest と同じ方式） | 体力測定 |
| **遅れの原因調査**（`trace`） | 指定したプログラムが待たされていた間に、その CPU で**実際に何が動いていたか**（他のプログラム、割り込みなど） | 心電図 |

結果は、**なぜ問題なのか**と**どう直すか**と一緒に示します。HTML のレポート（日本語対応）にまとめて、人に渡すこともできます。

依存ライブラリのない、ファイル1つのプログラムです。ロボットや組み込みボードにコピーするだけで動きます。

## すぐに使う

```bash
curl -fsSL https://github.com/naosyke/rt-doctor/releases/latest/download/install.sh | sh
sudo rtdoctor
```

`sudo rtdoctor` とだけ打つと、質問に答えていくだけで最後まで進む**対話モード**になります。

```text
[1/4] このシステムを診断しています…
  一番の要因: カーネルのプリエンプション方式 - プリエンプション: なし、または voluntary
  次に:       隔離CPU - なし

[2/4] CPU ごとの起床遅延を測ります（リアルタイムのスレッドが、約束の時刻からどれだけ遅れて起きるか）。
  何秒間測りますか？ [10]（0 で飛ばす）:
  最悪: 412 us (CPU 2) - まずまず: 10 ms 周期なら使えますが、1 ms 周期では期限切れが起きえます

[3/4] 遅れを調べたいプログラムを選びます。動いているプログラムを1秒間観察しています…
   1) ros2_node        pid 1826    リアルタイム FIFO 80、1秒に 1000 回起きる（約 1.0 ms ごと）、スレッド control_loop
   2) planner          pid 1902    1秒に 100 回起きる（約 10.0 ms ごと）
  番号・pid・名前のどれかを入力（Enter で飛ばす）: 1
  期限は何 ms ですか？ふつうは周期と同じです [1.0]:
  何秒間見張りますか？ [10]:

  スレッド control_loop: 10000 回実行、期限切れ 41 回、最悪 3.20 ms
  一番の原因: タスク ros2_logger (pid 2210)、38.20 ms
  対策: … そのタスクを別の CPU に移す（taskset、cgroup の cpuset）か、遅れたタスクにリアルタイム優先度を付けて（chrt -f）…

[4/4] レポートを作っています…
  /home/robot/rtdoctor-report-20261012-101500.html
```

手順の詳しい説明、結果の読み方、よくあるつまずきは **[使い方ガイド](docs/guide.md)** にまとめています。

## コマンド一覧

| やりたいこと | コマンド |
|---|---|
| 質問に答えながら全部やる | `sudo rtdoctor` |
| システムの診断 | `sudo rtdoctor check` |
| CPU ごとの起床遅延の測定 | `sudo rtdoctor latency --duration 60` |
| 遅延の原因も一緒に調べる | `sudo rtdoctor latency --duration 60 --trace` |
| プログラムを遅らせた原因の調査 | `sudo rtdoctor trace --pid PID --deadline-ms 期限` |
| 名前で指定して調査 | `sudo rtdoctor trace --comm スレッド名 ...` |
| HTML レポート（日本語） | `sudo rtdoctor report --lang ja -o report.html [--latency 秒] [--pid PID ...]` |
| ほかのツールで処理する | `check` / `trace` に `--json` |
| 自動テスト（CI）で設定の劣化を検知 | `rtdoctor check --fail-on warn` |
| 自動テストで遅延の悪化を検知 | `sudo rtdoctor latency --duration 60 --fail-over-us 200` |

終了コード: 0 = 問題なし（`--fail-on warn` なら警告もなし）、1 = 基準以上の問題あり、2 = 使い方の誤り、
3 = トレースが使えない（`trace` のみ。`report` ではレポートの中に書きます）。

ターミナルの表示は英語、対話モードとレポートは日本語に対応しています（対話モードは環境の言語設定 `LANG` に従い、
`--lang ja` / `--lang en` で切り替えられます）。

## システムの診断で見る項目

| 項目 | 見るもの | 影響の大きさ |
|---|---|---|
| 仮想マシン | Docker Desktop、WSL 2、KVM/QEMU、VMware、Hyper-V など。仮想マシンの一時停止は中からは防げない | 致命的 |
| カーネルのプリエンプション方式 | `PREEMPT_RT`、full / voluntary / none | 大 |
| リアルタイム優先度の使用権限 | `RLIMIT_RTPRIO`、root | 大 |
| 隔離CPU | `isolcpus`、`nohz_full`、`rcu_nocbs` | 中 |
| 隔離CPUへの割り込み | すべての割り込みの割り当て先 | 中 |
| CPU周波数の制御 | CPU ごとの cpufreq governor | 中 |
| CPUの深い休止状態 | 有効な一番深い休止状態からの復帰時間 | 中 |
| メモリロックの権限 | `RLIMIT_MEMLOCK`、root | 中 |
| スワップ | 設定だけでなく**実際の使用量**も見る（使用中なら警告、未使用なら参考情報） | 中／小 |
| Transparent Huge Pages | THP のモード | 小 |
| SMT（ハイパースレッディング） | 1つのコアを2つのスレッドで共有しているか | 小 |
| リアルタイム処理の上限 | `sched_rt_runtime_us` | 小 |
| クロックソース | TSC／アーキテクチャタイマーか、遅い時計か | 小 |

判定は4段階です。

| 判定 | 意味 |
|---|---|
| PASS | リアルタイム向けの良い設定。何もしなくてよい |
| INFO | それだけでは問題ではない参考情報（安全装置、条件次第のもの、対象外、確認できないもの） |
| WARN | 遅れの原因になりうる。直した方がよい |
| FAIL | このままではリアルタイム処理がまともに動かない |

結果の先頭には、**影響が一番大きい問題**を出します（「まずこれを直す」）。root でないと見えない値もあるので、`sudo` で実行してください。

## 起床遅延の測定

```bash
sudo rtdoctor latency --duration 60
```

各 CPU に1本ずつ、リアルタイム優先度（SCHED_FIFO 95）のスレッドを置き、「1 ms 後に起こして」とタイマーをセットしては、
実際に起きた時刻とのずれを記録します。cyclictest と同じ方式で、同じ条件で比べると同じ範囲の値になることを確かめています。

```text
rt-doctor latency - 15.0 s, timer every 1000 us, SCHED_FIFO 95 on CPUs 0-3

   CPU   wake-ups       min       avg       99%     99.9%        max
     0      14952     11 us    117 us    277 us   1500 us    6157 us
     1      14952    7.4 us    130 us    283 us   1260 us    5779 us
     2      14955    8.7 us    113 us    283 us   1590 us    5761 us
     3      14951     12 us    146 us    310 us   1560 us    6073 us

  Worst: 6157 us on CPU 0 -> poor: wake-ups can be milliseconds late, so even 10 ms loops can miss; see rtdoctor check
```

（Mac の Docker Desktop、つまり仮想マシンの上で測った実際の値です。同じ条件の cyclictest は、最小 6〜10 us、最大 6.1〜7.0 ms
でした。実機で調整済みのマシンなら、最大値は数十 us 程度になるのが一般的です。）

**最大値（max）**が、そのマシンで起こりうる最悪の遅れの目安です。

| 最大値 | 目安 |
|---|---|
| 50 us 以下 | とても良い（調整済みの PREEMPT_RT の水準）。100 us 程度の周期まで使える |
| 150 us 以下 | 良い。1 ms 周期の制御ループに使える |
| 1 ms 以下 | まずまず。10 ms 周期なら使えるが、1 ms 周期では期限切れが起きうる |
| 1 ms 超 | 悪い。10 ms 周期でも期限切れが起きうる |

`--trace` を付けると、測定スレッドが起こされてから実際に動くまでの間に、**その CPU で何が動いていたか**も表示します
（cyclictest にはない機能です）。

| オプション | 意味（初期値） |
|---|---|
| `--duration 秒` | 測る時間（10）。まれな遅れを捕まえるには、いつもの負荷をかけて数分以上 |
| `--interval-us us` | タイマーの間隔（1000） |
| `--priority 1-99` | 測定スレッドのリアルタイム優先度（95） |
| `--cpus 0-3` | 測る CPU（すべて） |
| `--trace` | 遅れの原因も調べる |
| `--fail-over-us us` | 最大値がこれを超えたら終了コード 1（CI 用） |
| `--json` | ほかのツールで処理する形式 |

## 遅れの原因調査の例

同じ CPU で計算し続けるプログラム（hog）に邪魔されている、10 ms 周期のプログラムを調べた例です。

```bash
sudo rtdoctor trace --pid 1826 --duration 5 --deadline-ms 4
```

```text
Thread periodic (tid 1826): 500 activations, 3 over the deadline
  Response time (wake-up to sleep): median 2.92 ms, 99th 3.91 ms, max 5.92 ms
  Waiting for its CPU: 504 times, 464.06 ms in total, max 3.00 ms (4 preemptions)

  Who delayed it (ran on its CPU while it was ready)
     460.23 ms  task hog (pid 1825)                             502 times
       2.16 ms  irq 11 arch_timer                               503 times
    -> Another task ran on the task's CPU while it was ready. ... give the delayed task a
       real-time priority (chrt -f) so it preempts the other one.
```

アドバイスどおり `chrt -f 50`（リアルタイム優先度）を付けて、同じ条件で測り直した結果です。

| | 対策前 | `chrt -f 50` 後 |
|---|---|---|
| 期限（4 ms）切れ | 500回中 3回 | 500回中 0回 |
| 最悪の応答時間 | 5.92 ms | 2.07 ms |
| CPU の空き待ちの合計 | 464 ms | 4.2 ms |
| そのうち hog が原因 | 460 ms | 3.3 ms |

仕組み: ほかのトレースツールの邪魔をしない専用の ftrace インスタンスで、スケジューラ（`sched_switch`、`sched_waking`、
`sched_migrate_task`）と割り込み（irq / softirq）の記録を取ります。対象が「動ける状態なのに待たされていた時間」を、
その間にその CPU で動いていたものごとに振り分けています。

## レポート

```bash
sudo rtdoctor report --lang ja -o report.html --pid 1826 --duration 10 --deadline-ms 4
```

`report.html` は、それだけで完結する1枚のページです（スクリプトなし、ネット接続不要、ライトモードとダークモードに対応、
印刷可）。チケットに添付したり、お客さんや同僚に送ったりできます。

* マシンの情報と、**一番の要因**
* CPU ごとの起床遅延（`--latency 秒` を付けたとき）
* 調査したプログラムの応答時間の分布（期限の線つき）、**誰が遅らせたか**と対策、時間がかかった回ワーストの内訳
* すべての診断項目（問題のあるものから、影響の大きい順）

## インストール

x86-64 用と arm64 用の静的バイナリがあり、どの Linux ディストリビューションでも動きます
（Ubuntu、Debian 11、Fedora、Amazon Linux 2、Alpine、BusyBox で確認済み）。

```bash
curl -fsSL https://github.com/naosyke/rt-doctor/releases/latest/download/install.sh | sh
```

スクリプトは、マシンに合う版を選び、公開されている SHA256SUMS で改ざんがないか確認してから `/usr/local/bin` に入れます
（`PREFIX=...` で場所を変更可）。[リリースページ](https://github.com/naosyke/rt-doctor/releases)から
`rtdoctor-linux-x86_64.tar.gz` / `rtdoctor-linux-arm64.tar.gz` を直接ダウンロードしても使えます。

アンインストールは `sudo rm /usr/local/bin/rtdoctor` です。

## ビルド

```bash
docker build --target base -t rtdoctor-base .
docker run --rm -v "$PWD":/workspace rtdoctor-base bash -c \
  "cmake -S . -B build -G Ninja -DRTDOCTOR_STATIC=ON && cmake --build build && ctest --test-dir build"
```

`build/rtdoctor`（約 2 MB の静的バイナリ）ができます。診断項目は、典型的なマシン（調整済みのリアルタイム機、普通の
デスクトップ、Docker Desktop、隔離CPUに割り込みがあるマシン）を模した偽の `/proc`・`/sys` でテストしています。
トレース、遅延測定、対話モードの実地テスト（周期プログラムと邪魔役を同じ CPU で動かし、邪魔役を名指しできるか）は、ftrace が
使える環境（root。コンテナなら `--privileged`）で動き、それ以外では飛ばします。

### リリース

CI は push のたびに x86-64 と arm64 でビルドとテストをします（root で実行するので、実地テストも含みます）。
公開するときは、`CMakeLists.txt` のバージョンを上げてから、同じ番号のタグを push します。

```bash
git tag v0.4.0
git push origin v0.4.0
```

リリース用のワークフローが `scripts/package.sh` で両方のバイナリを作り（タグとバージョンが違えば止まります）、
`SHA256SUMS` と `install.sh` と一緒に公開します。

## ロードマップ

* [x] `rtdoctor check` - システムの設定の診断
* [x] `rtdoctor trace` - プログラムが遅れたとき、**その CPU で何が動いていたか**
* [x] `rtdoctor report` - すべてを1つの HTML にまとめて共有（日本語対応）
* [x] x86-64 / arm64 の配布用バイナリ（タグで GitHub Actions が作成）
* [x] 対話モード（`sudo rtdoctor`）
* [x] `rtdoctor latency` - CPU ごとの起床遅延の測定を内蔵（cyclictest 不要、`--trace` で原因も表示）

開発の記録と、次にやることは [docs/status.md](docs/status.md) にまとめています。

## ライセンス

[Apache License 2.0](LICENSE) のオープンソースです。商用も含めて、自由に使用・改変・配布できます。Copyright 2026 naosyke.

公開されている Linux のインターフェースと資料（procfs、sysfs、ftrace、カーネルのドキュメント）だけをもとに作った、
個人のプロジェクトです。どの勤務先とも関係ありません。
