#include "ShipSurvivor.h"

#include "ShipFixture.h"
#include "ShipInventoryLibrary.h"
#include "ShipBackpackGrid.h"
#include "Components/StaticMeshComponent.h"
#include "ShipHUD.h"
#include "ShipLadder.h"
#include "Blueprint/UserWidget.h"
#include "Components/CapsuleComponent.h"
#include "Engine/OverlapResult.h"
#include "GameFramework/Character.h"
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
#include "Materials/MaterialParameterCollection.h"
#include "Materials/MaterialParameterCollectionInstance.h"
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
    const FName Filters[] = {TEXT("Red"), TEXT("Green"), TEXT("Blue"), TEXT("UV"), TEXT("White")};
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
        bool HandleMouseWheelOrGestureEvent(FSlateApplication&, const FPointerEvent& Event, const FPointerEvent*) override
        {
            if (!Owner.IsValid() || !Owner->IsStockInventoryVisible() || Owner->BackpackPageCount() <= 1 || Event.GetWheelDelta() == 0) return false;
            UGameViewportClient* Client = Owner->GetWorld()->GetGameViewport();
            const TSharedPtr<SViewport> View = Client ? Client->GetGameViewportWidget() : nullptr;
            if (!View || !View->GetCachedGeometry().IsUnderLocation(Event.GetScreenSpacePosition())) return false;
            return Owner->HandleModalShortcut(Event.GetWheelDelta() > 0 ? EKeys::PageUp : EKeys::PageDown, false);
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
    static ConstructorHelpers::FObjectFinder<UClass> InteractionClass(TEXT("/Game/Blueprints/Interfaces/BPI_Interaction.BPI_Interaction_C"));
    static ConstructorHelpers::FObjectFinder<UClass> GrabClass(TEXT("/Game/Blueprints/Interfaces/BPI_Grab.BPI_Grab_C"));
    StockInteraction = InteractionClass.Object;
    StockGrab = GrabClass.Object;
    static ConstructorHelpers::FObjectFinder<UMaterialParameterCollection> Params(TEXT("/Game/ShipMechanics/Filters/MPC_HandheldBeam.MPC_HandheldBeam"));
    BeamParameters = Params.Object;
    RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Kit"));
    InitPack();
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
    LeaveLadder();
    if (bKitEnabled) SetStockLightEnabled(bOriginalLightVisible);
    EndUse();
    EndDrag();
    ClosePanel();
    TrackedPawn = Pawn;
    BackpackPage = 0;
    BackpackCapacity = FMath::Max(ShipBackpackGrid::PageSize, UShipInventoryLibrary::SlotCount(Pawn));
    if (Pawn) ShipBackpackGrid::ShowPage(LocalPC.Get(), BackpackPage);
    UpdateHandMesh();
    OriginalLight.Reset();
    StockPower.Reset();
    MovementStats.Reset();
    if (!Pawn) return;
    OriginalLight = Pawn->FindComponentByClass<USpotLightComponent>();
    if (OriginalLight.IsValid() && bKitEnabled)
    {
        bOriginalLightVisible = OriginalLight->IsVisible();
        SetStockLightEnabled(false);
    }
    TInlineComponentArray<UActorComponent*> Components(Pawn);
    for (UActorComponent* Component : Components)
    {
        if (Component->GetClass()->FindPropertyByName(TEXT("CurrentStamina"))) MovementStats = Component;
        if (Component->GetClass()->FindPropertyByName(TEXT("CurrentBatteryLevel"))) StockPower = Component;
    }
    ApplyBeamSettings();
}

