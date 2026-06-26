# namecard_pdf — コンテナ形式仕様

> **STATUS: 実装済み（v1）。** 本ドキュメントが TypeScript 出力ライブラリと C++
> パースライブラリの「唯一の真実」です。両実装はこの仕様に追従させること。実装の
> 場所とテストは末尾 §8 を参照。WebApp / LVGL とは切り離してあり、UI 統合（共有画
> 像レイアウトの作り込み、`name_glyphs`→`lv_font` アダプタ）は別タスク。

## 1. 目的とスコープ

NameCardKnot 用の名刺データを 1 つの **PDF ファイル** に収め、次の 3 者すべてが
扱えるようにする:

1. **人間** — 普通の PDF ビューア/プリンタで開ける正規の PDF。
   - **1 ページ目** = 表示用画像（名刺の表）
   - **2 ページ目** = 共有データの確認面（URL・メッセージ・共有画像）
2. **TypeScript エディタ** (`editor/`) — 上記 PDF を **生成**する（自前ライタ）。
3. **ESP32 ファーム** (`components/namecard_pdf`) — 上記 PDF を **パース**して
   名前/URL/メッセージ/各 JPEG を取り出す。さらに **表示用画像を削除し、共有用
   データだけを残した PDF を生成**できる（= 共有用 PDF の書き出し）。

非スコープ: 汎用 PDF パーサ/ライタにはしない。本仕様が定める **正規レイアウト**
だけを読み書きできれば良い。

### ファイル拡張子

通常の PDF と区別するため、中身は正規の PDF のまま **二段の拡張子**を付ける:

- **`.mnc.pdf`** (my name card) — 表示用＋共有用を含むフルファイル（§2 の (A)+(B)）。
- **`.snc.pdf`** (shared name card) — 共有用データだけのファイル（(A) のみ）。device が
  フルファイルを `base_total_length` で切り詰めて生成するのもこれ。

どちらも普通の PDF ビューアで開ける（`.pdf` で終わる）。拡張子は保存/共有時の命名規約で
あり、フォーマット判定は末尾の `NCKEND01` マジックで行う（拡張子に依存しない）。

## 2. 中心アイデア — 「共有用 PDF = 先頭を切り出すだけ」

ESP32 で「表示画像を消して共有データだけ残す」を、**デバイス側で PDF を書き直さ
ず、ファイルの先頭部分をそのままコピー（切り詰め）するだけ**で実現する。

そのためにフルの PDF を 2 段構成で出力する:

```
┌──────────────────────────────────────────────┐  ← オフセット 0
│ (A) ベース PDF = 共有用データだけの完全な PDF      │
│     ・Catalog / Pages(1ページ)                   │
│     ・共有ページの画像 XObject 群                  │
│       (共有画像1/2、URL+メッセージのレンダ画像)     │
│     ・name/url/message のクリーン UTF-8 データ流    │
│     ・name_glyphs（収録外字形のみ・空可）          │
│     ・xref / trailer / %%EOF                     │
│     ・NCK フッタ A (共有アイテムのみの索引)         │
├──────────────────────────────────────────────┤  ← base_total_length（切り詰め点）
│ (B) インクリメンタル更新 = 表示画像の追加           │
│     ・表示画像 XObject (大きい JPEG)              │
│     ・表示ページ obj + content                    │
│     ・更新後 Pages(/Kids[表示,共有] /Count 2)      │
│       /Prev で (A) の xref に連結                 │
│     ・xref / trailer / %%EOF                     │
│     ・NCK フッタ B (フル索引 = 共有 + 表示)         │
└──────────────────────────────────────────────┘  ← EOF
```

PDF の **インクリメンタル更新**（末尾に追記して旧オブジェクトを上書き）は PDF 標
準機能なので、(A)+(B) 全体が普通のビューアで開ける 2 ページ PDF になる。ページの
**表示順**は更新後 Pages の `/Kids` 配列で決めるので、ファイル内のバイト順と無関
係に「1 ページ目 = 表示画像」にできる。

### 共有用 PDF の生成（デバイス側）

