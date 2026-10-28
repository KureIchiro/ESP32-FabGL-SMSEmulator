# SEGA MarkIII / Master System Emulator for ESP32

[ [English](#english) | [日本語](#日本語) ]

---

<a id="english"></a>
# English

An emulator for the SEGA MarkIII / Master System using ESP32 and the **FabGL** library (Work in Progress). It supports VGA output, PS/2 keyboards, and serial connection for external controllers.

## 📝 Overview

The goal of this project is to build a "comfortable SEGA MarkIII environment" within the resource constraints of the ESP32.
- **Target Frame Rate**: Approx. 60 FPS
- **Audio Limitations**: 
  - The reproducibility of the FM sound source (especially the rhythm section) is low due to FabGL's sampling rate constraints.
  - Titles using PSG sampling playback also suffer from lower audio fidelity for the same reason.

> ⚠️ **Notice Regarding Development & Licenses**
>
> Most of the main source code is built and modified based on AI-generated code. Therefore, the developer does not completely grasp all internal logic and cannot answer detailed questions regarding source code specifications. Thank you for your understanding.
> Please refer to the header comments at the beginning of the source code for license details and agree to them before use.

---

## 🤝 Credits & Acknowledgments

Deep gratitude to the authors who published these excellent foundations as open-source.

- **Graphics/Audio Engine**: Uses [FabGL](https://github.com) by Fabrizio Di Vittorio.
- **Z80 CPU Core**: Modified based on the work by Marat Fayzullin.
- **VDP Section**: Modified based on the TMS9918A core originally by David Latham and maintained by Marat Fayzullin.
- **OPLL (YM2413) Sound Core**: Modified based on the work by Mitsutaka Okazaki.

### 📜 Core Licenses
Each core within this software complies with the policies of FabGL, Marat Fayzullin (**fMSX License**), and Mitsutaka Okazaki (**MIT License / Free License**).

---

## 🛠️ Environment & Build Settings

### Development Environment
- **Board Manager**: ESP32 by Espressif Systems (**v2.0.5 Recommended**)
- **Library**: FabGL v1.0.9

### Recommended Arduino IDE Settings
- **Board**: `ESP32 Dev Module`
- **PSRAM**: `"Enabled"` (**Required**)
- **Partition Scheme**: `"Default 4MB with SPIFFS"` or similar

---

## 📂 SD Card Structure & Setup

Create a `/SMS` folder in the root of your MicroSD card and put the game ROMs in `/SMS/ROM` (MSX1 uses `/MSX1`, MSX2 `/MSX2`, PC-8801 `/PC88` and X1 `/X1`, so one card can hold every machine).
The `SAVE` folder is created automatically when the first save is written.

```text
/ (SD Root)
└── /SMS
    └── /ROM              : Storage for game ROM files (.sms / .sg, sub-folders allowed)
        └── /SAVE         : Storage for SRAM and state save data (created automatically)
```

---

## 🕹️ Operations Guide (Function Keys)

Please modify the source code to fit your keyboard layout if localization (e.g., key mapping) is required.

### 1. System Operations
- **`[F12]`** : Pause / Resume (Reproduces the **PAUSE button** on the MarkIII console)
- **`[Shift] + [F12]`** : Hardware Reset (Reboots the ESP32)

### 2. SRAM Save & Load
Titles supporting SRAM (battery backup) keep it on the SD card (`/SMS/ROM/SAVE/<rom name>_0.sav` etc.).

- **Automatic (slot 0, works like the battery)**: slot 0 is loaded automatically when the game starts, and saved automatically 2 seconds after the game writes to SRAM. Just save inside the game as on the real machine.
- **Manual copies (slots 1 / 2)**: `[Shift] + [F10]` / `[Shift] + [F9]` save SRAM to slot 1 / 2; `[F10]` / `[F9]` load it.
- `[F11]` / `[Shift] + [F11]` load / save slot 0 by hand.
- Loading while a game is running replaces the cartridge RAM immediately. Some games (such as Ys) also use this RAM as work memory, so it is safest to load from the title screen and then use the game's own load menu.

---

## 🔌 Joystick (External Controller) Connection

This project supports Atari-specification (D-Sub 9-pin) controller signals input via the ESP32 serial port (UART).

> 💡 **Joystick Converter Compatibility**
> 
> You can reuse the **Atari-to-Serial Joystick Converter hardware/firmware** already published in the MSX1 emulator section of this repository.

### 1. Connection & Communication Specs
- **Protocol**: RS-232C Serial Communication
- **Baud Rate**: 115200 bps
- **Connection Port**: `IO39`
- **Update Interval**: 16.67ms (Approx. 59.94Hz)
- **Supported Hardware**: MarkIII Joystick Ports 1 & 2 (1P / 2P)

### 2. Data Format from External Converter
Receives the following **2-byte binary data** via serial and reflects it onto the MarkIII joystick state in real-time. Data is expected to be transmitted continuously in the order of "1P (Port A) data → 2P (Port B) data".


| Bit | Function | Description |
| :---: | :--- | :--- |
| **b7** | Port ID | `0` = Port A (1P) / `1` = Port B (2P) |
| **b6** | Button 3 | `0`: ON / `1`: OFF (**Active Low**) |
| **b5** | Button 2 | `0`: ON / `1`: OFF |
| **b4** | Button 1 | `0`: ON / `1`: OFF |
| **b3** | Right | `0`: ON / `1`: OFF |
| **b2** | Left | `0`: ON / `1`: OFF |
| **b1** | Down | `0`: ON / `1`: OFF |
| **b0** | Up | `0`: ON / `1`: OFF |

---

## Disclaimer

IN NO EVENT SHALL FABRIZIO DI VITTORIO, MARAT FAYZULLIN, DAVID LATHAM, MITSUTAKA OKAZAKI, OR THE DEVELOPER BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, OR CONSEQUENTIAL DAMAGES ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE. USE AT YOUR OWN RISK.

---

<a id="日本語"></a>
# 日本語

ESP32と**FabGL**ライブラリを使用した、セガ・マークIII（Master System）のエミュレータです（現在開発中）。VGA出力およびPS/2キーボード、シリアル接続による外部コントローラーに対応しています。

## 📝 概要

本プロジェクトは、ESP32のリソース制約の中で「快適に動作するセガ・マークIII環境」を構築することを目的としています。
- **ターゲットフレームレート**: 約60FPS
- **サウンドに関する注意点**: 
  - FM音源（特にリズムセクション）は、FabGLのサンプリングレートの制約により再現度が低くなっています。
  - PSGでサンプリング再生を行っているタイトルについても、同様の理由で再現度が低くなります。

> ⚠️ **開発・ライセンスに関するご注意**
>
> メインソースコードの大部分はAI生成コードをベースに構築・改変しているため、開発者もすべてのロジックを完全に把握しているわけではありません。そのため、ソースコードの詳細な仕様や内部ロジックに関する質問にはお答えできかねます。あらかじめご了承ください。
> ライセンスの詳細な条件はソースコード冒頭のヘッダコメントを参照し、同意の上でご利用ください。

---

## 🤝 クレジットと謝辞

素晴らしい開発基盤をオープンソースとして公開されている諸氏に、深く感謝の意を表します。

- **グラフィック/オーディオエンジン**: Fabrizio Di Vittorio氏による [FabGL](https://github.com) を使用。
- **Z80 CPUコア**: Marat Fayzullin氏のコードをベースに改変。
- **VDPセクション**: David Latham氏のTMS9918Aコアをベースに、Marat Fayzullin氏が調整した実装を改変。
- **OPLL (YM2413) 音源コア**: Mitsutaka Okazaki氏によるコードをベースに改変。

### 📜 各コアのライセンスについて
本ソフトウェア内の各コアのライセンスは、FabGL、Marat Fayzullin氏 (**fMSX License**), および Mitsutaka Okazaki氏 (**MITライセンス / フリーライセンス**) の各ポリシーに従います。

---

## 🛠️ 動作確認済み環境・ビルド設定

### 開発環境
- **ボードマネージャ**: ESP32 by Espressif Systems (**v2.0.5 推奨**)
- **使用ライブラリ**: FabGL v1.0.9

### Arduino IDE 推奨設定
- **開発ボード**: `ESP32 Dev Module`
- **PSRAM**: `"Enabled"` (**必須**)
- **Partition Scheme**: `"Default 4MB with SPIFFS"` またはそれに準ずるもの

---

## 📂 SDカードの構成とセットアップ

MicroSDカードのルートに `/SMS` フォルダを作り、ゲームROMを `/SMS/ROM` に入れてください（MSX1 は `/MSX1`、MSX2 は `/MSX2`、PC-8801 は `/PC88`、X1 は `/X1` なので、1枚のカードに全機種を入れられます）。
`SAVE` フォルダは、最初にセーブしたときに自動で作られます。

```text
/ (SD Root)
└── /SMS
    └── /ROM              : ゲームROMファイル（.sms / .sg。中にフォルダを作ってもよい）
        └── /SAVE         : SRAM とステートセーブの保存用フォルダ（自動で作られます）
```

---

## 🕹️ 拡張機能操作ガイド（ファンクションキー）

キーボード配列等のローカライズに関しては、各自のキーボード環境に合わせて必要に応じてソースコードを修正してください。

### 1. システム操作
- **`[F12]`** : 一時停止 (Pause) / 再開（マークIII本体の **PAUSEボタン** を再現）
- **`[Shift] + [F12]`** : ハードウェアリセット (ESP32を再起動します)

### 2. SRAMセーブ＆ロード
SRAM（電池バックアップ）に対応したタイトルは、その内容をSDカード（`/SMS/ROM/SAVE/<ROM名>_0.sav` など）に保存します。

- **自動（スロット0。電池の代わり）**: ゲームを始めるときにスロット0を自動で読み込み、ゲームがSRAMを書き換えたら2秒後に自動で保存します。実機と同じく、ゲームの中でセーブするだけで残ります。
- **手動の控え（スロット1・2）**: `[Shift] + [F10]` / `[Shift] + [F9]` でスロット1 / 2 に保存、`[F10]` / `[F9]` で読み込みます。
- `[F11]` / `[Shift] + [F11]` はスロット0を手動で読み込み / 保存します。
- 遊んでいる途中で読み込むと、カートリッジのRAMがすぐに入れ替わります。イースのようにこのRAMを作業用にも使うゲームがあるので、タイトル画面で読み込んでからゲームのロードを使うのが安全です。

---

## 🔌 ジョイスティック（外部コントローラー）の接続

本プロジェクトは、ESP32のシリアルポート（UART）経由で入力された「アタリ仕様（D-Sub 9ピン）コントローラー」の信号をサポートしています。

> 💡 **ジョイスティックコンバータの互換性について**
> 
> 外部コントローラーの接続に使用する **アタリ仕様→シリアル変換器のハードウェアおよびファームウェア** は、本リポジトリ内のMSX1エミュレータ部分で公開しているものがそのまま流用可能です。

### 1. 接続・通信仕様
- **通信規格**: RS-232C シリアル通信
- **ボーレート**: 115200 bps
- **接続ポート**: `IO39`
- **更新周期**: 16.67ms (約59.94Hz)
- **対応ハード**: マークIII ジョイスティックポート1・2（1P / 2P）

### 2. 外部変換器からのデータフォーマット
シリアル経由で以下の **2バイトバイナリデータ** を受信し、マークIIIのジョイスティック状態にリアルタイムに反映します。データは「1P（ポートA）データ → 2P（ポートB）データ」の順で連続して送信されることを想定しています。


| Bit | 機能 | 内容 |
| :---: | :--- | :--- |
| **b7** | ポート判別ID | `0` = Port A (1P) ／ `1` = Port B (2P) |
| **b6** | ボタン3 | `0`: ON ／ `1`: OFF (**Active Low**) |
| **b5** | ボタン2 | `0`: ON ／ `1`: OFF |
| **b4** | ボタン1 | `0`: ON ／ `1`: OFF |
| **b3** | 右 (Right) | `0`: ON ／ `1`: OFF |
| **b2** | 左 (Left) | `0`: ON ／ `1`: OFF |
| **b1** | 下 (Down) | `0`: ON ／ `1`: OFF |
| **b0** | 上 (Up) | `0`: ON ／ `1`: OFF |

---

## 免責事項

本ソフトウェアの使用によって生じたいかなる損害（直接的・間接的を問わず）についても、Fabrizio Di Vittorio氏、Marat Fayzullin氏、David Latham氏、Mitsutaka Okazaki氏、および開発者本人は一切の責任を負いません。すべて利用者の自己責任において使用してください。
