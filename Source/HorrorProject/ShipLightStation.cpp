#include "ShipLightStation.h"
#include "ShipSurvivor.h"

#include "Components/BoxComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"

namespace
{
    constexpr float SprintThreshold = 300.f;
    constexpr float ReserveLimit = 35.f;
    const FLinearColor Ready(.08f, 1.f, .36f);
    const FLinearColor Idle(.1f, .5f, 1.f);
    const FLinearColor Alert(1.f, .12f, .025f);

    FString ColorName(FName Color)
    {
        if (Color == TEXT("Red")) return TEXT("czerwony");
        if (Color == TEXT("Green")) return TEXT("zielony");
        if (Color == TEXT("Blue")) return TEXT("niebieski");
        if (Color == TEXT("UV")) return TEXT("fioletowy (UV)");
        if (Color == TEXT("White")) return TEXT("biały");
        return Color.ToString();
    }
}

AShipLightStation::AShipLightStation()
{
    Kind = TEXT("LightStation");
    RequiredFilter = NAME_None;
    ColorSequence = {FName(TEXT("Red")), FName(TEXT("Blue")), FName(TEXT("UV"))};
}

FName AShipLightStation::PrimaryColor() const
{
    if (!RequiredFilter.IsNone()) return RequiredFilter;
    switch (Mode)
    {
        case ELightStationMode::Switch:
        case ELightStationMode::Dynamo: return TEXT("Green");
        case ELightStationMode::Memory: return TEXT("UV");
        case ELightStationMode::Shutter: return TEXT("Blue");
        default: return TEXT("Red");
    }
}

FName AShipLightStation::ClosingColor() const
{
    const FName OpenColor = PrimaryColor();
    if (!AlternateColor.IsNone() && AlternateColor != OpenColor) return AlternateColor;
    return OpenColor == TEXT("Red") ? FName(TEXT("Green")) : FName(TEXT("Red"));
}

void AShipLightStation::BeginPlay()
{
    Super::BeginPlay();
    SetActorTickEnabled(bStationEnabled);
    if (!bStationEnabled)
    {
        SetActorEnableCollision(false);
        SetActorHiddenInGame(true);
        return;
    }

    SetActorTickInterval(.05f);
    const FVector Front = Trigger->GetComponentLocation() + GetActorForwardVector() * (Trigger->GetScaledBoxExtent().X + 1.5f);
    GlyphText->SetWorldLocationAndRotation(Front,GetActorRotation());
    GlyphText->SetWorldSize(GlyphSize);
    GlyphText->SetTextRenderColor(FColor(200,245,246));
    GlyphText->SetVisibility(Mode != ELightStationMode::Memory);
}

void AShipLightStation::SetDisplay(const FString& Text)
{
    if (Text == LastDisplay) return;
    LastDisplay = Text;
    GlyphText->SetText(FText::FromString(Text));
}

int32 AShipLightStation::SecondsRemaining() const
{
    return FMath::Max(0, FMath::CeilToInt32(ActiveUntil - GetWorld()->GetTimeSeconds()));
}

void AShipLightStation::SetSignal(bool bOn)
{
    if (bInitializedSignal && bActive == bOn) return;
    const bool bChanged = bInitializedSignal;
    bInitializedSignal = true;
    bActive = bOn;
    const bool bAlarm = Mode == ELightStationMode::Alarm;
    SetIndicator(true, bAlarm ? (bOn ? Alert : Ready) : (bOn ? Ready : Idle));
    if (Mode != ELightStationMode::Dynamo && Mode != ELightStationMode::Memory)
        BroadcastDoors(bAlarm ? !bOn : bOn);
    if (bChanged && bOn) PlayFeedback(bAlarm ? .9f : .25f,1.f);
    else if (bChanged && bAlarm) PlayFeedback(.28f,1.32f);
}

bool AShipLightStation::AccumulateExposure(bool bExposed, float DeltaSeconds)
{
    Progress = bExposed ? FMath::Min(1.f, Progress + DeltaSeconds / FMath::Max(.1f, ExposureSeconds)) : 0.f;
    return Progress >= 1.f;
}

