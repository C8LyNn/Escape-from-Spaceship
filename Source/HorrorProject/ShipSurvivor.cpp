#include "ShipSurvivor.h"

#include "ShipFixture.h"
#include "ShipHUD.h"
#include "CollisionQueryParams.h"
#include "Components/AudioComponent.h"
#include "Components/InputComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SceneComponent.h"
#include "Components/SpotLightComponent.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "Framework/Application/IInputProcessor.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "InputCoreTypes.h"
#include "Kismet/GameplayStatics.h"
#include "PhysicsEngine/PhysicsHandleComponent.h"
#include "Sound/SoundBase.h"
#include "UObject/ConstructorHelpers.h"
#include "UObject/UnrealType.h"
#include "Widgets/SViewport.h"

namespace
{
    constexpr float UseRange = 260.f;
    constexpr int32 MiddleSetting = 3;
    constexpr int32 MaxSetting = 5;
    float PowerMultiplier(int32 Level) { return 1.f + .30f * (Level - MiddleSetting); }
    const FName Filters[] = {TEXT("Red"), TEXT("Green"), TEXT("Blue"), TEXT("UV")};
    const FKey Digits[] = {EKeys::Zero, EKeys::One, EKeys::Two, EKeys::Three, EKeys::Four, EKeys::Five, EKeys::Six, EKeys::Seven, EKeys::Eight, EKeys::Nine};
    const FKey KeypadDigits[] = {EKeys::NumPadZero, EKeys::NumPadOne, EKeys::NumPadTwo, EKeys::NumPadThree, EKeys::NumPadFour, EKeys::NumPadFive, EKeys::NumPadSix, EKeys::NumPadSeven, EKeys::NumPadEight, EKeys::NumPadNine};
    int32 DigitFor(const FKey& Key)
    {
        for (int32 Index = 0; Index < 10; ++Index)
            if (Key == Digits[Index] || Key == KeypadDigits[Index]) return Index;
        return INDEX_NONE;
    }

    class FShipModalShortcuts final : public IInputProcessor
    {
    public:
        explicit FShipModalShortcuts(AShipSurvivor* InOwner) : Owner(InOwner) {}
        void Tick(float, FSlateApplication&, TSharedRef<ICursor>) override {}
        bool HandleKeyDownEvent(FSlateApplication&, const FKeyEvent& Event) override
        {
            const FKey Key = Event.GetKey();
            if (Consumed.Contains(Key) && Event.IsRepeat()) return true;
            Consumed.Remove(Key);
            if (!Owner.IsValid() || !Owner->HandleModalShortcut(Key, Event.IsRepeat())) return false;
            Consumed.Add(Key);
            return true;
        }
        bool HandleKeyUpEvent(FSlateApplication&, const FKeyEvent& Event) override
        {
            return Consumed.Remove(Event.GetKey()) > 0;
        }
        const TCHAR* GetDebugName() const override { return TEXT("ShipModalShortcuts"); }
    private:
        TWeakObjectPtr<AShipSurvivor> Owner;
        TSet<FKey> Consumed;
    };
}

AShipSurvivor::AShipSurvivor()
{
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.bTickEvenWhenPaused = true;
    InputPriority = 90;
    RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Kit"));
    Beam = CreateDefaultSubobject<USpotLightComponent>(TEXT("HandheldBeam"));
    Beam->SetupAttachment(RootComponent);
    Beam->SetMobility(EComponentMobility::Movable);
    ApplyBeamSettings();
    Beam->SetCastShadows(true);
    Beam->SetVisibility(false);
    WelderAudio = CreateDefaultSubobject<UAudioComponent>(TEXT("WelderSparks"));
    WelderAudio->SetupAttachment(RootComponent);
    WelderAudio->bAutoActivate = false;
    WelderAudio->SetVolumeMultiplier(.35f);
    Grabber = CreateDefaultSubobject<UPhysicsHandleComponent>(TEXT("CrateHandle"));
    static ConstructorHelpers::FObjectFinder<USoundBase> Ping(TEXT("/Engine/EngineSounds/1kSineTonePing.1kSineTonePing"));
    static ConstructorHelpers::FObjectFinder<USoundBase> Sparks(TEXT("/Game/StarterContent/Audio/Fire_Sparks01.Fire_Sparks01"));
    FeedbackSound = Ping.Object;
    if (Sparks.Succeeded()) WelderAudio->SetSound(Sparks.Object);
}

