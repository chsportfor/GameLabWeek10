#pragma once

#include "LightweightStats.h"

namespace DecalStats
{
    // 버퍼 준비와 Draw 명령 제출에 걸린 CPU 시간이다. GPU가 픽셀을 칠하는 시간은 포함하지 않는다.
    DECLARE_CYCLE_STAT("Decal Pass CPU", STAT_RenderCPU);

    // Scene에 등록된 전체 Decal 수다.
    DECLARE_COUNTER_STAT("Decal Total", STAT_Total);
    // Fade로 건너뛴 Decal은 세지 않고, 여러 View의 Draw는 합산한다.
    DECLARE_COUNTER_STAT("Decal Draw Calls", STAT_DrawCalls);
    DECLARE_COUNTER_STAT("Decal Additive Draw Calls", STAT_AdditiveDrawCalls);
}