void AShipLightStation::ReadSequence(FName Incident, double Now)
{
    if (bSolved || ColorSequence.IsEmpty()) return;
    if (NextColor > 0 && Now >= ActiveUntil)
    {
        NextColor = 0;
        SetIndicator(true, Idle);
        PlayFeedback(.19f,.72f);
    }
    if (!Incident.IsNone() && Incident != LastIncidentColor)
    {
        const bool bCorrect = Incident == ColorSequence[NextColor];
        if (bCorrect) ++NextColor;
        else NextColor = Incident == ColorSequence[0] ? 1 : 0;
        ActiveUntil = Now + HoldSeconds;
        if (NextColor < ColorSequence.Num())
        {
            SetIndicator(true, bCorrect ? Ready : Alert);
            PlayFeedback(.23f,bCorrect ? 1.f + .12f*NextColor : .58f);
        }
    }
    LastIncidentColor = Incident;
    Progress = float(NextColor) / ColorSequence.Num();
    bSolved = NextColor == ColorSequence.Num();
    SetSignal(bSolved);
    const FString Counter = FString::Printf(TEXT("%d / %d"), NextColor, ColorSequence.Num());
    SetDisplay(NextColor && !bSolved ? FString::Printf(TEXT("%s · %d s"),*Counter,SecondsRemaining()) : Counter);
}

void AShipLightStation::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    AShipSurvivor* S = FindSurvivor();
    if (!bStationEnabled || !S || S->bDead || S->bEscaped) return;

    const FVector Eye = Trigger->GetComponentLocation();
    const bool bLit = S->BeamReachesTarget(Eye, this);
    const FName Incident = bLit ? S->Filter : NAME_None;
    const bool bMatches = bLit && Incident == PrimaryColor() && S->PowerLevel >= MinimumPower;
    const double Now = GetWorld()->GetTimeSeconds();
    switch (Mode)
    {
        case ELightStationMode::Latch:
            if (!bSolved) bSolved = AccumulateExposure(bMatches, DeltaSeconds);
            SetSignal(bSolved);
            SetDisplay(bSolved ? TEXT("OTWARTE") : FString::Printf(TEXT("%d%%"), FMath::RoundToInt(100.f * Progress)));
            break;
        case ELightStationMode::Shutter:
            if (bMatches) ActiveUntil = Now + HoldSeconds;
            SetSignal(Now < ActiveUntil);
            SetDisplay(!bActive ? TEXT("ZAMKNIETE") : bMatches ? TEXT("OTWARTE")
                : FString::Printf(TEXT("ZAMKNIE ZA %d s"),SecondsRemaining()));
            break;
        case ELightStationMode::Sequence:
            ReadSequence(Incident, Now);
            break;
        case ELightStationMode::Memory:
        {
            if (bMatches) ActiveUntil = Now + HoldSeconds;
            Progress = FMath::Clamp(float((ActiveUntil - Now) / FMath::Max(.1f, HoldSeconds)), 0.f, 1.f);
            SetSignal(Progress > 0.f);
            SetDisplay(Value.IsEmpty() ? TEXT("SLAD") : Value);
            GlyphText->SetVisibility(bActive);
            if (bActive)
                GlyphText->SetTextRenderColor((FLinearColor(.70f,.90f,1.f) * Progress).ToFColorSRGB());
            break;
        }
        case ELightStationMode::Dark:
        case ELightStationMode::Alarm:
        {
            const APawn* Pawn = UGameplayStatics::GetPlayerPawn(this, 0);
            const bool bNearby = Pawn && FVector::DistSquared(Pawn->GetActorLocation(), Eye) <= FMath::Square(ProximityRadius);
            if (Mode == ELightStationMode::Dark)
            {
                SetSignal(AccumulateExposure(bNearby && !bLit, DeltaSeconds));
                SetDisplay(!bNearby ? TEXT("CZUWANIE") : bActive ? TEXT("OTWARTE") : bLit ? TEXT("SWIATLO")
                    : FString::Printf(TEXT("ADAPTACJA %d%%"),FMath::RoundToInt(Progress*100.f)));
            }
            else
            {
                if (bNearby && ((bLit && S->PowerLevel >= MinimumPower) || Pawn->GetVelocity().SizeSquared2D() > FMath::Square(SprintThreshold)))
                    ActiveUntil = Now + HoldSeconds;
                SetSignal(Now < ActiveUntil);
                SetDisplay(bActive ? FString::Printf(TEXT("ALARM · %d s"),SecondsRemaining()) : TEXT("CISZA"));
            }
            break;
        }
        case ELightStationMode::Switch:
            if (bMatches) SetSignal(true);
            else if (bLit && Incident == ClosingColor() && S->PowerLevel >= MinimumPower) SetSignal(false);
            else SetSignal(bActive);
            SetDisplay(bActive ? TEXT("OTWARTE") : TEXT("ZAMKNIETE"));
            break;
        case ELightStationMode::Dynamo:
            if (bMatches) ActiveUntil = Now + HoldSeconds;
            SetSignal(Now < ActiveUntil);
            SetDisplay(bActive ? FString::Printf(TEXT("DYNAMO x4 · %d s"),SecondsRemaining()) : TEXT("DYNAMO x1"));
            break;
    }
}

