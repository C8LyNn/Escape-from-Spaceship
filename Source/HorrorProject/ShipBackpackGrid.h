#pragma once

#include "Containers/UnrealString.h"

class APlayerController;

namespace ShipBackpackGrid
{
    constexpr int32 PageSize = 8;

    FString CapacityLabel(int32 Count);
    bool Expand(APlayerController* PC, int32 ExistingCapacity, int32 TargetCapacity);
    bool ShowPage(APlayerController* PC, int32 Page);
}