void AShipSurvivor::EndPlay(const EEndPlayReason::Type Reason)
{
    if (ModalShortcuts && FSlateApplication::IsInitialized()) FSlateApplication::Get().UnregisterInputPreProcessor(ModalShortcuts);
    ModalShortcuts.Reset();
    LeaveLadder();
    ClosePanel();
    EndUse();
    EndDrag();
    if (bKitEnabled) SetStockLightEnabled(bOriginalLightVisible);
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
    Bind(EKeys::E, IE_Released, &AShipSurvivor::ReleaseInteraction);
    Bind(EKeys::SpaceBar, IE_Pressed, &AShipSurvivor::JumpFromLadder);
    Bind(EKeys::LeftMouseButton, IE_Pressed, &AShipSurvivor::StartDrag);
    Bind(EKeys::LeftMouseButton, IE_Released, &AShipSurvivor::ReleaseMouseDrag);
    Bind(EKeys::F, IE_Pressed, &AShipSurvivor::ToggleLamp);
    Bind(EKeys::Q, IE_Pressed, &AShipSurvivor::CycleFilter);
    for (const FKey& Key : {EKeys::MouseScrollDown, EKeys::MouseScrollUp})
        InputComponent->BindKey(Key, IE_Pressed, this, &AShipSurvivor::ScrollEquipment);
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
    const bool bModal = IsKeypadOpen() || bReadingNote || bDead || bEscaped;
    const bool bYield = IsBaseMenuActive();
    for (auto& Binding : InputComponent->KeyBindings)
    {
        const FKey& K = Binding.Chord.Key;
        bool bOwn = false;
        if (K == EKeys::G) bOwn = true;
        else if (K == EKeys::E) bOwn = Focus.IsValid() || FocusCrate.IsValid() || CrateGrabKey == EKeys::E
            || HeldStockObject.IsValid() || (StockTarget.IsValid() && StockGrab && StockTarget->GetClass()->ImplementsInterface(StockGrab)
                && !StockTarget->GetClass()->ImplementsInterface(StockInteraction))
            || bOwnUse || bModal || IsOnLadder() || bLadderUseDown;
        else if (K == EKeys::SpaceBar) bOwn = IsOnLadder() || bModal;
        else if (K == EKeys::LeftMouseButton) bOwn = FocusCrate.IsValid() || bOwnDrag || bModal
            || Equipped == TEXT("Glowstick") || Equipped == TEXT("Note") || Equipped == TEXT("Bandage") || Equipped == TEXT("FlashlightBattery");
        else if (K == EKeys::F || K == EKeys::Q || K == EKeys::I) bOwn = bKitEnabled;
        else if (K == EKeys::MouseScrollDown || K == EKeys::MouseScrollUp) bOwn = !bModal && (IsDragging() || Equipped == TEXT("ColorLamp"));
        else if (K == EKeys::Z || K == EKeys::X || K == EKeys::C || K == EKeys::V) bOwn = CanAdjustLamp();
        else if (K == EKeys::R) bOwn = bDead || bEscaped;
        else if (K == EKeys::Left || K == EKeys::Right || K == EKeys::PageUp || K == EKeys::PageDown) bOwn = bReadingNote;
        else if (K == EKeys::Escape) bOwn = bModal;
        else if (K == EKeys::Tab) bOwn = bModal;
        else if (K == EKeys::Enter || K == EKeys::BackSpace) bOwn = IsKeypadOpen();
        else { const int32 N = DigitFor(K); bOwn = IsKeypadOpen() || (N >= 1 && N <= 8); }
        Binding.bConsumeInput = bOwn && !bYield;
    }
}

bool AShipSurvivor::IsBaseMenuActive() const
{
    return UGameplayStatics::IsGamePaused(this) || (LocalPC.IsValid() && LocalPC->bShowMouseCursor);
}