void AShipSurvivor::BeginPlay()
{
    Super::BeginPlay();
    ApplyBeamSettings();
    BindToPlayer(UGameplayStatics::GetPlayerController(this, 0));
    if (FSlateApplication::IsInitialized())
    {
        ModalShortcuts = MakeShared<FShipModalShortcuts>(this);
        FSlateApplication::Get().RegisterInputPreProcessor(ModalShortcuts, EInputPreProcessorType::PreEngine);
    }
}

void AShipSurvivor::BindToPlayer(APlayerController* PC)
{
    if (!PC || !PC->IsLocalController()) return;
    if (LocalPC.Get() == PC) return;
    if (LocalPC.IsValid())
    {
        ClosePanel();
        SetKit(false);
        DisableInput(LocalPC.Get());
    }
    if (Overlay) Overlay->RemoveFromParent();
    LocalPC = PC;
    TrackedPawn.Reset();
    EnableInput(PC);
    ConfigureInput();
    SyncPawn();
    if (PC->GetLocalPlayer())
    {
        Overlay = CreateWidget<UShipHUDOverlay>(PC, UShipHUDOverlay::StaticClass());
        if (Overlay)
        {
            Overlay->SetSurvivor(this);
            Overlay->SetVisibility(ESlateVisibility::HitTestInvisible);
            Overlay->AddToPlayerScreen(35);
        }
    }
}

void AShipSurvivor::SyncPawn()
{
    APawn* Pawn = LocalPC.IsValid() ? LocalPC->GetPawn() : nullptr;
    if (TrackedPawn.Get() == Pawn) return;
    if (OriginalLight.IsValid() && bKitEnabled) OriginalLight->SetVisibility(bOriginalLightVisible);
    EndUse();
    EndDrag();
    ClosePanel();
    TrackedPawn = Pawn;
    OriginalLight.Reset();
    MovementStats.Reset();
    if (!Pawn) return;
    OriginalLight = Pawn->FindComponentByClass<USpotLightComponent>();
    if (OriginalLight.IsValid() && bKitEnabled)
    {
        bOriginalLightVisible = OriginalLight->IsVisible();
        OriginalLight->SetVisibility(false);
    }
    TInlineComponentArray<UActorComponent*> Components(Pawn);
    for (UActorComponent* Component : Components)
        if (Component->GetClass()->FindPropertyByName(TEXT("CurrentStamina"))) { MovementStats = Component; break; }
}

void AShipSurvivor::EndPlay(const EEndPlayReason::Type Reason)
{
    if (ModalShortcuts && FSlateApplication::IsInitialized()) FSlateApplication::Get().UnregisterInputPreProcessor(ModalShortcuts);
    ModalShortcuts.Reset();
    ClosePanel();
    EndUse();
    EndDrag();
    if (OriginalLight.IsValid() && bKitEnabled) OriginalLight->SetVisibility(bOriginalLightVisible);
    if (Overlay) Overlay->RemoveFromParent();
    if (LocalPC.IsValid()) DisableInput(LocalPC.Get());
    Super::EndPlay(Reason);
}

