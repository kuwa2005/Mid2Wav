# Mid2Wav

MIDIファイルをSoundFontベースで高品質WAVに変換するC++コマンドラインツール。
SC-8850レベルのGS MIDI演奏再現を目指す。

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
| `--device <model>` | デバイスエミュレーション（下記参照） | auto（SysExから自動検出） |
| `--pitch <semitones>` | 全ノートの音程シフト（半音単位）。+12で+1オクターブ、-12で-1オクターブ | 0 |
| `--channels` | 各MIDIチャンネルを個別WAVファイルとして出力 | 通常のステレオ合成 |
| `--no-normalize` | ピーク正規化をスキップ | 正規化ON |
| `--csv` | バッチ処理ログをCSVファイルに出力 | 出力なし |
| `--analyze` | MIDI解析のみ（WAV変換なし） | 変換実行 |

### --device オプション

| カテゴリ | 選択肢 | 説明 |
|---------|--------|------|
| 汎用 | `GM` | General MIDI互換 |
| 汎用 | `GS` | Roland GS互換 |
| Roland | `SC-55` | Roland SC-55相当 |
| Roland | `SC-88` | Roland SC-88相当 |
| Roland | `SC-88VL` | Roland SC-88VL相当 |
| Roland | `SC-8850` | Roland SC-8850相当 |
| Yamaha | `XG` | Yamaha XG互換 |
| Yamaha | `MU-50` | Yamaha MU-50相当 |
| Yamaha | `MU-80` | Yamaha MU-80相当 |
| Yamaha | `MU-100` | Yamaha MU-100相当 |
| Yamaha | `MU-128` | Yamaha MU-128相当 |
| Yamaha | `MOTIF` | Yamaha MOTIF相当 |

`auto`（デフォルト）ではMIDIファイル内のSysExメッセージからデバイスを自動検出します。

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
| 32 | Bank Select LSB | ○ |
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
- デバイス自動検出（SC-55/SC-88/SC-88VL/SC-8850）

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

## ライセンス

MIT License