bool AShipSurvivor::HandleModalShortcut(FKey Key, bool bRepeat)
{
    if (IsStockInventoryVisible())
    {
        const int32 Digit = DigitFor(Key);
        UGameViewportClient* Client = GetWorld()->GetGameViewport();
        const TSharedPtr<SViewport> View = Client ? Client->GetGameViewportWidget() : nullptr;
        if (!View || !View->HasAnyUserFocusOrFocusedDescendants()) return false;
        if (Key == EKeys::PageUp || Key == EKeys::PageDown)
        {
            if (BackpackPageCount() == 1) return false;
            if (!bRepeat) ChangeBackpackPage(Key == EKeys::PageUp ? -1 : 1);
            return true;
        }
        if (Digit < 1 || Digit > ShipBackpackGrid::PageSize) return false;
        if (!bRepeat) EquipSlot(QuickSlotIndex(Digit));
        return true;
    }
    if ((!IsKeypadOpen() && !bReadingNote) || IsBaseMenuActive()) return false;
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
    StockTarget.Reset();
    if (!TrackedPawn.IsValid() || IsOnLadder() || bDead || bEscaped || IsKeypadOpen() || bReadingNote) return;
    FHitResult Hit;
    FCollisionQueryParams Params(SCENE_QUERY_STAT(ShipInteraction), false, TrackedPawn.Get());
    Params.AddIgnoredActor(this);
    const FVector Start = ViewOrigin();
    constexpr float StockUseRange = 350.f; // L1_Character uses this distance for Action and LMB.
    if (GetWorld()->LineTraceSingleByChannel(Hit, Start, Start + ViewDirection() * StockUseRange, ECC_Visibility, Params))
    {
        AActor* Actor = Hit.GetActor();
        if (Actor && ((StockInteraction && Actor->GetClass()->ImplementsInterface(StockInteraction))
            || (StockGrab && Actor->GetClass()->ImplementsInterface(StockGrab))))
        {
            StockTarget = Actor;
            return;
        }
        if (Hit.Distance <= UseRange)
        {
            Focus = Cast<AShipFixture>(Actor);
            FocusHit = Hit.ImpactPoint;
            if (Actor && Actor->ActorHasTag(TEXT("PushCrate")) && Hit.GetComponent() && Hit.GetComponent()->IsSimulatingPhysics())
                FocusCrate = Hit.GetComponent();
        }
    }
    if (Focus.IsValid() || FocusCrate.IsValid()) return;
    // From the upper landing, the deck can hide the rungs from the player's view.
    const ACharacter* Character = Cast<ACharacter>(TrackedPawn.Get());
    if (!Character) return;
    const FVector Feet = Character->GetActorLocation() - FVector::UpVector * Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
    TArray<FOverlapResult> Nearby;
    GetWorld()->OverlapMultiByObjectType(Nearby, Feet, FQuat::Identity, FCollisionObjectQueryParams(ECC_WorldDynamic),
        FCollisionShape::MakeSphere(105.f), Params);
    for (const FOverlapResult& Item : Nearby)
        if (AShipLadder* Ladder = Cast<AShipLadder>(Item.GetActor()); Ladder && Ladder->IsAtLanding(Character))
        {
            Focus = Ladder;
            break;
        }
}

bool AShipSurvivor::BeamReaches(const FVector& Point) const
{
    return BeamReachesTarget(Point, nullptr);
}