void AShipSurvivor::ConfigureInput()
{
    if (!InputComponent) return;
    InputComponent->KeyBindings.Reset();
    auto Bind = [this](FKey Key, EInputEvent Event, void (AShipSurvivor::*Callback)())
    {
        auto& Binding = InputComponent->BindKey(Key, Event, this, Callback);
        Binding.bExecuteWhenPaused = Key == EKeys::R;
    };
    Bind(EKeys::G, IE_Pressed, &AShipSurvivor::ToggleKit);
    Bind(EKeys::E, IE_Pressed, &AShipSurvivor::StartUse);
    Bind(EKeys::E, IE_Released, &AShipSurvivor::EndUse);
    Bind(EKeys::LeftMouseButton, IE_Pressed, &AShipSurvivor::StartDrag);
    Bind(EKeys::LeftMouseButton, IE_Released, &AShipSurvivor::EndDrag);
    Bind(EKeys::F, IE_Pressed, &AShipSurvivor::ToggleLamp);
    Bind(EKeys::Q, IE_Pressed, &AShipSurvivor::CycleFilter);
    Bind(EKeys::MouseScrollDown, IE_Pressed, &AShipSurvivor::PreviousFilter);
    for (const FKey& Key : {EKeys::Z, EKeys::X, EKeys::C, EKeys::V})
        InputComponent->BindKey(Key, IE_Pressed, this, &AShipSurvivor::AdjustLamp);
    Bind(EKeys::I, IE_Pressed, &AShipSurvivor::ToggleInventory);
    Bind(EKeys::Escape, IE_Pressed, &AShipSurvivor::ClosePanel);
    Bind(EKeys::Tab, IE_Pressed, &AShipSurvivor::ClosePanel);
    Bind(EKeys::BackSpace, IE_Pressed, &AShipSurvivor::DeleteDigit);
    Bind(EKeys::Enter, IE_Pressed, &AShipSurvivor::ConfirmCode);
    Bind(EKeys::Left, IE_Pressed, &AShipSurvivor::PriorNote);
    Bind(EKeys::Right, IE_Pressed, &AShipSurvivor::NextNote);
    Bind(EKeys::PageUp, IE_Pressed, &AShipSurvivor::PageUp);
    Bind(EKeys::PageDown, IE_Pressed, &AShipSurvivor::PageDown);
    Bind(EKeys::R, IE_Pressed, &AShipSurvivor::Restart);
    for (int32 Index = 0; Index < 10; ++Index)
    {
        InputComponent->BindKey(Digits[Index], IE_Pressed, this, &AShipSurvivor::PressDigit);
        InputComponent->BindKey(KeypadDigits[Index], IE_Pressed, this, &AShipSurvivor::PressDigit);
    }
    UpdateInputRouting();
}

void AShipSurvivor::UpdateInputRouting()
{
    if (!InputComponent) return;
    const bool bModal = IsKeypadOpen() || bInventoryOpen || bDead || bEscaped;
    const bool bYield = IsBaseMenuActive();
    for (auto& Binding : InputComponent->KeyBindings)
    {
        const FKey& K = Binding.Chord.Key;
        bool bOwn = false;
        if (K == EKeys::G) bOwn = true;
        else if (K == EKeys::E) bOwn = Focus.IsValid() || bOwnUse || bModal;
        else if (K == EKeys::LeftMouseButton) bOwn = FocusCrate.IsValid() || bOwnDrag || bModal;
        else if (K == EKeys::F || K == EKeys::Q || K == EKeys::I) bOwn = bKitEnabled;
        else if (K == EKeys::MouseScrollDown) bOwn = bKitEnabled && bHasColorLamp && !bModal;
        else if (K == EKeys::Z || K == EKeys::X || K == EKeys::C || K == EKeys::V) bOwn = CanAdjustLamp();
        else if (K == EKeys::R) bOwn = bDead || bEscaped;
        else if (K == EKeys::Left || K == EKeys::Right || K == EKeys::PageUp || K == EKeys::PageDown) bOwn = bInventoryOpen;
        else if (K == EKeys::Escape) bOwn = bModal;
        else if (K == EKeys::Tab) bOwn = IsKeypadOpen() || bInventoryOpen;
        else if (K == EKeys::Enter || K == EKeys::BackSpace) bOwn = IsKeypadOpen();
        else { const int32 N = DigitFor(K); bOwn = IsKeypadOpen() || (bKitEnabled && N >= 1 && N <= 3); }
        Binding.bConsumeInput = bOwn && !bYield;
    }
}

bool AShipSurvivor::IsBaseMenuActive() const
{
    return UGameplayStatics::IsGamePaused(this) || (LocalPC.IsValid() && LocalPC->bShowMouseCursor);
}

bool AShipSurvivor::HandleModalShortcut(FKey Key, bool bRepeat)
{
    if ((!IsKeypadOpen() && !bInventoryOpen) || IsBaseMenuActive()) return false;
    const bool bClose = Key == EKeys::Escape || Key == EKeys::Tab;
    const bool bSubmit = IsKeypadOpen() && Key == EKeys::Enter;
    if (!bClose && !bSubmit) return false;
    UGameViewportClient* Client = GetWorld() ? GetWorld()->GetGameViewport() : nullptr;
    const TSharedPtr<SViewport> View = Client ? Client->GetGameViewportWidget() : nullptr;
    if (!View || !View->HasAnyUserFocusOrFocusedDescendants()) return false;
    if (!bRepeat)
    {
        if (bClose) ClosePanel();
        else ConfirmCode();
    }
    return true;
}