1. 末尾の NCK フッタ B を読み、`base_total_length` を得る。
2. ファイル先頭から `base_total_length` バイトを新ファイルにコピー。
3. 完成。コピー結果は **(A) そのまま** = NCK フッタ A 付きの完全な 1 ページ共有用
   PDF。**PDF の再シリアライズも xref 修正も不要**（バイトコピーのみ）。

> この設計により、重い処理（PDF 構築・オフセット計算）は TS 側に寄せ、デバイス側
> は「末尾を読む」「先頭をコピーする」だけで済む。[確定したい方針]

## 3. PDF 本体のレイアウト

> 自前ライタは「最初は小さく」。オブジェクト構成は固定テンプレートとし、両言語が
> 同一バイトを出すことをゴールにする（= ゴールデンテストが容易）。[確定したい方針]

### 3.1 共有データの埋め込み方式 — Type / Length / Offset 索引

データ（文字列・画像）はすべて **PDF 本体に実体として埋め込み**、NCK フッタは各実
体への **{Type, Offset, Length}** を記録するだけにする（§4）。デバイスはフッタの
索引から直接 seek してバイトを読むので、**PDF 文法（xref・辞書）を解釈しない**。

- **画像 (JPEG)**: 画像 XObject の `stream`〜`endstream` の中身の生バイトを指す。
  この生バイトはそのまま baseline JPEG なので `imgproc::decode_buffer` に直接渡せ
  る。
- **文字列 (UTF-8)**: ページの描画用テキストとは**別に**、クリーンな UTF-8 を持
  つ専用のデータ流オブジェクトを置き、索引はその生バイトを指す。
  - 理由: ページ描画側の `(...) Tj` は PDF エスケープ（`( ) \`）が混ざりデバイス
    から読みづらい。文字列は小さいので二重持ちのコストは無視できる。[要検討:
    本当に二重持ちで良いか / データ流だけにして描画はラスタ画像に委ねるか]

### 3.2 文字の描画方針 — 「誰がフォントを持つか」で分ける [確定したい方針]

| データ | 格納物 | PC/PDF の描画 | ESP32 の描画 |
|--------|--------|---------------|--------------|
| URL    | UTF-8 文字列のみ | 非埋め込みフォント（base-14） | UTF-8 をそのまま |
| メッセージ | UTF-8 文字列のみ | 非埋め込み Type0 + 定義済み CMap（`UniJIS-UCS2-H` 等）→ ビューアが代替フォント | UTF-8 をそのまま（任意で LVGL フォント表示） |
| 名前   | UTF-8 ＋ `name_glyphs`（収録外字形のみ） | UTF-8 文字列（PC の標準フォント任せ） | **`name`(UTF-8) をラベル描画**（折り返し可）。内蔵 lv_font ＋ `name_glyphs`→lv_font を fallback に繋ぐ |

- **URL・メッセージ・名前の PC 表示**はすべて表示する PC にフォントがある前提で
  **文字列だけ**埋め込む（最近の PC は難字も標準フォントに収録済み）。PDF はフォント
  非埋め込み（日本語は定義済み CMap 参照でビューアに代替させる）。**`name_vector` は
  廃止**。
- **名前は ESP32 でも テキストとして**描く（折り返し可・ASCII はゼロコスト）。device
  内蔵 lv_font でカバーできない字だけを `name_glyphs` で補う:
  - 内蔵フォントの収録字 = **`docs/jp_glyphs.txt`**（ASCII＋かな＋常用漢字＋記号）。
    M5Paper の内蔵 lv_font はこのセットから生成する想定。
  - エディタは **`jp_glyphs.txt` に無い字だけ**を `name_glyphs` に埋め込む（普通の名前
    なら空になる）。ESP32 は `name`(UTF-8) をラベルで描き、内蔵フォントを primary・
    `name_glyphs` 由来の lv_font を **fallback** に繋ぐ。LVGL の fallback は「primary に
    無い字」だけ使うので、収録字リストが多少ズレても致命的にならない（保険）。
- **glyph 形式**: blob ヘッダ（px サイズ・bpp・ベースライン等）＋ グリフ表（codepoint,
  adv_w, box_w/h, ofs_x/y, bitmap）。LVGL v9 のランタイム `get_glyph_dsc_cb`/
  `get_glyph_bitmap_cb` で引くので **LVGL のバイナリフォント形式に合わせる必要はない**。
  blob → `lv_font_t` 変換は **app 側アダプタ**（`lv_image_adapter.hpp` 同様）で、パーサ
  component は LVGL-free のまま。
- **描画 px サイズは 48px 決め打ち**（両側ハードコード。blob ヘッダには自己記述と
  して記録するが、共有定数や版管理の仕組みは当面作らない）。グリフラスタライズは
  エディタが canvas（ブラウザのフォント）で行う。`jp_glyphs.txt` はエディタにインラ
  インで保持してよい（fallback 設計でズレを許容するので device 側との厳密同期は不要）。

### 3.3 オブジェクト構成（確定・PDF 1.7）

**共通の決定事項**:
- **MediaBox（pt=px 1:1、実機表示のみ基準、印刷/名刺実寸は考慮しない）**:
  - **page1（表示画像）= `[0 0 540 960]`**（EPD 表示と一致・縦）
  - **page2（共有確認面）= `[0 0 1920 1080]`**（横・確定）
- **xref は旧来の xref テーブル**（cross-reference stream は使わない）。
- **ストリームは無圧縮**（content/データ流は生バイト、画像は DCTDecode 済み JPEG）。
- **画像 XObject の ColorSpace は `/DeviceRGB`**、`/BitsPerComponent 8`、`/Filter /DCTDecode`。
- **テキスト**: URL は base-14 `/Helvetica`。名前/メッセージは**非埋め込み Type0**
  （`/Encoding /UniJIS-UCS2-H`＋Adobe-Japan1 の CIDFont、`FontFile` 無し→ビューア代替）。
  描画文字列は UCS-2BE の hex 文字列 `<....> Tj`。
- オブジェクト番号は採番順。**(A) のオブジェクト数は可変**（共有画像の枚数・page2 の
  装飾を足せる）。footer は実際に書き出したバイトオフセットを記録するので番号に依存
  しない。(B) は (A) の `/Size` から継番する。

(A) ベース領域（= 共有用 PDF そのもの。番号は一例）:

```
%PDF-1.7
1 0 obj  Catalog    << /Type /Catalog /Pages 2 0 R >>
2 0 obj  Pages      << /Type /Pages /Kids [ 3 0 R ] /Count 1 >>          ← (B) で上書き
3 0 obj  page2 Page << /Parent 2 0 R /MediaBox [0 0 1920 1080]
                       /Resources << /Font << /F1 5 0 R /F2 6 0 R >>
                                     /XObject << /S0 9 0 R /S1 10 0 R … >> >>  ← 装飾追加可
                       /Contents 4 0 R >>
