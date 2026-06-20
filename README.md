# Mid2Wav

MIDIファイルをWAVに変換するコマンドラインツール。

## 使い方

```bash
pip install -r requirements.txt
python mid2wav.py input.mid -o output.wav
```

## オプション

- `-o, --output`: 出力ファイル名（デフォルト: `<入力ファイル名>.wav`）
- `-s, --sr`: サンプルレート（デフォルト: 44100）
- `-w, --wave`: 波形種類 `square`, `sine`, `sawtooth`（デフォルト: square）