bool AShipSurvivor::BeamReachesTarget(const FVector& Point, const AActor* Receiver) const
{
    if (!bKitEnabled || !bLampOn || Equipped != TEXT("ColorLamp") || PuzzleCharge <= 0.f || bDead || bEscaped || !GetWorld() || !HasPlayerView()) return false;
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
    if (IsBaseMenuActive() || bReadingNote || IsKeypadOpen() || bDead || bEscaped || bLadderUseDown) return;
    if (IsOnLadder())
    {
        bLadderUseDown = true;
        LeaveLadder();
        return;
    }
    RefreshFocus();
    if (IsDragging() || bOwnUse || HeldStockObject.IsValid()) return;
    if (FocusCrate.IsValid()) { GrabFocusedCrate(EKeys::E); return; }
    if (AActor* Actor = StockTarget.Get())
    {
        // Stock Action already dispatches Interact. Only the grab-only interface needs an E alias.
        if (StockGrab && Actor->GetClass()->ImplementsInterface(StockGrab) && !Actor->GetClass()->ImplementsInterface(StockInteraction))
            if (UFunction* Grab = Actor->FindFunction(TEXT("GrabObject")); Grab && Grab->NumParms == 0)
            {
                HeldStockObject = Actor;
                Actor->ProcessEvent(Grab, nullptr);
            }
        return;
    }
    AShipFixture* Fixture = Focus.Get();
    if (!Fixture) return;
    if (AShipLadder* Ladder = Cast<AShipLadder>(Fixture))
    {
        if (Ladder->TryMount(Cast<ACharacter>(TrackedPawn.Get())))
        {
            MountedLadder = Ladder;
            bLadderUseDown = true;
            UpdateInputRouting();
        }
        return;
    }
    bOwnUse = true;
    HeldFixture = Fixture;
    Fixture->Use(this);
}
void AShipSurvivor::EndUse()
{
    if (AActor* Actor = HeldStockObject.Get())
    {
        HeldStockObject.Reset();
        if (UFunction* Release = Actor->FindFunction(TEXT("ReleaseObject")); Release && Release->NumParms == 0)
            Actor->ProcessEvent(Release, nullptr);
    }
    AShipFixture* Previous = HeldFixture.Get();
    HeldFixture.Reset();
    bOwnUse = false;
    if (IsValid(Previous)) Previous->StopUse(this);
    SetWelding(false);
}

