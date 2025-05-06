# yadif のアルゴリズム

入力: `Cur`, `Prev`, `Next` の3フレーム

`parity = tff ^ !is_second` でparityが決まる。

|tff|is_second|parity|
|---|--------|------|
|0  |0       |1     |
|0  |1       |0     |
|1  |0       |0     |
|1  |1       |1     |

tffなら!is_second(first frame)が1、second frameが0になる。
つまりparity=1 => parity=0の2フレームを出力する。
tffかつparity=1なら、上から0originで偶数行(top)が現在フレーム、奇数行が補完が必要なピクセルになる。
tffかつparity=0なら、上から0originで奇数行(bottom)が現在フレーム、偶数行が補完が必要なピクセルになる。

parity=1のときは`Prev2 = Prev`、`Next2 = Cur`とする。
parity=0のときは`Prev2 = Cur`、`Next2 = Next`とする。

TODO: ここの意味を考える

```javascript
c(x, y) = Cur(x, y - 1)
d(x, y) = avg(Prev(x, y), Next(x, y))
e(x, y) = Cur(x, y + 1)

sp_scorej(j, x, y) =
     absdiff(Cur(x - 1 + j, y - 1), Cur(x - 1 - j, y + 1))
   + absdiff(Cur(x + j, y - 1), Cur(x + j, y + 1))
   + absdiff(Cur(x + 1 + j, y - 1), Cur(x + 1 - j, y + 1)) + (j == 0 ? -1 : 0)
sp_predj(j, x, y) = avg(Cur(x + j, y - 1), Cur(x - j, y + 1))
[sp_score_m, sp_pred_m] = sp_scorej(0, x, y) > sp_scorej(-1, x, y) ?
  [sp_scorej(0, x, y), sp_predj(0, x, y)] :
    sp_scorej(-1, x, y) > sp_scorej(-2, x, y) ?
      [sp_scorej(-2, x, y), sp_predj(-2, x, y)] : [sp_scorej(-1, x, y), sp_predj(-1, x, y)]
[sp_score_p, sp_pred_p] = sp_scorej(0, x, y) > sp_scorej(+1, x, y) ?
  [sp_scorej(0, x, y), sp_predj(0, x, y)] :
    sp_scorej(+1, x, y) > sp_scorej(+2, x, y) ?
      [sp_scorej(+2, x, y), sp_predj(+2, x, y)] : [sp_scorej(+1, x, y), sp_predj(+1, x, y)]
sp_pred(x, y) = sp_score_m > sp_score_p ? sp_pred_m : sp_pred_p

tmp_diff0(x, y) = absdiff(Prev2(x, y), Next2(x, y))
tmp_diff1(x, y) = avg(absdiff(Prev(x, y - 1), c), absdiff(Prev(x, y + 1), e))
tmp_diff2(x, y) = avg(absdiff(Next(x, y - 1), c), absdiff(Next(x, y + 1), e))
diff0(x, y) = max(tmp_diff0 / 2, tmp_diff1, tmp_diff2)
b(x, y) = avg(Prev2(x, y - 2), Next2(x, y - 2))
f(x, y) = avg(Prev2(x, y + 2), Next2(x, y + 2))
diff_max(x, y) = max(d - e, d - c, min(b - c, f - e))
diff_min(x, y) = min(d - e, d - c, max(b - c, f - e))
diff(x, y) = max(diff0(x, y), diff_min(x, y), -diff_max(x, y))

pred(x, y) =
  sp_pred(x, y) > d(x, y) + diff(x, y) ?
    d(x, y) + diff(x, y) :
    sp_pred(x, y) < d(x, y) - diff(x, y) ?
      d(x, y) - diff(x, y) :
      sp_pred(x, y)

dst(x, y) = pred(x, y)
```

b, c, d, e, fとかだとアルゴリズム分からなくなっているので元に戻す