4 0 obj  content 流  URL/名前/メッセージのテキスト描画＋共有画像配置（＋任意の装飾）
5 0 obj  Font       << /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>
6 0 obj  Font(JP)   << /Type /Font /Subtype /Type0 /BaseFont /Ryumin-Light
                       /Encoding /UniJIS-UCS2-H /DescendantFonts [ 7 0 R ] >>
7 0 obj  CIDFont    << /Type /Font /Subtype /CIDFontType0 /BaseFont /Ryumin-Light
                       /CIDSystemInfo << /Registry (Adobe) /Ordering (Japan1) /Supplement 7 >>
                       /FontDescriptor 8 0 R >>
8 0 obj  FontDesc   << /Type /FontDescriptor /FontName /Ryumin-Light /Flags 6 … >> （FontFile 無し）
9 0 obj  共有画像 XObj  /Subtype /Image /ColorSpace /DeviceRGB /Filter /DCTDecode …
10 0 obj 共有画像 XObj  （0..n 枚。装飾用 XObj をここに足してよい）
…  name データ流  << /Length L >> stream\n<クリーン UTF-8>\nendstream   ← footer が指す
…  url  データ流
…  message データ流
xref（旧来テーブル, 0..N）
trailer << /Root 1 0 R /Size N+1 >>
startxref <(A) xref 先頭>
%%EOF
<NCK フッタ A>                                                         ← ここまでが base_total_length
```

(B) インクリメンタル更新領域（追記。(A) の `/Size` を M とする）:

```
M+0 0 obj 表示画像 XObj  /Subtype /Image /ColorSpace /DeviceRGB /Filter /DCTDecode …  ← STRIP 対象
M+1 0 obj 表示 Page      << /Parent 2 0 R /MediaBox [0 0 540 960]
                           /Resources << /XObject << /Im (M+0) 0 R >> >> /Contents (M+2) 0 R >>