void AShipSurvivor::ReleaseInteraction()
{
    bLadderUseDown = false;
    if (CrateGrabKey == EKeys::E) EndDrag();
    EndUse();
}
bool AShipSurvivor::IsOnLadder() const { return MountedLadder.IsValid() && MountedLadder->HasRider(); }
void AShipSurvivor::LeaveLadder()
{
    if (MountedLadder.IsValid()) MountedLadder->Release();
    MountedLadder.Reset();
}
void AShipSurvivor::JumpFromLadder()
{
    if (IsOnLadder() && !IsBaseMenuActive() && !bReadingNote && !IsKeypadOpen() && !bDead && !bEscaped)
    {
        MountedLadder->Release(true);
        MountedLadder.Reset();
    }
}
bool AShipSurvivor::IsDragging() const { return Grabber->GetGrabbedComponent() != nullptr; }
void AShipSurvivor::StartDrag()
{
    if (IsDragging() || IsBaseMenuActive() || IsOnLadder() || bReadingNote || IsKeypadOpen() || bDead || bEscaped) return;
    if (Equipped == TEXT("Glowstick") || Equipped == TEXT("Note") || Equipped == TEXT("Bandage") || Equipped == TEXT("FlashlightBattery"))
    {
        UseHeldItem();
        return;
    }
    RefreshFocus();
    GrabFocusedCrate(EKeys::LeftMouseButton);
}
void AShipSurvivor::ReleaseMouseDrag()
{
    if (CrateGrabKey == EKeys::LeftMouseButton) EndDrag();
}
void AShipSurvivor::GrabFocusedCrate(FKey Key)
{
    UPrimitiveComponent* Crate = FocusCrate.Get();
    if (!Crate || !TrackedPawn.IsValid()) return;
    EndUse();
    GrabDistance = FMath::Clamp(float((FocusHit - ViewOrigin()).Size2D()), 78.f, UseRange);
    GrabHeight = FocusHit.Z - TrackedPawn->GetActorLocation().Z;
    Grabber->GrabComponentAtLocation(Crate, NAME_None, FocusHit);
    bOwnDrag = IsDragging();
    CrateGrabKey = bOwnDrag ? Key : FKey();
}
void AShipSurvivor::EndDrag()
{
    if (IsDragging()) Grabber->ReleaseComponent();
    bOwnDrag = false;
    CrateGrabKey = FKey();
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

bool AShipSurvivor::HasPlayerView() const
{
    return TrackedPawn.IsValid() && LocalPC.IsValid() && LocalPC->GetViewTarget() == TrackedPawn.Get();
}

void AShipSurvivor::SetStockLightEnabled(bool bEnable)
{
    USpotLightComponent* Light = OriginalLight.Get();
    if (!Light || Light->IsVisible() == bEnable) return;
    // The stock component owns both visibility and its battery timer.
    for (UActorComponent* Component : Light->GetOwner()->GetComponents())
    {
        const FObjectPropertyBase* Lamp = FindFProperty<FObjectPropertyBase>(Component->GetClass(), TEXT("Flashlight Light"));
        if (Lamp && Lamp->GetObjectPropertyValue_InContainer(Component) == Light)
        {
            UFunction* Toggle = Component->FindFunction(TEXT("ToggleFlashlight"));
            if (Toggle && Toggle->NumParms == 0)
            {
                Component->ProcessEvent(Toggle, nullptr);
                return;
            }
        }
    }
    Light->SetVisibility(bEnable);
}

void AShipSurvivor::SetKit(bool bEnable)
{
    if (bKitEnabled == bEnable) return;
    bKitEnabled = bEnable;
    if (OriginalLight.IsValid())
    {
        if (bEnable) bOriginalLightVisible = OriginalLight->IsVisible();
        SetStockLightEnabled(!bEnable && bOriginalLightVisible);
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
    if (bKitEnabled)
    {
        const int32 Slot = UShipInventoryLibrary::FindSlot(TrackedPawn.Get(), PackClasses.FindRef(TEXT("StockLamp")).Get());
        if (Slot != INDEX_NONE) EquipSlot(Slot);
    }
    else if (LastStockSlot != INDEX_NONE) EquipSlot(LastStockSlot);
    else Notify(TEXT("Tab otwiera plecak. Kliknij przedmiot lub naciśnij numer pola."));
}
void AShipSurvivor::ToggleLamp()
{
    if (IsBaseMenuActive() || !bKitEnabled || bReadingNote || IsKeypadOpen() || bDead || bEscaped) return;
    if (Equipped == TEXT("Welder")) { Notify(TEXT("Przyłóż spawarkę do stanowiska i przytrzymaj E.")); return; }
    if (!CanAdjustLamp()) { Notify(TEXT("Wybierz latarkę w plecaku pod Tab.")); return; }
    if (PuzzleCharge <= 0.f) { Notify(TEXT("Bateria latarki zagadkowej jest pusta. Podejdź z nią do ładowarki.")); return; }
    bLampOn = !bLampOn;
    UpdateBeam();
    Beep(bLampOn ? 1.12f : .72f);
}
void AShipSurvivor::CycleFilter() { ChangeFilter(1); }
void AShipSurvivor::ScrollEquipment(FKey Key)
{
    if (IsStockInventoryVisible())
    {
        ChangeBackpackPage(Key == EKeys::MouseScrollUp ? -1 : 1);
        return;
    }
    if (IsBaseMenuActive() || bReadingNote || IsKeypadOpen() || bDead || bEscaped) return;
    const int32 Step = Key == EKeys::MouseScrollUp ? 1 : -1;
    if (IsDragging()) GrabDistance = FMath::Clamp(GrabDistance + Step * 24.f, 78.f, UseRange);
    else ChangeFilter(Step);
}
void AShipSurvivor::ChangeFilter(int32 Step)
{
    if (IsBaseMenuActive() || !bKitEnabled || bReadingNote || IsKeypadOpen() || bDead || bEscaped) return;
    if (!bHasColorLamp) { if (Step > 0) Notify(TEXT("Podnieś latarkę z kolorami, aby zmienić kolor.")); return; }
    if (Equipped != TEXT("ColorLamp")) return;
    int32 Index = 0;
    constexpr int32 Count = UE_ARRAY_COUNT(Filters);
    while (Index < Count && Filters[Index] != LastFilter) ++Index;
    Index = (Index + Step + Count) % Count;
    LastFilter = Filters[Index];
    UpdateBeam();
    Beep(.9f + .08f * Index);
}
bool AShipSurvivor::CanAdjustLamp() const
{
    return !bReadingNote && !IsKeypadOpen() && !bDead && !bEscaped && !IsBaseMenuActive()
        && (IsWalkingLamp() || (Equipped == TEXT("ColorLamp") && bHasColorLamp));
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
    if (IsWalkingLamp()) ApplyWalkingBeamSettings();
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

void AShipSurvivor::UpdateBeam()
{
    Filter = Equipped == TEXT("ColorLamp") ? LastFilter : FName(TEXT("White"));
    if (!bKitEnabled || bDead || bEscaped) { bWelding = false; bLampOn = false; }
    if (!bHasBattery || Charge <= 0.f) bWelding = false;
    if (Equipped != TEXT("ColorLamp") || !bHasColorLamp || PuzzleCharge <= 0.f) bLampOn = false;
    const bool bVisible = HasPlayerView() && (bLampOn || bWelding);
    if (Beam->IsVisible() != bVisible) Beam->SetVisibility(bVisible);
    if (bVisible)
    {
        if (TrackedPawn.IsValid()) Beam->SetWorldLocationAndRotation(ViewOrigin(), ViewDirection().Rotation());
        const FLinearColor Color = bWelding ? FLinearColor(.40f,.74f,1.f) : GetFilterColor();
        if (!Beam->GetLightColor().Equals(Color,.012f)) Beam->SetLightColor(Color);
    }
    if (GetWorld() && BeamParameters)
    {
        if (UMaterialParameterCollectionInstance* Ink = GetWorld()->GetParameterCollectionInstance(BeamParameters))
        {
            const bool bInkLit = bVisible && bLampOn;
            if (bInkLit)
            {
                const FVector Origin = Beam->GetComponentLocation();
                const FVector Axis = Beam->GetForwardVector();
                Ink->SetVectorParameterValue(TEXT("OriginRange"), FLinearColor(Origin.X, Origin.Y, Origin.Z, Beam->AttenuationRadius));
                Ink->SetVectorParameterValue(TEXT("AxisOuter"), FLinearColor(Axis.X, Axis.Y, Axis.Z, FMath::Cos(FMath::DegreesToRadians(Beam->OuterConeAngle))));
            }
            Ink->SetVectorParameterValue(TEXT("BeamResponse"), FLinearColor(FMath::Cos(FMath::DegreesToRadians(Beam->InnerConeAngle)),
                bInkLit ? PowerMultiplier(PowerLevel) : 0.f, 0.f, 0.f));
        }
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
    if (IsOnLadder()) return MountedLadder->Prompt(const_cast<AShipSurvivor*>(this));
    if (IsDragging()) return CrateGrabKey == EKeys::E ? TEXT("Puść E — odstaw skrzynię · Kółko myszy — odległość")
        : TEXT("Puść LPM — odstaw skrzynię · Kółko myszy — odległość");
    if (HeldStockObject.IsValid()) return TEXT("Puść E — puść uchwyt");
    if (FocusCrate.IsValid()) return TEXT("Przytrzymaj E — przesuń skrzynię");
    if (const AActor* Actor = StockTarget.Get())
        return StockInteraction && Actor->GetClass()->ImplementsInterface(StockInteraction)
            ? TEXT("E — użyj") : TEXT("Przytrzymaj E — chwyć");
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
    Keypad.Reset(); EnteredCode.Reset(); bReadingNote = false;
    LockControls(bDead || bEscaped);
    UpdateInputRouting();
}
void AShipSurvivor::CloseStockInventory()
{
    APlayerController* PC = LocalPC.Get();
    if (!PC) return;
    const FObjectPropertyBase* Ref = FindFProperty<FObjectPropertyBase>(PC->GetClass(), TEXT("W_InventoryMenuRef"));
    const UUserWidget* Menu = Ref ? Cast<UUserWidget>(Ref->GetObjectPropertyValue_InContainer(PC)) : nullptr;
    if (!IsValid(Menu) || !Menu->IsVisible()) return;
    UFunction* Toggle = PC->FindFunction(TEXT("ToggleInventory"));
    if (Toggle && Toggle->NumParms == 0) PC->ProcessEvent(Toggle, nullptr);
}
void AShipSurvivor::OpenKeypad(AShipFixture* Fixture)
{
    if (!IsValid(Fixture) || bDead || bEscaped) return;
    EndUse(); EndDrag();
    bReadingNote = false; Keypad = Fixture; EnteredCode.Reset();
    LockControls(true); UpdateInputRouting();
}
void AShipSurvivor::PressDigit(FKey Key)
{
    const int32 Digit = DigitFor(Key);
    if (Digit == INDEX_NONE || bDead || bEscaped) return;
    if (IsStockInventoryVisible())
    {
        if (Digit >= 1 && Digit <= ShipBackpackGrid::PageSize) EquipSlot(QuickSlotIndex(Digit));
        return;
    }
    if (IsBaseMenuActive()) return;
    if (IsKeypadOpen())
    {
        if (EnteredCode.Len() < GetCodeLength()) { EnteredCode.AppendChar(TCHAR('0' + Digit)); Beep(1.f + .035f * Digit); }
        return;
    }
    if (!bReadingNote && Digit >= 1 && Digit <= ShipBackpackGrid::PageSize) EquipSlot(QuickSlotIndex(Digit));
}
void AShipSurvivor::DeleteDigit()
{
    if (IsKeypadOpen() && !EnteredCode.IsEmpty()) { EnteredCode.LeftChopInline(1); Beep(.8f); }
}
int32 AShipSurvivor::GetCodeLength() const
{
    const AShipFixture* Fixture = Keypad.Get();
    return Fixture && !Fixture->Value.IsEmpty() ? Fixture->Value.TrimStartAndEnd().Len() : 4;
}
void AShipSurvivor::ConfirmCode()
{
    AShipFixture* Fixture = Keypad.Get();
    if (!Fixture) return;
    if (EnteredCode.Len() != GetCodeLength()) { Notify(TEXT("Uzupełnij wszystkie cyfry przed zatwierdzeniem.")); return; }
    Fixture->SubmitCode(this, EnteredCode);
    if (Fixture->bSolved) ClosePanel();
    else EnteredCode.Reset();
}
void AShipSurvivor::ReadNote(const FString& Text)
{
    if (Text.IsEmpty() || bDead || bEscaped) return;
    EndUse(); EndDrag();
    LastNote = Text; SelectedNote = Notes.AddUnique(Text); ScrollLines = 0;
    Keypad.Reset(); bReadingNote = true;
    LockControls(true); UpdateInputRouting();
}
void AShipSurvivor::ToggleInventory()
{
    if (IsBaseMenuActive() || IsKeypadOpen() || bDead || bEscaped) return;
    if (bReadingNote) { ClosePanel(); return; }
    if (LocalPC.IsValid())
        if (UFunction* Toggle = LocalPC->FindFunction(TEXT("ToggleInventory")); Toggle && Toggle->NumParms == 0)
            LocalPC->ProcessEvent(Toggle, nullptr);
}
void AShipSurvivor::PriorNote() { if (bReadingNote && !Notes.IsEmpty()) { SelectedNote = (SelectedNote + Notes.Num() - 1) % Notes.Num(); ScrollLines = 0; } }
void AShipSurvivor::NextNote() { if (bReadingNote && !Notes.IsEmpty()) { SelectedNote = (SelectedNote + 1) % Notes.Num(); ScrollLines = 0; } }
void AShipSurvivor::PageUp()
{
    if (bReadingNote) ScrollLines = FMath::Max(0, ScrollLines - 5);
    else ChangeBackpackPage(-1);
}
void AShipSurvivor::PageDown()
{
    if (bReadingNote) ScrollLines += 5;
    else ChangeBackpackPage(1);
}
void AShipSurvivor::ClampNoteScroll(int32 Maximum) { ScrollLines = FMath::Clamp(ScrollLines,0,FMath::Max(0,Maximum)); }

void AShipSurvivor::Die()
{
    if (bDead || bEscaped) return;
    CloseStockInventory();
    LeaveLadder();
    SetKit(true); bDead = true; ClosePanel(); EndUse(); EndDrag(); UpdateBeam(); Beep(.48f);
}
void AShipSurvivor::Escape()
{
    if (bDead || bEscaped) return;
    CloseStockInventory();
    LeaveLadder();
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
    RechargeGlowsticks();
    HandMesh->SetVisibility(HandMesh->GetStaticMesh() && HasPlayerView() && !bDead && !bEscaped && !bReadingNote && !IsStockInventoryVisible());
    if (IsBaseMenuActive())
    {
        Focus.Reset(); FocusCrate.Reset(); StockTarget.Reset();
        if (bOwnUse || HeldStockObject.IsValid()) EndUse();
        if (IsDragging()) EndDrag();
        TickEquipment(UGameplayStatics::IsGamePaused(this) ? 0.f : DeltaSeconds);
        UpdateInputRouting();
        return;
    }
    if (bLockedControls && !bReadingNote && !IsKeypadOpen() && !bEscaped && !bDead) LockControls(false);
    if (IsOnLadder() && !bReadingNote && !IsKeypadOpen())
    {
        const float Axis = float(LocalPC->IsInputKeyDown(EKeys::W)) - float(LocalPC->IsInputKeyDown(EKeys::S));
        MountedLadder->Climb(Axis, DeltaSeconds);
    }
    RefreshFocus();
    if (bOwnUse)
    {
        if (!HeldFixture.IsValid() || HeldFixture != Focus || !LocalPC->IsInputKeyDown(EKeys::E)) EndUse();
        else HeldFixture->HeldUse(this, DeltaSeconds);
    }
    if (IsDragging())
    {
        if (LocalPC->IsInputKeyDown(CrateGrabKey)) UpdateDrag();
        else EndDrag();
    }
    TickEquipment(DeltaSeconds);
    UpdateInputRouting();
}

void AShipSurvivor::TickEquipment(float DeltaSeconds)
{
    if (!bKitEnabled) return;
    SetStockLightEnabled(false);
    if (UActorComponent* Stats = MovementStats.Get())
        if (const FNumericProperty* Property = FindFProperty<FNumericProperty>(Stats->GetClass(),TEXT("CurrentStamina")))
            if (Property->IsFloatingPoint()) Stamina = Property->GetFloatingPointPropertyValue(Property->ContainerPtrToValuePtr<void>(Stats));
    const float Drain = bLampOn && HasPlayerView() ? GetLampDrainPerSecond() : 0.f;
    if (Drain > 0.f) PuzzleCharge = FMath::Max(0.f, PuzzleCharge - Drain * DeltaSeconds);
    if (PuzzleCharge > 20.f) bWarnedLowCharge = false;
    if (!bWarnedLowCharge && PuzzleCharge > 0.f && PuzzleCharge <= 15.f && Drain > 0.f)
    {
        bWarnedLowCharge = true;
        Notify(TEXT("Niski poziom baterii · Z zmniejsza moc, C zwęża wiązkę."));
    }
    if (Drain > 0.f && PuzzleCharge <= 0.f) Notify(TEXT("Latarka zagadkowa rozładowana. Zwykła latarka ma własną baterię."));
    UpdateBeam();
}