FVector AShipSurvivor::ViewOrigin() const
{
    FVector Position = GetActorLocation();
    FRotator Rotation = GetActorRotation();
    if (LocalPC.IsValid()) LocalPC->GetPlayerViewPoint(Position, Rotation);
    return Position;
}
FVector AShipSurvivor::ViewDirection() const
{
    FVector Position;
    FRotator Rotation = GetActorRotation();
    if (LocalPC.IsValid()) LocalPC->GetPlayerViewPoint(Position, Rotation);
    return Rotation.Vector();
}

void AShipSurvivor::RefreshFocus()
{
    Focus.Reset();
    FocusCrate.Reset();
    if (!TrackedPawn.IsValid() || bDead || bEscaped || IsKeypadOpen() || bInventoryOpen) return;
    FHitResult Hit;
    FCollisionQueryParams Params(SCENE_QUERY_STAT(ShipInteraction), false, TrackedPawn.Get());
    Params.AddIgnoredActor(this);
    const FVector Start = ViewOrigin();
    if (!GetWorld()->LineTraceSingleByChannel(Hit, Start, Start + ViewDirection() * UseRange, ECC_Visibility, Params)) return;
    Focus = Cast<AShipFixture>(Hit.GetActor());
    FocusHit = Hit.ImpactPoint;
    if (Hit.GetActor() && Hit.GetActor()->ActorHasTag(TEXT("PushCrate")) && Hit.GetComponent() && Hit.GetComponent()->IsSimulatingPhysics())
        FocusCrate = Hit.GetComponent();
}

bool AShipSurvivor::BeamReaches(const FVector& Point) const
{
    return BeamReachesTarget(Point, nullptr);
}

bool AShipSurvivor::BeamReachesTarget(const FVector& Point, const AActor* Receiver) const
{
    if (!bKitEnabled || !bLampOn || !bHasBattery || Charge <= 0.f || bDead || bEscaped || !GetWorld() || !TrackedPawn.IsValid()) return false;
    const FVector Start = ViewOrigin();
    const FVector Delta = Point - Start;
    const float Distance = Delta.Size();
    if (Distance < 1.f || Distance > Beam->AttenuationRadius || FVector::DotProduct(Delta / Distance, ViewDirection()) < FMath::Cos(FMath::DegreesToRadians(Beam->OuterConeAngle))) return false;
    FHitResult Hit;
    FCollisionQueryParams Params(SCENE_QUERY_STAT(ShipSpectrum), false, TrackedPawn.Get());
    Params.AddIgnoredActor(this);
    if (!GetWorld()->LineTraceSingleByChannel(Hit, Start, Point, ECC_Visibility, Params)) return true;
    // The receiving ink has no collision. Permit only numerical contact at the endpoint.
    return (Receiver && Hit.GetActor() == Receiver) || FVector::DistSquared(Hit.ImpactPoint, Point) <= FMath::Square(.5f);
}

void AShipSurvivor::StartUse()
{
    if (IsBaseMenuActive() || bInventoryOpen || IsKeypadOpen() || bDead || bEscaped) return;
    RefreshFocus();
    AShipFixture* Fixture = Focus.Get();
    if (!Fixture || IsDragging() || bOwnUse) return;
    SetKit(true);
    bOwnUse = true;
    HeldFixture = Fixture;
    Fixture->Use(this);
}
void AShipSurvivor::EndUse()
{
    AShipFixture* Previous = HeldFixture.Get();
    HeldFixture.Reset();
    bOwnUse = false;
    if (IsValid(Previous)) Previous->StopUse(this);
    SetWelding(false);
}