```javascript
sp_scorej(j, x, y) =
     absdiff(Cur(x - 1 + j, y - 1), Cur(x - 1 - j, y + 1))
   + absdiff(Cur(x + j, y - 1), Cur(x + j, y + 1))
   + absdiff(Cur(x + 1 + j, y - 1), Cur(x + 1 - j, y + 1)) + (j == 0 ? -1 : 0)
sp_predj(j, x, y) = avg(Cur(x + j, y - 1), Cur(x - j, y + 1))
[sp_score_m, sp_pred_m] = sp_scorej(0, x, y) > sp_scorej(-1, x, y) ?
  [sp_scorej(0, x, y), sp_predj(0, x, y)] :
    sp_scorej(-1, x, y) > sp_scorej(-2, x, y) ?
      [sp_scorej(-2, x, y), sp_predj(-2, x, y)] : [sp_scorej(-1, x, y), sp_predj(-1, x, y)]
[sp_score_p, sp_pred_p] = sp_scorej(0, x, y) > sp_scorej(+1, x, y) ?
  [sp_scorej(0, x, y), sp_predj(0, x, y)] :
    sp_scorej(+1, x, y) > sp_scorej(+2, x, y) ?
      [sp_scorej(+2, x, y), sp_predj(+2, x, y)] : [sp_scorej(+1, x, y), sp_predj(+1, x, y)]
sp_pred(x, y) = sp_score_m > sp_score_p ? sp_pred_m : sp_pred_p

diff0(x, y) = max(
    absdiff(Prev2(x, y), Next2(x, y)) / 2,
    avg(absdiff(Prev(x, y - 1), Cur(x, y - 1)), absdiff(Prev(x, y + 1), Cur(x, y + 1))),
    avg(absdiff(Next(x, y - 1), Cur(x, y - 1)), absdiff(Next(x, y + 1), Cur(x, y + 1)))
)
diff_max(x, y) = max(
    avg(Prev(x, y), Next(x, y)) - Cur(x, y + 1),
    avg(Prev(x, y), Next(x, y)) - Cur(x, y - 1),
    min(
        avg(Prev2(x, y - 2), Next2(x, y - 2)) - Cur(x, y - 1),
        avg(Prev2(x, y + 2), Next2(x, y + 2)) - Cur(x, y + 1)
    )
)
diff_min(x, y) = min(
    avg(Prev(x, y), Next(x, y)) - Cur(x, y + 1),
    avg(Prev(x, y), Next(x, y)) - Cur(x, y - 1),
    max(
        avg(Prev2(x, y - 2), Next2(x, y - 2)) - Cur(x, y - 1),
        avg(Prev2(x, y + 2), Next2(x, y + 2)) - Cur(x, y + 1)
    )
)
diff(x, y) = max(diff0(x, y), diff_min(x, y), -diff_max(x, y))

pred(x, y) =
  sp_pred(x, y) > avg(Prev(x, y), Next(x, y)) + diff(x, y) ?
    avg(Prev(x, y), Next(x, y)) + diff(x, y) :
    sp_pred(x, y) < avg(Prev(x, y), Next(x, y)) - diff(x, y) ?
      avg(Prev(x, y), Next(x, y)) - diff(x, y) :
      sp_pred(x, y)

dst(x, y) = pred(x, y)
```

最後のpredのところは実際はclampしているので、clamp(val, min, max)があると、こう書ける

```javascript
pred(x, y) = clamp(
    sp_pred(x, y),
    avg(Prev(x, y), Next(x, y)) - diff(x, y),
    avg(Prev(x, y), Next(x, y)) + diff(x, y)
)
```

最終的な結果は空間的な補完値を左右2ピクセルずらしたあたりまで見て、時間方向をチェックした値でクランプしている

注意：sp_scoreの -1は0～255のレンジのときなので、f32計算するなら1.0/255.0になる（何してるんだろう？0を有利にすることで安定化？高速化？）

## WebGPU化

`min`, `max`, `clamp`は使い勝手が良いので使う。
max3, min3を作って置くと良い。
avgは普通に計算して平均を取る。
absdiffも`max - min`で計算できるので、absdiffを作っておく。
実は単純なabsは使ってない。

`select(f, t, cond)`も`sp_pred`の計算のところで使えそう。fが先なのか？（ChatGPTに聞いただけなので要確認）


