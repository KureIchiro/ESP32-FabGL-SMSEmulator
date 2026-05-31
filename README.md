# Sega Mark III Emulator for ESP32

🌐 **Language / 言語切り替え**
- [日本語 (Japanese)](#日本語)
- [English (英語)](#english)

---

<a id="日本語"></a>
# 日本語

ESP32と**FabGL**ライブラリを使用した、セガ・マークIII（Master System）のエミュレータです（現在開発中）。VGA出力およびPS/2キーボード、シリアル接続による外部コントローラーに対応しています。

## 📝 概要

本プロジェクトは、ESP32のリソース制約の中で「快適に動作するセガ・マークIII環境」を構築することを目的としています。
- **ターゲットフレームレート**: 約30FPS
- **サウンドに関する注意点**: 
  - ESP32のパフォーマンスの制約の為、再現度が低い場合があります。ご了承の上、ご使用ください。

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

MicroSDカードのルートディレクトリに、以下のフォルダおよびファイルを配置してください。
`SAVE` フォルダは、エミュレータ起動前にあらかじめ作成しておく必要があります。

```text
/ (SD Root)
├── /ROM                 : ゲームROMファイルなどを格納
│   └── /SAVE            : ステートセーブデータ保存用フォルダ
│       ├── ROMファイル名0.sav  : スロット0のセーブデータ
│       ├── ROMファイル名1.sav  : スロット1のセーブデータ
│       └── ROMファイル名2.sav  : スロット2のセーブデータ
```

---

## 🕹️ 操作ガイド

### 1. 基本操作

| 入力デバイス | 操作内容 | エミュレータ上の機能 |
| :--- | :--- | :--- |
| **キーボード** | **カーソルキー（上下左右）** | 方向キー（上下左右） |
| | **`[Z]`** | ボタン1 |
| | **`[X]`** | ボタン2 |
| **コントローラー** | **ボタン3** | 本体PAUSE（`[F12]` と同じ） |
| | **ボタン1, 2, 3 同時押し** | ハードウェアリセット（`[Shift] + [F12]` と同じ） |

---

### 2. 拡張機能（ファンクションキー操作）

#### 💡 システム操作
* **`[F12]`** : 一時停止 (Pause) / 再開（マークIII本体の **PAUSEボタン** を再現）
* **`[Shift] + [F12]`** : ハードウェアリセット (ESP32を再起動します)

#### 🎨 画面・サウンド調整
* **`[F7]` : フレームスキップタイミング調整機能**
  本ソフトはフレームスキップで30FPSを実現しているため、60フレームで交互に表示・消去を繰り返してキャラクターの点滅や半透明を表現するソフトでは、「表示しっぱなし」または「全く表示されない」という現象が起こりえます。
  その場合にフレームスキップのタイミングを変化させることによって、点滅表示が見えるようにする機能です。
  `0, 1, 2, 4, 8, 16` フレームごとにタイミングを変化させることが可能です。キャラクターの動作やスクロールががたついて見える可能性もあるため、調整してご使用ください。
* **`[F8]` : 擬似SCC音源 有効/無効**
  PSGにしか対応していないソフトでも、プリセットの音色でSCC音源を鳴らすことができる機能です。
  *※FM音源に対応したソフトではOFFにしてご使用ください。*
* **`[Shift] + [F8]` : スプライト制限解除**
  スプライトの横8個制限をなくします。

#### 💾 SRAMセーブ＆ロード
SRAM（バックアップ機能）に対応したタイトルは、その内容をSDカードへ保存・復元できます。3つのスロットに対応しています。
*※ソフト起動時に自動的にロードされるわけではありません。*

* **状態のロード**: `[F9]` / `[F10]` / `[F11]`（それぞれスロット2〜0に対応）
* **状態のセーブ**: `[Shift] + [F9]` / `[Shift] + [F10]` / `[Shift] + [F11]`（それぞれスロット2〜0に対応）

---

## 🔌 ジョイスティック（外部コントローラー）の接続

本プロジェクトは、ESP32のシリアルポート（UART）経由で入力された「アタリ仕様（D-Sub 9ピン）コントローラー」の信号をサポートしています。

> 💡 **ジョイスティックコンバータの互換性について**
> 
> 外部コントローラーの接続に使用する **アタリ仕様→シリアル変換器のハードウェアおよびファームウェア** は、MSX1エミュレータ部分で公開しているものがそのまま流用可能です。

### 1. 接続・通信仕様
- **通信規格**: RS-232C シリアル通信
- **ボーレート**: 115200 bps
- **接続ポート**: `IO39`
- **更新周期**: 33.34ms (約29.97Hz)
- **対応ハード**: マークIII ジョイスティックポート1・2（1P / 2P）

### 2. 外部変換器からのデータフォーマット
シリアル経由で以下の **2バイトバイナリデータ** を受信し、マークIIIのジョイスティック状態にリアルタイムに反映します。データは「1P（ポートA）データ → 2P（ポートB）データ」の順で連続して送信されることを想定しています。

| Bit | 機能 | 内容 |
| :---: | :--- | :--- |
| **b7** | ポート判別ID | `0` = Port A (1P) ／ `1` = Port B (2P) |
| **b6** | ボタン3 | `0`: ON ／ `1`: OFF（押下時に本体PAUSE） |
| **b5** | ボタン2 | `0`: ON ／ `1`: OFF |
| **b4** | ボタン1 | `0`: ON ／ `1`: OFF |
| **b3** | 右 (Right) | `0`: ON ／ `1`: OFF |
| **b2** | 左 (Left) | `0`: ON ／ `1`: OFF |
| **b1** | 下 (Down) | `0`: ON ／ `1`: OFF |
| **b0** | 上 (Up) | `0`: ON ／ `1`: OFF |

---

## 免責事項

本ソフトウェアの使用によって生じたいかなる損害（直接的・間接的を問わず）についても、Fabrizio Di Vittorio氏、Marat Fayzullin氏、David Latham氏、Mitsutaka Okazaki氏、および開発者本人は一切の責任を負いません。すべて利用者の自己責任において使用してください。

---
---

<a id="english"></a>
# English

This is a Sega Mark III (Master System) emulator using ESP32 and the **FabGL** library (currently under development). It supports VGA output, PS/2 keyboard, and external controllers via serial connection.

## 📝 Overview

The objective of this project is to build a "comfortable Sega Mark III environment" within the resource constraints of the ESP32.
- **Target Frame Rate**: Approx. 30 FPS
- **Notes on Sound**: 
  - Due to the performance constraints of the ESP32, the audio reproduction quality may be low. Please understand this before use.

> ⚠️ **Notice Regarding Development and Licensing**
>
> Since the majority of the main source code is built and modified based on AI-generated code, the developer does not fully grasp all the internal logic. Therefore, please note that we cannot answer detailed questions regarding the source code specifications or internal logic.
> Please refer to the header comments at the beginning of the source code for specific license terms and conditions, and agree to them before use.

---

## 🤝 Credits and Acknowledgments

We would like to express our deepest gratitude to the following individuals for opening their wonderful development foundations to the open-source community.

- **Graphics/Audio Engine**: Uses [FabGL](https://github.com) by Fabrizio Di Vittorio.
- **Z80 CPU Core**: Modified based on the code by Marat Fayzullin.
- **VDP Section**: Modified based on the TMS9918A core by David Latham, with adjustments by Marat Fayzullin.
- **OPLL (YM2413) Sound Core**: Modified based on the code by Mitsutaka Okazaki.

### 📜 About the Licenses of Each Core
The licenses for each core within this software comply with the respective policies of FabGL, Marat Fayzullin (**fMSX License**), and Mitsutaka Okazaki (**MIT License / Free License**).

---

## 🛠️ Verified Environment & Build Settings

### Development Environment
- **Board Manager**: ESP32 by Espressif Systems (**v2.0.5 Recommended**)
- **Library Used**: FabGL v1.0.9

### Arduino IDE Recommended Settings
- **Development Board**: `ESP32 Dev Module`
- **PSRAM**: `"Enabled"` (**Required**)
- **Partition Scheme**: `"Default 4MB with SPIFFS"` or equivalent

---

## 📂 SD Card Structure and Setup

Please place the following folders and files in the root directory of your MicroSD card.
The `SAVE` folder must be created manually before launching the emulator.

```text
/ (SD Root)
├── /ROM                 : Stores game ROM files, etc.
│   └── /SAVE            : Folder for storing state save data
│       ├── ROM_filename0.sav  : Save data for Slot 0
│       ├── ROM_filename1.sav  : Save data for Slot 1
│       └── ROM_filename2.sav  : Save data for Slot 2
```

---

## 🕹️ Operation Guide

### 1. Basic Controls

| Input Device | Action | Emulator Function |
| :--- | :--- | :--- |
| **Keyboard** | **Cursor Keys (Up/Down/Left/Right)** | D-Pad (Up/Down/Left/Right) |
| | **`[Z]`** | Button 1 |
| | **`[X]`** | Button 2 |
| **Controller** | **Button 3** | Console PAUSE (Same as `[F12]`) |
| | **Simultaneous press of Buttons 1, 2, and 3** | Hardware Reset (Same as `[Shift] + [F12]`) |

---

### 2. Extended Functions (Function Key Operations)

#### 💡 System Operations
* **`[F12]`** : Pause / Resume (Reproduces the **PAUSE button** on the Mark III console)
* **`[Shift] + [F12]`** : Hardware Reset (Reboots the ESP32)

#### 🎨 Screen & Sound Adjustments
* **`[F7]` : Frame Skip Timing Adjustment**
  This emulator achieves 30 FPS by using frame skipping. Therefore, games that alternate flashing or semi-transparency every 60 frames may result in characters either remaining solid or disappearing entirely.
  This function changes the frame skip timing so that the flashing effect becomes visible.
  The timing can be altered every `0, 1, 2, 4, 8, 16` frames. Please adjust as needed, though character movement or background scrolling may appear jittery.
* **`[F8]` : Pseudo-SCC Sound Enable/Disable**
  This allows titles that only support PSG to play sound using pre-configured SCC wavetable synthesizer tones.
  *\*Please turn this OFF for software that natively supports FM sound.*
* **`[Shift] + [F8]` : Remove Sprite Limit**
  Removes the original hardware limitation of 8 sprites per horizontal line.

#### 💾 SRAM Save & Load
Titles that feature SRAM (backup memory) can save and restore their data to/from the SD card. Three slots are supported.
*\*Data is not automatically loaded when the game starts.*

* **Load State**: `[F9]` / `[F10]` / `[F11]` (Corresponds to Slots 2 to 0, respectively)
* **Save State**: `[Shift] + [F9]` / `[Shift] + [F10]` / `[Shift] + [F11]` (Corresponds to Slots 2 to 0, respectively)

---

## 🔌 Connecting a Joystick (External Controller)

This project supports Atari-specification (D-Sub 9-pin) controller signals input via the ESP32's serial port (UART).

> 💡 **Joystick Converter Compatibility**
> 
> The **Atari-to-Serial converter hardware and firmware** used for connecting external controllers can be shared directly from the one published in the MSX1 emulator section.

### 1. Connection & Communication Specifications
- **Communication Standard**: RS-232C Serial Communication
- **Baud Rate**: 115200 bps
- **Connection Port**: `IO39`
- **Update Interval**: 33.34ms (Approx. 29.97Hz)
- **Supported Hardware**: Mark III Joystick Ports 1 & 2 (1P / 2P)

### 2. Data Format from External Converter
The emulator receives the following **2-byte binary data** via serial communication and reflects the status of the Mark III joystick in real-time. It assumes data is transmitted continuously in the order of "1P (Port A) data → 2P (Port B) data".

| Bit | Function | Description |
| :---: | :--- | :--- |
| **b7** | Port ID | `0` = Port A (1P) / `1` = Port B (2P) |
| **b6** | Button 3 | `0`: ON / `1`: OFF (Triggers Console PAUSE when pressed) |
| **b5** | Button 2 | `0`: ON / `1`: OFF |
| **b4** | Button 1 | `0`: ON / `1`: OFF |
| **b3** | Right | `0`: ON / `1`: OFF |
| **b2** | Left | `0`: ON / `1`: OFF |
| **b1** | Down | `0`: ON / `1`: OFF |
| **b0** | Up | `0`: ON / `1`: OFF |

---

## Disclaimer

Fabrizio Di Vittorio, Marat Fayzullin, David Latham, Mitsutaka Okazaki, and the developer of this software assume no responsibility or liability for any damages (direct or indirect) arising from the use of this software. Please use it entirely at your own risk.