bool AShipSurvivor::IsDragging() const { return Grabber->GetGrabbedComponent() != nullptr; }
void AShipSurvivor::StartDrag()
{
    if (IsBaseMenuActive() || bInventoryOpen || IsKeypadOpen() || bDead || bEscaped) return;
    RefreshFocus();
    UPrimitiveComponent* Crate = FocusCrate.Get();
    if (!Crate || !TrackedPawn.IsValid()) return;
    SetKit(true);
    EndUse();
    GrabDistance = FMath::Clamp(float((FocusHit - ViewOrigin()).Size2D()), 78.f, UseRange);
    GrabHeight = FocusHit.Z - TrackedPawn->GetActorLocation().Z;
    Grabber->GrabComponentAtLocation(Crate, NAME_None, FocusHit);
    bOwnDrag = IsDragging();
}
void AShipSurvivor::EndDrag()
{
    if (IsDragging()) Grabber->ReleaseComponent();
    bOwnDrag = false;
}
void AShipSurvivor::UpdateDrag()
{
    UPrimitiveComponent* Crate = Grabber->GetGrabbedComponent();
    if (!Crate || !TrackedPawn.IsValid()) { EndDrag(); return; }
    const FVector Eye = ViewOrigin();
    if (FVector::DistSquared(Crate->Bounds.Origin, Eye) > FMath::Square(UseRange + Crate->Bounds.SphereRadius + 75.f)) { EndDrag(); return; }
    FVector Target = Eye + ViewDirection().GetSafeNormal2D() * GrabDistance;
    Target.Z = TrackedPawn->GetActorLocation().Z + GrabHeight;
    FCollisionQueryParams Params(SCENE_QUERY_STAT(ShipCrate), false, TrackedPawn.Get());
    Params.AddIgnoredActor(this);
    Params.AddIgnoredActor(Crate->GetOwner());
    FHitResult Wall;
    if (GetWorld()->SweepSingleByChannel(Wall, Eye, Target, FQuat::Identity, ECC_Visibility, FCollisionShape::MakeSphere(7.f), Params))
        Target = Wall.Location;
    Grabber->SetTargetLocation(Target);
}

