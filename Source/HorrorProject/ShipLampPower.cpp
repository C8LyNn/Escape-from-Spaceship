#include "ShipSurvivor.h"
#include "Components/ActorComponent.h"
#include "Components/SpotLightComponent.h"
#include "UObject/UnrealType.h"

namespace
{
    double ReadNumber(const UObject* Object, const TCHAR* Name)
    {
        const FNumericProperty* Field = Object ? FindFProperty<FNumericProperty>(Object->GetClass(), Name) : nullptr;
        return Field && Field->IsFloatingPoint() ? Field->GetFloatingPointPropertyValue(Field->ContainerPtrToValuePtr<void>(Object)) : 0.;
    }

    void WriteNumber(UObject* Object, const TCHAR* Name, double Value)
    {
        const FNumericProperty* Field = Object ? FindFProperty<FNumericProperty>(Object->GetClass(), Name) : nullptr;
        if (Field && Field->IsFloatingPoint())
            Field->SetFloatingPointPropertyValue(Field->ContainerPtrToValuePtr<void>(Object), Value);
    }
}

bool AShipSurvivor::IsWalkingLamp() const
{
    // White was the original white workshop item before the two walking lights were merged.
    return Equipped == TEXT("StockLamp") || Equipped == TEXT("White");
}

bool AShipSurvivor::IsWalkingLampOn() const
{
    return IsWalkingLamp() && OriginalLight.IsValid() && OriginalLight->IsVisible();
}

float AShipSurvivor::WalkingLampPercent() const
{
    const double Max = ReadNumber(StockPower.Get(), TEXT("MaxBatteryLevel"));
    return Max > 0. ? FMath::Clamp(100. * ReadNumber(StockPower.Get(), TEXT("CurrentBatteryLevel")) / Max, 0., 100.) : 0.;
}

float AShipSurvivor::SelectedBatteryPercent() const
{
    if (IsWalkingLamp() && StockPower.IsValid()) return WalkingLampPercent();
    if (Equipped == TEXT("ColorLamp") && bHasColorLamp) return PuzzleCharge;
    if ((Equipped == TEXT("Welder") || Equipped == TEXT("Battery")) && bHasBattery) return Charge;
    return -1.f;
}

void AShipSurvivor::SetSelectedBatteryPercent(float Percent)
{
    const float NewLevel = FMath::Clamp(Percent, 0.f, 100.f);
    if (IsWalkingLamp())
    {
        const double Max = ReadNumber(StockPower.Get(), TEXT("MaxBatteryLevel"));
        WriteNumber(StockPower.Get(), TEXT("CurrentBatteryLevel"), Max * NewLevel / 100.);
    }
    else if (Equipped == TEXT("ColorLamp") && bHasColorLamp) PuzzleCharge = NewLevel;
    else if ((Equipped == TEXT("Welder") || Equipped == TEXT("Battery")) && bHasBattery) Charge = NewLevel;
}

void AShipSurvivor::ApplyWalkingBeamSettings()
{
    USpotLightComponent* Lamp = OriginalLight.Get();
    if (!Lamp || !StockPower.IsValid()) return;
    Lamp->SetIntensity(Beam->Intensity);
    Lamp->SetAttenuationRadius(Beam->AttenuationRadius);
    Lamp->SetInnerConeAngle(Beam->InnerConeAngle);
    Lamp->SetOuterConeAngle(Beam->OuterConeAngle);
    // The original component owns the timer and cells. Convert percent per second to units per pulse.
    const double PulseSeconds = ReadNumber(StockPower.Get(), TEXT("DepletionSpeed"));
    const double Full = ReadNumber(StockPower.Get(), TEXT("MaxBatteryLevel"));
    if (PulseSeconds > 0. && Full > 0.)
        WriteNumber(StockPower.Get(), TEXT("DepletionAmount"), GetLampDrainPerSecond() * PulseSeconds * Full / 100.);
}
