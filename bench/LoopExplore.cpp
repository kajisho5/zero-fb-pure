// 閉ループ試験の数値を一覧で出す（調整と報告用。合否は tests 側）。
#include "../tests/ClosedLoop.h"
#include <cstdio>
#include <cstdlib>

using namespace zfptest::loop;

int main (int argc, char** argv)
{
    const double seconds = argc > 1 ? std::atof (argv[1]) : 14.0;
    struct Room { const char* name; double rt60; float rho; std::uint32_t seed; };
    const Room rooms[] = { { "ROOM-A", 0.4, 0.05f, 1001u }, { "ROOM-B", 0.9, 0.04f, 1002u }, { "ROOM-C", 1.6, 0.03f, 1003u } };

    for (const auto& room : rooms)
    {
        const auto h = makeRoom (room.rt60, room.rho, room.seed);
        for (const double delay : { 5.0, 12.0, 25.0 })
        {
            // G0：プラグインなしで発散しない最大のゲイン（0.1 dB 刻みの二分探索）。
            double lo = -12.0, hi = 6.0;
            while (hi - lo > 0.1)
            {
                const double mid = 0.5 * (lo + hi);
                const auto st = analyse (simulate (h, delay, mid, 6.0, nullptr), 1.0);
                (st.diverged ? hi : lo) = mid;
            }
            const double g0 = lo;

            std::printf ("%s D=%2.0fms G0=%+.1f |", room.name, delay, g0);
            for (const double over : { 3.0, 6.0, 9.0, 12.0 })
            {
                zfp::Engine e;
                e.prepare (kFs, 1, 1);
                const auto run = simulate (h, delay, g0 + over, seconds, &e);
                const auto st = analyse (run, 10.0);
                const auto ev = trackModes (run.x);
                int repeats = 0;
                double worstHop = 0.0, firstHop = 0.0, worstPeak = -200.0;
                for (std::size_t i = 0; i < ev.size(); ++i)
                {
                    repeats += ev[i].repeat;
                    const double hopT = (ev[i].peakSec - ev[i].emergeSec) * 1000.0;
                    if (i == 0) firstHop = hopT; else worstHop = std::max (worstHop, hopT);
                    worstPeak = std::max (worstPeak, ev[i].peakDb);
                }
                int engaged = 0; float sumDb = 0.0f;
                for (int b = 0; b < zfp::kNumBands; ++b)
                {
                    engaged += e.getBandGainsDb()[b] < -1.0f;
                    sumDb += e.getBandGainsDb()[b];
                }
                std::printf (" +%g: %s ev=%zu rep=%d hop1=%.0f hopN=%.0f pk=%.0f tail5=%.0f n=%d avg=%.1f |", over,
                             st.diverged ? "DIV" : "ok ", ev.size(), repeats, firstHop, worstHop, worstPeak,
                             st.tail5RmsDb, engaged, sumDb / float (zfp::kNumBands));
            }
            std::printf ("\n");
            std::fflush (stdout);
        }
    }
    return 0;
}