void AShipSurvivor::SetKit(bool bEnable)
{
    if (bKitEnabled == bEnable) return;
    bKitEnabled = bEnable;
    if (OriginalLight.IsValid())
    {
        if (bEnable) bOriginalLightVisible = OriginalLight->IsVisible();
        OriginalLight->SetVisibility(bEnable ? false : bOriginalLightVisible);
    }
    if (!bEnable) { ClosePanel(); EndUse(); EndDrag(); bLampOn = false; }
    UpdateBeam();
    UpdateInputRouting();
}
void AShipSurvivor::ToggleKit()
{
    if (IsBaseMenuActive()) return;
    if (bDead) { Restart(); return; }
    if (bEscaped) { bEscaped = false; LockControls(false); }
    SetKit(!bKitEnabled);
    if (bKitEnabled) Notify(TEXT("Zestaw testowy aktywny. I otwiera wyposażenie i dziennik."));
}
void AShipSurvivor::SetTool(const FString& Tool)
{
    const bool bHave = Tool == TEXT("White") ? bHasWhite : Tool == TEXT("ColorLamp") ? bHasColorLamp : bHasWelder;
    if (!bHave) { Notify(TEXT("Najpierw podnieś to narzędzie na stanowisku.")); return; }
    if (Equipped != Tool) EndUse();
    Equipped = Tool;
    ApplyBeamSettings();
    if (Tool == TEXT("Welder")) bLampOn = false;
    UpdateBeam();
    Beep(.85f);
}
void AShipSurvivor::ToggleLamp()
{
    if (IsBaseMenuActive() || !bKitEnabled || bInventoryOpen || IsKeypadOpen() || bDead || bEscaped) return;
    if (Equipped == TEXT("Welder")) { Notify(TEXT("Przyłóż spawarkę do stanowiska i przytrzymaj E.")); return; }
    if (Equipped == TEXT("ColorLamp") ? !bHasColorLamp : !bHasWhite) { Notify(TEXT("Najpierw podnieś odpowiednią latarkę.")); return; }
    if (!bHasBattery || Charge <= 0.f) { Notify(bHasBattery ? TEXT("Bateria jest pusta. Naładuj ją w kajucie.") : TEXT("Brak baterii. Poszukaj jej na stanowisku.")); return; }
    bLampOn = !bLampOn;
    UpdateBeam();
    Beep(bLampOn ? 1.12f : .72f);
}
void AShipSurvivor::CycleFilter() { ChangeFilter(1); }
void AShipSurvivor::PreviousFilter() { ChangeFilter(-1); }
void AShipSurvivor::ChangeFilter(int32 Step)
{
    if (IsBaseMenuActive() || !bKitEnabled || bInventoryOpen || IsKeypadOpen() || bDead || bEscaped) return;
    if (!bHasColorLamp) { if (Step > 0) Notify(TEXT("Podnieś latarkę z kolorami, aby zmienić kolor.")); return; }
    if (Equipped != TEXT("ColorLamp")) { SetTool(TEXT("ColorLamp")); return; }
    int32 Index = 0;
    while (Index < 4 && Filters[Index] != LastFilter) ++Index;
    LastFilter = Filters[(Index + Step + 4) % 4];
    UpdateBeam();
    Beep(.9f + .08f * ((Index + Step + 4) % 4));
}
bool AShipSurvivor::CanAdjustLamp() const
{
    return bKitEnabled && !bInventoryOpen && !IsKeypadOpen() && !bDead && !bEscaped && !IsBaseMenuActive()
        && ((Equipped == TEXT("White") && bHasWhite) || (Equipped == TEXT("ColorLamp") && bHasColorLamp));
}
void AShipSurvivor::AdjustLamp(FKey Key)
{
    if (!CanAdjustLamp()) return;
    const bool bPower = Key == EKeys::Z || Key == EKeys::X;
    const int32 Step = Key == EKeys::X || Key == EKeys::V ? 1 : -1;
    int32& Level = bPower ? PowerLevel : RadiusLevel;
    const int32 NewLevel = FMath::Clamp(Level + Step, 1, MaxSetting);
    if (NewLevel == Level) return;
    Level = NewLevel;
    ApplyBeamSettings();
}
void AShipSurvivor::ApplyBeamSettings()
{
    const bool bLamp = Equipped != TEXT("Welder");
    const int32 RadiusOffset = bLamp ? RadiusLevel - MiddleSetting : 0;
    const float HalfAngle = 24.f + 6.f * RadiusOffset;
    Beam->SetIntensity(3800.f * (bLamp ? PowerMultiplier(PowerLevel) : 1.f));
    Beam->SetAttenuationRadius(1600.f + 300.f * RadiusOffset);
    Beam->SetInnerConeAngle(HalfAngle * .5f);
    Beam->SetOuterConeAngle(HalfAngle);
}
float AShipSurvivor::GetLampDrainPerSecond() const
{
    const float RadiusMultiplier = 1.f + .20f * (RadiusLevel - MiddleSetting);
    return .65f * PowerMultiplier(PowerLevel) * RadiusMultiplier;
}
FLinearColor AShipSurvivor::GetFilterColor() const
{
    if (Filter == TEXT("Red")) return FLinearColor(1.f,.016f,.008f);
    if (Filter == TEXT("Green")) return FLinearColor(.025f,1.f,.055f);
    if (Filter == TEXT("Blue")) return FLinearColor(.025f,.12f,1.f);
    if (Filter == TEXT("UV")) return FLinearColor(.43f,.035f,1.f);
    return FLinearColor(1.f,.94f,.83f);
}
FString AShipSurvivor::GetFilterLabel() const
{
    if (Filter == TEXT("Red")) return TEXT("czerwony");
    if (Filter == TEXT("Green")) return TEXT("zielony");
    if (Filter == TEXT("Blue")) return TEXT("niebieski");
    if (Filter == TEXT("UV")) return TEXT("fioletowy (UV)");
    return TEXT("biały");
}
FString AShipSurvivor::GetToolLabel() const
{
    if (Equipped == TEXT("Welder")) return bHasWelder ? TEXT("Spawarka") : TEXT("Brak narzędzia");
    if (Equipped == TEXT("ColorLamp")) return bHasColorLamp ? TEXT("Latarka z kolorami") : TEXT("Brak narzędzia");
    return bHasWhite ? TEXT("Latarka biała") : TEXT("Brak narzędzia");
}
void AShipSurvivor::UpdateBeam()
{
    Filter = Equipped == TEXT("ColorLamp") ? LastFilter : FName(TEXT("White"));
    if (!bKitEnabled || !bHasBattery || Charge <= 0.f || bDead || bEscaped) { bWelding = false; bLampOn = false; }
    if (Equipped == TEXT("Welder") || (Equipped == TEXT("White") && !bHasWhite) || (Equipped == TEXT("ColorLamp") && !bHasColorLamp)) bLampOn = false;
    const bool bVisible = bLampOn || bWelding;
    if (Beam->IsVisible() != bVisible) Beam->SetVisibility(bVisible);
    if (bVisible)
    {
        if (TrackedPawn.IsValid()) Beam->SetWorldLocationAndRotation(ViewOrigin(), ViewDirection().Rotation());
        const FLinearColor Color = bWelding ? FLinearColor(.40f,.74f,1.f) : GetFilterColor();
        if (!Beam->GetLightColor().Equals(Color,.012f)) Beam->SetLightColor(Color);
    }
    if (bWelding && WelderAudio->Sound && !WelderAudio->IsPlaying()) WelderAudio->Play();
    if (!bWelding && WelderAudio->IsPlaying()) WelderAudio->Stop();
}
void AShipSurvivor::SetWelding(bool bActive)
{
    const bool bNew = bActive && bKitEnabled && bHasWelder && Equipped == TEXT("Welder") && bHasBattery && Charge > 0.f && !bDead && !bEscaped;
    if (bWelding == bNew) return;
    bWelding = bNew;
    if (bWelding) { bLampOn = false; Beep(.65f); }
    UpdateBeam();
}
void AShipSurvivor::Beep(float Pitch) const
{
    if (FeedbackSound) UGameplayStatics::PlaySound2D(this, FeedbackSound, .075f, Pitch);
}
void AShipSurvivor::Notify(const FString& Message)
{
    if (Message.IsEmpty()) return;
    if (GetNotice() != Message) Beep(1.2f);
    Notice = Message;
    NoticeUntil = GetWorld()->GetRealTimeSeconds() + 4.;
}
FString AShipSurvivor::GetNotice() const
{
    return GetWorld() && GetWorld()->GetRealTimeSeconds() < NoticeUntil ? Notice : FString();
}
FString AShipSurvivor::GetPrompt() const
{
    if (IsDragging()) return TEXT("Puść LPM, aby odstawić skrzynię.");
    if (FocusCrate.IsValid()) return TEXT("Przytrzymaj LPM i porusz się, aby przesunąć skrzynię.");
    if (const AShipFixture* Fixture = Focus.Get()) return Fixture->Prompt(const_cast<AShipSurvivor*>(this));
    return {};
}
void AShipSurvivor::LockControls(bool bLock)
{
    if (bLock == bLockedControls) return;
    bLockedControls = bLock;
    if (LocalPC.IsValid())
    {
        LocalPC->SetIgnoreMoveInput(bLock);
        LocalPC->SetIgnoreLookInput(bLock);
    }
}
void AShipSurvivor::ClosePanel()
{
    Keypad.Reset(); EnteredCode.Reset(); bInventoryOpen = false;
    LockControls(bDead || bEscaped);
    UpdateInputRouting();
}
void AShipSurvivor::OpenKeypad(AShipFixture* Fixture)
{
    if (!IsValid(Fixture) || bDead || bEscaped) return;
    SetKit(true); EndUse(); EndDrag();
    bInventoryOpen = false; Keypad = Fixture; EnteredCode.Reset();
    LockControls(true); UpdateInputRouting();
}
void AShipSurvivor::PressDigit(FKey Key)
{
    const int32 Digit = DigitFor(Key);
    if (IsBaseMenuActive() || Digit == INDEX_NONE || bDead || bEscaped) return;
    if (IsKeypadOpen())
    {
        if (EnteredCode.Len() < 12) { EnteredCode.AppendChar(TCHAR('0' + Digit)); Beep(1.f + .035f * Digit); }
        return;
    }
    if (!bKitEnabled || bInventoryOpen) return;
    if (Digit == 1) SetTool(TEXT("White"));
    if (Digit == 2) SetTool(TEXT("ColorLamp"));
    if (Digit == 3) SetTool(TEXT("Welder"));
}
void AShipSurvivor::DeleteDigit()
{
    if (IsKeypadOpen() && !EnteredCode.IsEmpty()) { EnteredCode.LeftChopInline(1); Beep(.8f); }
}
void AShipSurvivor::ConfirmCode()
{
    AShipFixture* Fixture = Keypad.Get();
    if (!Fixture) return;
    if (EnteredCode.IsEmpty()) { Notify(TEXT("Wpisz kod przed zatwierdzeniem.")); return; }
    const FString Code = EnteredCode;
    ClosePanel();
    Fixture->SubmitCode(this, Code);
}
void AShipSurvivor::ReadNote(const FString& Text)
{
    if (Text.IsEmpty() || bDead || bEscaped) return;
    SetKit(true); EndUse(); EndDrag();
    LastNote = Text; SelectedNote = Notes.AddUnique(Text); ScrollLines = 0;
    Keypad.Reset(); bInventoryOpen = true;
    LockControls(true); UpdateInputRouting();
}
void AShipSurvivor::ToggleInventory()
{
    if (IsBaseMenuActive() || !bKitEnabled || IsKeypadOpen() || bDead || bEscaped) return;
    if (bInventoryOpen) { ClosePanel(); return; }
    EndUse(); EndDrag(); bInventoryOpen = true; LockControls(true); UpdateInputRouting();
}
void AShipSurvivor::PriorNote() { if (bInventoryOpen && !Notes.IsEmpty()) { SelectedNote = (SelectedNote + Notes.Num() - 1) % Notes.Num(); ScrollLines = 0; } }
void AShipSurvivor::NextNote() { if (bInventoryOpen && !Notes.IsEmpty()) { SelectedNote = (SelectedNote + 1) % Notes.Num(); ScrollLines = 0; } }
void AShipSurvivor::PageUp() { if (bInventoryOpen) ScrollLines = FMath::Max(0, ScrollLines - 5); }
void AShipSurvivor::PageDown() { if (bInventoryOpen) ScrollLines += 5; }
void AShipSurvivor::ClampNoteScroll(int32 Maximum) { ScrollLines = FMath::Clamp(ScrollLines,0,FMath::Max(0,Maximum)); }

