# Mid2Wav

MIDIファイルをSoundFontベースで高品質WAVに変換するC++コマンドラインツール。
GM/GS準拠のMIDI演奏再現を目指す。

## ビルド

```bash
cd cpp
make linux        # Linux
make              # macOS
```

## 使い方

```bash
Mid2Wav -i <input> -o <output> [options]
```

### 基本例

```bash
# 自動でSoundFontを選択して変換
Mid2Wav -i input.mid -o output/

# 特定のSoundFontを指定
Mid2Wav -i input.mid -o output/ --sf2 /path/to/TyrolandGSV30fix.sf2

# ディレクトリ内の全MIDIを一括変換
Mid2Wav -i midi_folder/ -o wav_folder/
```

## オプション

| オプション | 説明 | デフォルト |
|-----------|------|-----------|
| `-i, --input <path>` | 入力MIDIファイルまたはディレクトリ | 必須 |
| `-o, --output <path>` | 出力WAVディレクトリ | 必須 |
| `--sf2 <path\|auto>` | SoundFontファイル指定。`auto`でsoundfonts/から自動選択 | auto |
| `--device <model>` | デバイスモデルの指定・表示（合成には非連動） | auto（SysExから自動検出） |
| `--pitch <semitones>` | 全ノートの音程シフト（半音単位）。+12で+1オクターブ、-12で-1オクターブ | 0 |
| `--channels` | 各MIDIチャンネルを個別WAVファイルとして出力 | 通常のステレオ合成 |
| `--no-normalize` | ピーク正規化をスキップ | 正規化ON |
| `--csv` | バッチ処理ログをCSVファイルに出力 | 出力なし |
| `--analyze` | MIDI解析のみ（WAV変換なし） | 変換実行 |

### --device オプション

MIDIファイル内のSysExからデバイスを検出し表示します。合成パイプラインには影響しません。

| カテゴリ | 選択肢 |
|---------|--------|
| 汎用 | `GM`, `GS` |
| Roland | `SC-55`, `SC-88`, `SC-88VL`, `SC-8850` |
| Yamaha | `MU-50`, `MU-80`, `MU-100`, `MU-128`, `MOTIF` |

### --channels オプション

各MIDIチャンネルを個別WAVファイルとして出力します。ファイル名にはチャンネル番号、GM楽器名、プログラム番号が含まれます。

```
output/test_09_Acoustic_Grand_Piano_P000.wav
output/test_10_Strings_Ensemble_2_P048.wav
output/test_11_Distortion_Guitar_P030_B128.wav  ← bank指定あり
```

## 対応するMIDI CC

| CC | 名前 | 状態 |
|----|------|------|
| 0 | Bank Select MSB | ○ |
| 1 | Modulation (LFO→Pitch/Filter/Amp) | ○ |
| 2 | Breath Controller | ○ |
| 4 | Foot Controller | ○ |
| 5 | Portamento Time | ○ |
| 6 | Data Entry MSB (RPN) | ○ |
| 7 | Volume | ○ |
| 10 | Pan | ○ |
| 11 | Expression | ○ |
| 32 | Bank Select LSB (14bit bank解決) | ○ |
| 38 | Data Entry LSB (RPN) | ○ |
| 64 | Sustain Pedal | ○ |
| 65 | Portamento On/Off | ○ |
| 91 | Reverb | ○ |
| 93 | Chorus | ○ |
| 94 | Delay | ○ |
| 98/99 | NRPN | ○ |
| 100/101 | RPN (Pitch Bend Range, Fine/Coarse Tune) | ○ |
| 120 | All Sound Off | ○ |
| 121 | Reset All Controllers | ○ |
| 123 | All Notes Off | ○ |

## GS SysEx対応

- Roland GS DT1 SysEx解析（F0 41 10 42 12）
- リバーブ/コーラス/ディレイのパラメータ設定
- パート別エフェクト送り量
- GS Part Mode SysEx（リズム/メロディパート切替）
- デバイス自動検出（SC-55/SC-88/SC-88VL/SC-8850）— 表示のみ

## SoundFont

自動検索で使用するSoundFontの優先順位：
1. Tyroland GS V30（最高品質）
2. FluidR3 GM
3. GeneralUser GS
4. Arachno
5. Timbres of Heaven
6. SGM V2.01
7. FatBoy

`soundfonts/` ディレクトリにSF2ファイルを配置してください。
合成品質は使用するSF2に完全依存します。

## 実装状況

### 完全対応
- CC サンプル精度適用（vol/expr/pan/pb/mod/sus/fx をイベント駆動）
- 14bit Bank Select（CC0+CC32 → SF2 preset解決）
- SF2 loop mode 1/3（mode 3: release 後ループ停止）
- Exclusive class（ドラムのみ: ハイハットチョーク等）
- Biquad LPF（SF2 gen43 明示時のみ、未指定は bypass）
- ソースレベルリバーブ（Dattorro FDN）、コーラス、ディレイ
- GS Part Mode SysEx（リズム/メロディ切替、bank選択に影響）
- GS エフェクトパラメータ（リバーブ/コーラス/ディレイ設定）

### 部分対応
- Per-channel 分割出力: CC初期化済みだが、セグメント分割note処理・FX未適用
- GS Capital/Variation: bank MSB+LSB解決済みだが、SC-8850専用バンクマップなし
- `--device`: SysEx検出・表示のみ、合成パイプライン非連動

### 未対応
- SF2 Modulator（ベロシティ/モジュレーションカーブ未反映）
- アフタータッチ（0xD0 記録のみ）
- SMPTE division 対応（480 tick 固定）

## ライセンス

MIT License