M+2 0 obj content 流     表示画像を全面配置（cm [540 0 0 960 0 0]）
2 0 obj   Pages（上書き） << /Type /Pages /Kids [ (M+1) 0 R 3 0 R ] /Count 2 >>  ← 表示=1ページ目
xref（更新分のみ）/Prev <(A) xref 先頭>
trailer << /Root 1 0 R /Size M+3 /Prev <(A) xref 先頭> >>
startxref <(B) xref 先頭>
%%EOF
<NCK フッタ B>                                                         ← フル索引・base_total_length 記録
```

- **データ流（clean UTF-8）は参照されない孤立オブジェクトで良い**（PDF 的に合法）。
  footer のエントリは `stream`〜`endstream` 内の生 UTF-8 バイトを offset/length で指す。
  描画側（content 流）は別途 Type0/Helvetica 用に再エンコードするので二重持ちになるが
  文字列は小さい（§3.1 / #5 の決定）。
- **page2 の装飾**: `content 流` に描画命令を、`Resources` に XObject/Font を足せば自由に
  拡張できる。footer のバイトオフセットは実書き出し位置を指すので影響しない。

## 4. NCK フッタ形式

フッタ A・B は同一構造。すべて **リトルエンディアン**（ESP32 が LE）。文字列実体は
PDF 本体側（§3.1）にあり、フッタは索引のみ。

### 4.1 ヘッダ + 索引（フッタ先頭から）

| off | size | field        | 説明 |
|-----|------|--------------|------|
| 0   | 4    | `magic`      | ASCII `"NCK1"` |
| 4   | 2    | `version`    | u16 = 1 |
| 6   | 2    | `flags`      | u16（予約） |
| 8   | 2    | `entry_count`| u16 |
| 10  | 2    | `reserved`   | u16 = 0 |
| 12  | …    | `entries[]`  | 各 16 バイト（下記） |

各エントリ（16 バイト）:

| off | size | field    | 説明 |
|-----|------|----------|------|
| 0   | 1    | `type`   | 0=name, 1=url, 2=message, 3=display_jpeg, 4=share_jpeg, 5=name_glyphs |
| 1   | 1    | `flags`  | bit0 = `STRIP_ON_SHARE`（共有用では除外） |
| 2   | 1    | `subtype`| 画像系の符号化: 0=jpeg, 1=png。`name_glyphs` は 0=自前グリフ表形式。文字列は 0 |
| 3   | 1    | `reserved` | u8 = 0 |
| 4   | 4    | `offset` | u32 実体の絶対ファイルオフセット |
| 8   | 4    | `length` | u32 実体のバイト長 |
| 12  | 4    | `crc32`  | u32 実体の CRC32（zlib 多項式） |

- エントリは 16 バイト。同じ `type` が複数可（共有画像が 2 枚なら `type=4` が 2 個）。
- `display_jpeg`(3) のみ `STRIP_ON_SHARE` を立て、(B) 区画に置く。name・name_glyphs・
  url・message・share は (A) 区画なので共有用 PDF にも残る。
- **ESP32 が読むのは** `name`(0)/`url`(1)/`message`(2)/`display_jpeg`(3)/`share_jpeg`(4)/
  `name_glyphs`(5)。PC/PDF は文字列と画像のみ使い、`name_glyphs` は無視する。
- offset/length を u32 とするのでファイル上限 4GB（実際は数百 KB）。[要検討: u64 化]

### 4.2 末尾トレーラ（ファイル EOF から固定 24 バイトを読む）

後方スキャン不要。ファイル末尾の 24 バイトをそのまま読み、`magic_end` を検証する。

| off (EOF 基準) | size | field              | 説明 |
|----------------|------|--------------------|------|
| -24 | 4 | `base_total_length` | u32 共有用 PDF の切り詰め長（= (A)+フッタ A の終端）。**フッタ A では 0**（既に最小） |
| -20 | 4 | `footer_offset`     | u32 この `"NCK1"` ヘッダの絶対オフセット |
| -16 | 4 | `footer_crc32`      | u32 `[footer_offset .. このトレーラ直前)`（ヘッダ＋索引）の CRC32 |
| -12 | 4 | `reserved`          | u32 = 0 |
| -8  | 8 | `magic_end`         | ASCII `"NCKEND01"` |

- `version` はトレーラに重複させない（`footer_offset` 先頭のヘッダにある §4.1）。
- **CRC32 は zlib 標準**（多項式 0xEDB88320、init/xorout = 0xFFFFFFFF）。各アセットの
  `crc32`（§4.1）は実体バイトに対して、`footer_crc32` はヘッダ＋索引に対して計算する。
- `base_total_length` で先頭から切り詰めると **フッタ A 付きの完全な共有用 PDF** に
  なる（§2）。切り詰め後ファイルの末尾 24 バイトはフッタ A のトレーラで、その
  `base_total_length` は 0。

### 4.3 デバイスの読み取り手順

1. ファイル末尾 24 バイトを読み、`magic_end`(`"NCKEND01"`) を確認。
2. `footer_offset` へ seek し `magic`/`version` 検証、`entries[]` を読む。
3. 必要な type の `offset/length` へ seek し実体を読む（CRC 検証）。
   - 画像 → そのまま `imgproc::decode_buffer`。
   - 文字列 → クリーン UTF-8 をそのまま利用。
4. 共有用 PDF を作るとき → `base_total_length` が 0 でなければ
   `[0, base_total_length)` をコピー（§2）。

### 4.4 `name_glyphs` blob 形式（type=5, subtype=0）

footer エントリ `name_glyphs` が指す実体。device は `name`(UTF-8) をラベル描画する
際、これを `lv_font_t` に変換して内蔵フォントの **fallback** に繋ぐ（§3.2）。すべて
リトルエンディアン。

ヘッダ（16 バイト）:

| off | size | field        | 説明 |
|-----|------|--------------|------|
| 0   | 4    | `magic`      | ASCII `"NGLY"` |
| 4   | 1    | `version`    | u8 = 1 |
| 5   | 1    | `bpp`        | u8 = 1（1bpp 固定） |
| 6   | 1    | `compress`   | u8 = 1（bit ラン RLE/varint。0=無圧縮） |
| 7   | 1    | `reserved`   | u8 = 0 |
| 8   | 2    | `glyph_px`   | u16 描画 px サイズ（既定 48、§3.2） |
| 10  | 2    | `base_line`  | u16 ベースライン（上端からの px、内蔵フォントと一致） |
| 12  | 2    | `line_height`| u16 行高 px（内蔵フォントと一致） |
| 14  | 2    | `glyph_count`| u16 |

グリフ表（`glyph_count` × 16 バイト、**`codepoint` 昇順** = device は二分探索可）:

| off | size | field      | 説明 |
|-----|------|------------|------|
| 0   | 4    | `codepoint`| u32 Unicode スカラ値 |
| 4   | 2    | `adv_w`    | u16 **文字幅（送り幅）**。1/16 px の 8.4 固定小数（LVGL 互換） |
| 6   | 1    | `box_w`    | u8 ビットマップ幅 px |
| 7   | 1    | `box_h`    | u8 ビットマップ高 px |
| 8   | 1    | `ofs_x`    | i8 左サイドベアリング（pen からの x） |
| 9   | 1    | `ofs_y`    | i8 ベースラインからの下端 y（上が正） |
| 10  | 4    | `data_off` | u32 データ部先頭からの相対オフセット |
| 14  | 2    | `data_len` | u16 RLE バイト長 |

データ部: 各グリフの RLE バイト列を連結。展開先は **`box_w*box_h` ビットの連続
ビット列**（行優先・MSB first・行パディング無し）。RLE は §（確定）の「先頭=色0 の
交互ラン、ラン長 varint(LEB128)」。`box_w*box_h==0`（空白文字など）はデータ長 0。

> `adv_w` だけ持つグリフ（描画ビットマップ無し＝スペース等）も表に入れてよい
> （`box_w=box_h=0`, `data_len=0`）。複数 px サイズが要る場合は `name_glyphs` を
> px ごとに複数エントリ（footer 側）として持つ。[要検討: 当面は単一 px]

## 5. 公開 API（叩き台）

### 5.1 TypeScript (`editor/src/lib/namecard-pdf/`, React 非依存)

```ts
interface NameCardInput {
  name: string; url: string; message: string;     // クリーン UTF-8（正本）
  display: Uint8Array;            // baseline JPEG（canvas.toBlob('image/jpeg')）
  shares?: Uint8Array[];          // baseline JPEG
  // name_glyphs はライブラリが内部生成: name の各字を docs/jp_glyphs.txt と突き合わせ、
  // 収録外の字だけを glyphPx（既定 48）で canvas ラスタライズして格納（§3.2）
  glyphPx?: number;               // 既定 48。device 内蔵フォントの px と一致させる
}
function buildNameCardPdf(input: NameCardInput): Uint8Array;   // フル PDF（A+B+フッタ B）
// テスト/自己検証用
function buildSharePdf(input): Uint8Array;                      // = (A)+フッタ A 単体
function parseFooter(pdf: Uint8Array): NckIndex;
```

### 5.2 C++ (`components/namecard_pdf/`, LVGL-free・依存ゼロ・host テスト可)

```cpp
namespace nckpdf {
  enum class Status { Ok, NotNckPdf, BadMagic, VersionUnsupported,
                      Truncated, CrcMismatch, BadArgument, OpenFailed, IoError };
  struct Asset { uint8_t type, flags; uint32_t offset, length, crc; };
  struct Card  { std::string name, url, message; std::vector<Asset> assets; };