void AShipSurvivor::Die()
{
    if (bDead || bEscaped) return;
    SetKit(true); bDead = true; ClosePanel(); EndUse(); EndDrag(); UpdateBeam(); Beep(.48f);
}
void AShipSurvivor::Escape()
{
    if (bDead || bEscaped) return;
    SetKit(true); bEscaped = true; ClosePanel(); EndUse(); EndDrag(); UpdateBeam(); Beep(1.5f);
}
void AShipSurvivor::Restart()
{
    if (!bDead && !bEscaped) return;
    const FString Name = UGameplayStatics::GetCurrentLevelName(this, true);
    if (!Name.IsEmpty()) UGameplayStatics::OpenLevel(this, FName(*Name));
}

void AShipSurvivor::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    BindToPlayer(UGameplayStatics::GetPlayerController(this, 0));
    if (!LocalPC.IsValid()) return;
    SyncPawn();
    if (IsBaseMenuActive())
    {
        Focus.Reset(); FocusCrate.Reset();
        if (bOwnUse) EndUse();
        if (IsDragging()) EndDrag();
        UpdateInputRouting();
        return;
    }
    if (bLockedControls && !bInventoryOpen && !IsKeypadOpen() && !bEscaped && !bDead) LockControls(false);
    RefreshFocus();
    if (bOwnUse)
    {
        if (!HeldFixture.IsValid() || HeldFixture != Focus || !LocalPC->IsInputKeyDown(EKeys::E)) EndUse();
        else HeldFixture->HeldUse(this, DeltaSeconds);
    }
    if (IsDragging())
    {
        if (LocalPC->IsInputKeyDown(EKeys::LeftMouseButton)) UpdateDrag();
        else EndDrag();
    }
    if (bKitEnabled)
    {
        if (OriginalLight.IsValid() && OriginalLight->IsVisible()) OriginalLight->SetVisibility(false);
        if (UActorComponent* Stats = MovementStats.Get())
            if (const FNumericProperty* Property = FindFProperty<FNumericProperty>(Stats->GetClass(),TEXT("CurrentStamina")))
                if (Property->IsFloatingPoint()) Stamina = Property->GetFloatingPointPropertyValue(Property->ContainerPtrToValuePtr<void>(Stats));
        const float Drain = bLampOn ? GetLampDrainPerSecond() : 0.f;
        Charge = FMath::Clamp(Charge - Drain * DeltaSeconds, 0.f, 100.f);
        if (Drain > 0.f && Charge <= 0.f) Notify(TEXT("Bateria rozładowana. Wróć do ładowarki w kajucie."));
        UpdateBeam();
    }
    UpdateInputRouting();
}