FString AShipLightStation::Prompt(AShipSurvivor* S) const
{
    if (!bStationEnabled || !S || S->bDead || S->bEscaped) return {};
    FString Help;
    switch (Mode)
    {
        case ELightStationMode::Latch:
            Help = bSolved ? TEXT("E — zresetuj zamek") : FString::Printf(TEXT("Kolor %s, moc min. %d/5 · utrzymuj wiązkę · %d%%"), *ColorName(PrimaryColor()), MinimumPower, FMath::RoundToInt(100.f * Progress));
            break;
        case ELightStationMode::Shutter:
            Help = FString::Printf(TEXT("Kolor %s podtrzymuje otwarcie przegrody"), *ColorName(PrimaryColor()));
            if (bActive) Help += FString::Printf(TEXT(" · zapas %d s"),SecondsRemaining());
            break;
        case ELightStationMode::Sequence:
            Help = bSolved ? TEXT("E — powtórz sekwencję") : FString::Printf(TEXT("Pamięć kolorów · %d z %d"), NextColor, ColorSequence.Num());
            if (!bSolved && NextColor > 0)
                Help += FString::Printf(TEXT(" · pozostało %d s · E — zacznij od nowa"),SecondsRemaining());
            break;
        case ELightStationMode::Memory:
            Help = FString::Printf(TEXT("Naświetl napis · kolor %s"), *ColorName(PrimaryColor()));
            if (bActive) Help += FString::Printf(TEXT(" · widoczność %d s"),SecondsRemaining());
            break;
        case ELightStationMode::Dark:
            Help = TEXT("Podejdź i odsuń światło od czujnika");
            break;
        case ELightStationMode::Switch:
            Help = FString::Printf(TEXT("Otwórz (%s)  ·  Zamknij (%s)"), *ColorName(PrimaryColor()), *ColorName(ClosingColor()));
            break;
        case ELightStationMode::Alarm:
            Help = FString::Printf(TEXT("Przejdź powoli. Wiązka o mocy %d/5 lub większej uruchomi alarm"), MinimumPower);
            if (bActive) Help += FString::Printf(TEXT(" · do wyciszenia %d s"),SecondsRemaining());
            break;
        case ELightStationMode::Dynamo:
            if (!S->bHasBattery) Help = TEXT("Potrzebna bateria");
            else if (S->Charge >= ReserveLimit) Help = TEXT("Rezerwa pełna · limit dynama 35%");
            else Help = FString::Printf(TEXT("Przytrzymaj E · ładowanie %d%%/s do 35%%"), bActive ? 8 : 2);
            if (bActive) Help += FString::Printf(TEXT(" · premia %d s"),SecondsRemaining());
            break;
    }
    return Caption.IsEmpty() ? Help : Caption + TEXT(" · ") + Help;
}

void AShipLightStation::Use(AShipSurvivor* S)
{
    if (!bStationEnabled || !S || S->bDead || S->bEscaped) return;
    if ((Mode == ELightStationMode::Latch && bSolved) || (Mode == ELightStationMode::Sequence && NextColor > 0))
    {
        bSolved = false;
        Progress = 0.f;
        NextColor = 0;
        LastIncidentColor = S->bLampOn ? S->Filter : NAME_None;
        ActiveUntil = -1.;
        SetSignal(false);
        SetIndicator(true, Idle);
        PlayFeedback(.22f,.76f);
        S->Notify(Mode == ELightStationMode::Sequence ? TEXT("Pamięć wyczyszczona. Zacznij od pierwszego koloru.") : TEXT("Zatrzask zresetowany."));
    }
}

void AShipLightStation::HeldUse(AShipSurvivor* S, float DeltaSeconds)
{
    if (!bStationEnabled || Mode != ELightStationMode::Dynamo || !S || !S->bHasBattery || S->bDead || S->bEscaped || S->Charge >= ReserveLimit) return;
    const float Rate = bActive ? 8.f : 2.f;
    S->Charge = FMath::Min(ReserveLimit, S->Charge + Rate * FMath::Max(0.f, DeltaSeconds));
}