  Status parse_buffer(const void* data, size_t len, Card& out);   // RAM 上/テスト
  Status open_file(const char* path, Card& out);                  // SD: 末尾→seek でメタのみ
  Status read_asset(const char* path, const Asset&, void* buf, size_t buflen);

  // 表示画像を削いだ共有用 PDF を書き出す（§2: base_total_length まで丸ごとコピー）
  Status write_share_pdf(const char* src_path, const char* dst_path);
}
```

`imgproc` の `Status`/`Image` 様式に合わせる。RAM に PDF 全体を載せない（画像が
100KB+ になり得るため seek ベース）。

## 6. テスト戦略（実装済み）

- **仕様正本**: 本ドキュメント。両実装はここに追従。
- **TS 単体 (vitest)** `editor/`: foundation（crc32/RLE/footer/glyphs）ラウンドトリップ、
  `buildNameCardPdf` がフッタ索引経由で全アセットを復元できる、2 ページ PDF の構造
  チェック。（`pdfjs-dist` での描画検証は未導入＝将来。代わりに poppler `pdfinfo`/
  `pdftoppm` で手動確認済み。）
- **共有用切り詰めの相互検証**: `buildNameCardPdf` を `base_total_length` で切り詰めた
  結果が `buildSharePdf` と **バイト一致**（§2 の核心。TS テスト＋gen-fixtures で確認）。
- **C++ host 単体 (g++, no ESP-IDF)**: fixtures をパースしメタ＆全アセットの offset/
  length/crc を `manifest.h` と照合、`write_share_pdf` 結果が共有用ゴールデンと **バイト
  一致**、`name_glyphs` の RLE 展開を既知パターンと照合、破損/切り詰め/null は
  `Status` エラー。`NCK_SAN=1` で ASAN/UBSAN（ランタイムがある環境のみ）。
- **★ クロス言語ゴールデン**: `npm run gen-fixtures` が
  `components/namecard_pdf/test/fixtures/*.pdf` ＋ `manifest.json` ＋ `manifest.h`
  （期待 name/message/各アセット offset・length・crc32・sha256・`base_total_length`）を
  生成。C++ テストが `manifest.h` を読んで一致を主張＝両言語を同一バイトに縛る。
- **E2E**: 抽出 JPEG → `imgproc::decode_buffer`（`run_e2e.sh`、両 component をリンク）。

## 7. 確定が必要な論点（叩き台のうち未決）

- ~~2 ページ目の文字描画~~ → **確定**: URL/メッセージ/名前は PC では文字列のみ
  （非埋め込みフォント）。`name_vector` は廃止。(§3.2)
- ~~名前を device でどう出すか~~ → **確定**: `name`(UTF-8) をテキスト描画＋内蔵
  lv_font の fallback として `name_glyphs`（`jp_glyphs.txt` 収録外の字のみ、既定 48px）。
- ~~テキスト/字形を誰が作るか~~ → **確定**: すべてエディタ。device は描き直さない。

- ~~`name_glyphs` の形式~~ → **確定**: §4.4 のレイアウト。1bpp、bit ラン RLE(varint)
  圧縮、`adv_w` は 1/16px 8.4 固定小数。
- ~~px サイズの調整/共有~~ → **確定**: 48px 決め打ち、共有/版管理はしない。
- ~~`jp_glyphs.txt` の同期~~ → **確定**: device は LVGL UI 側で生成、エディタはインラ
  イン保持。fallback 設計でズレ許容、厳密同期不要。

- ~~§3.3 PDF 本体~~ → **確定**: MediaBox `[0 0 540 960]`(両頁)、xref テーブル、無圧縮
  ストリーム、DeviceRGB、page2 は Helvetica(URL)＋非埋め込み Type0/UniJIS-UCS2-H
  (名前/メッセージ)、clean UTF-8 は孤立ストリーム obj、装飾追加可、(B) は `/Size` 継番。
- ~~§4.2 フッタ末尾~~ → **確定**: EOF から固定 24 バイト、`base_total_length`/
  `footer_offset`/`footer_crc32`/`reserved`/`magic_end`、CRC32 は zlib 標準。

実装初期値（最低限動作させてから調整する暫定値）:

1. **Type0 の `BaseFont`** = `/Ryumin-Light`（Adobe-Japan1、多くのビューアが代替）。
   ASCII は `/Helvetica`。
2. **`FontDescriptor`** = `Flags 4`(Symbolic), `FontBBox [-100 -250 1100 900]`,
   `ItalicAngle 0`, `Ascent 880`, `Descent -120`, `CapHeight 700`, `StemV 80`
   （FontFile 無し）。
3. **page2 レイアウト**（1920×1080）: 上部に名前(大)、その下に URL、本文にメッセージ、
   下部に共有画像を横並び。座標・装飾はエディタの描画ポリシーとして実装時に調整。

## 8. 実装の場所とテスト実行

**TypeScript 出力ライブラリ** — `editor/src/lib/namecard-pdf/`（React 非依存）:
`constants.ts`（バイト定数）/ `bytes.ts`（LE writer）/ `crc32.ts` / `rle.ts`（bit ラン
RLE）/ `footer.ts`（NCK フッタ）/ `glyphs.ts`（`name_glyphs` blob）/ `pdf-writer.ts`
（自前 PDF ライタ）/ `index.ts`（`buildNameCardPdf` / `buildSharePdf` / 入力型
`NameCardInput`）/ `gen-fixtures.ts`（ゴールデン生成）。グリフのラスタライズ（canvas）と
`jp_glyphs.txt` 突き合わせは未実装＝ブラウザ統合時に `nameGlyphs?: GlyphSet` を渡す。

```sh
cd editor && npm install
npm test               # vitest（foundation + PDF + 切り詰め一致）
npm run gen-fixtures   # components/namecard_pdf/test/fixtures/ を再生成
```

**C++ パースライブラリ** — `components/namecard_pdf/`（in-tree・依存ゼロ・LVGL-free。
device は `../components` コンテナで自動検出、host は自前テスト）:
`inc/namecard_pdf.hpp` 公開 API（`parse_buffer`/`open_file`/`read_asset`/
`write_share_pdf`/`parse_name_glyphs`/`rle_decode`/`crc32`）、`src/namecard_pdf.cpp`。
まだ app/simulator/device のビルドには結線していない（切り離し維持。統合は別タスクで
`SIMULATOR_COMPONENTS` 追加＋consumer の `REQUIRES namecard_pdf`）。

```sh
nix develop -c sh components/namecard_pdf/test/run.sh       # 単体（ゴールデン照合）
nix develop -c sh components/namecard_pdf/test/run_e2e.sh   # E2E（→ image_processor）
```

`test/assets/*.jpg` は libjpeg で一度生成した入力（committed）、`test/fixtures/` は
`gen-fixtures` 出力（committed、`manifest.h` が C++ テストのゴールデン）。
