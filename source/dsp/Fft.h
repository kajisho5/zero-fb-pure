// 小さな複素 FFT（2 のべき乗長・基数 2）。解析専用。確保は prepare() の中だけ。
// 実部と虚部を別々の配列で持ち、内側のループをコンパイラがまとめて計算できる形にしてある。
#pragma once
#include <vector>

namespace zfp
{
class Fft
{
public:
    void prepare (int size); // size は 2 のべき乗（8 以上）
    int size() const noexcept { return n; }

    // その場で順変換する。re・im の長さは size()。
    void forward (float* re, float* im) const noexcept;

private:
    int n = 0;
    std::vector<int> swapA, swapB;   // ビット反転の入れ替え対
    std::vector<float> twRe, twIm;   // 段ごとに連続して並べた回転係数
    std::vector<int> stageOffset;    // 各段の回転係数の先頭位置
};
} // namespace zfp
