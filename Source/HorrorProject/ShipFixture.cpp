#include "ShipFixture.h"
#include "ShipSurvivor.h"

#include "Components/BoxComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/OverlapResult.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInterface.h"
#include "Sound/SoundAttenuation.h"
#include "Sound/SoundBase.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
    const FName White(TEXT("White")), ColorLamp(TEXT("ColorLamp")), UV(TEXT("UV")), Welder(TEXT("Welder")), Battery(TEXT("Battery")), Card(TEXT("Card"));
    const FName Note(TEXT("Note")), Charger(TEXT("Charger")), Crate(TEXT("Crate")), Plate(TEXT("Plate")), Door(TEXT("Door"));
    const FName RepairDoor(TEXT("RepairDoor")), Keypad(TEXT("Keypad")), BridgePad(TEXT("BridgePad")), Glyph(TEXT("Glyph"));
    const FName Roar(TEXT("Roar")), Scare(TEXT("Scare")), ExitKind(TEXT("Exit")), Hazard(TEXT("Hazard")), PushCrate(TEXT("PushCrate"));
    const FLinearColor Amber(1.f, .32f, .03f), Green(.05f, 1.f, .35f), Blue(.08f, .55f, 1.f);

    bool IsPickup(FName K) { return K == White || K == ColorLamp || K == Welder || K == Battery || K == Card; }
    bool IsDoor(FName K) { return K == Door || K == RepairDoor; }

    FString PickupName(FName K)
    {
        if (K == White) return TEXT("latarkę");
        if (K == ColorLamp) return TEXT("latarkę z kolorami");
        if (K == Welder) return TEXT("spawarkę");
        if (K == Battery) return TEXT("baterię");
        return TEXT("kartę dostępu");
    }

    FString FilterLabel(FName F)
    {
        if (F == TEXT("Red")) return TEXT("[R] CZERWONY");
        if (F == TEXT("Green")) return TEXT("[G] ZIELONY");
        if (F == TEXT("Blue")) return TEXT("[B] NIEBIESKI");
        if (F == White) return TEXT("[W] BIAŁY");
        return TEXT("[UV] FIOLET");
    }

    FColor FilterColor(FName F)
    {
        if (F == TEXT("Red")) return FColor(255, 69, 58);
        if (F == TEXT("Green")) return FColor(90, 255, 130);
        if (F == TEXT("Blue")) return FColor(80, 175, 255);
        if (F == White) return FColor::White;
        return FColor(203, 116, 255);
    }

    bool CanAct(const AShipSurvivor* S) { return IsValid(S) && !S->bDead && !S->bEscaped; }
}

AShipFixture::AShipFixture()
{
    PrimaryActorTick.bCanEverTick = true;
    Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
    SetRootComponent(Mesh);
    Mesh->SetMobility(EComponentMobility::Movable);
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
    if (Cube.Succeeded()) Mesh->SetStaticMesh(Cube.Object);

    Trigger = CreateDefaultSubobject<UBoxComponent>(TEXT("InteractionBounds"));
    Trigger->SetupAttachment(Mesh);
    Trigger->SetAbsolute(false, false, true);
    Trigger->SetCanEverAffectNavigation(false);
    GlyphText = CreateDefaultSubobject<UTextRenderComponent>(TEXT("FilteredText"));
    GlyphText->SetupAttachment(Mesh);
    GlyphText->SetAbsolute(false, false, true);
    GlyphText->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    GlyphText->SetCastShadow(false);
    GlyphText->SetHorizontalAlignment(EHTA_Center);
    GlyphText->SetVerticalAlignment(EVRTA_TextCenter);
    PaintLayer = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("FilteredPaint"));
    PaintLayer->SetupAttachment(Mesh);
    PaintLayer->SetAbsolute(false, false, true);
    PaintLayer->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    PaintLayer->SetGenerateOverlapEvents(false);
    PaintLayer->SetCanEverAffectNavigation(false);
    PaintLayer->SetCastShadow(false);
    PaintLayer->SetVisibility(false);
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Plane(TEXT("/Engine/BasicShapes/Plane.Plane"));
    if (Plane.Succeeded()) PaintLayer->SetStaticMesh(Plane.Object);

    Indicator = CreateDefaultSubobject<UPointLightComponent>(TEXT("Indicator"));
    Indicator->SetupAttachment(Mesh);
    Indicator->SetCastShadows(false);
    Indicator->SetIntensity(35.f);
    Indicator->SetAttenuationRadius(74.f);
    Indicator->SetVisibility(false);
}

void AShipFixture::OnConstruction(const FTransform& Transform)
{
    Super::OnConstruction(Transform);
    Configure();
}

void AShipFixture::Configure()
{
    const bool bInvisible = Kind == Glyph || Kind == Hazard || Kind == Roar;
    const bool bSolid = IsDoor(Kind) || Kind == Crate || Kind == Plate || Kind == Scare;
    Mesh->SetMobility(EComponentMobility::Movable);
    Mesh->SetHiddenInGame(bInvisible, false);
    if (bSolid)
    {
        Mesh->SetCollisionProfileName(Kind == Crate ? TEXT("PhysicsActor") : TEXT("BlockAllDynamic"));
        Mesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
        if (Kind == Crate)
        {
            Mesh->SetCollisionObjectType(ECC_PhysicsBody);
            Mesh->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
            Tags.AddUnique(PushCrate);
            Mesh->ComponentTags.AddUnique(PushCrate);
        }
    }
    else
    {
        Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    }

    FVector Min, Max;
    Mesh->GetLocalBounds(Min, Max);
    const FVector Half = ((Max-Min) * .5f * Mesh->GetComponentScale().GetAbs()).ComponentMax(FVector(1.f));
    const FVector Center = Mesh->GetComponentTransform().TransformPosition((Min+Max)*.5f);
    FVector BoxHalf = Half;
    FVector BoxCenter = Center;
    if (Kind == Plate)
    {
        BoxCenter += GetActorUpVector() * (Half.Z + 15.f);
        BoxHalf.Z = 19.f;
    }
    else if (Kind == Scare)
    {
        // A short approach region permits either an explicit use or a nearby trigger.
        BoxHalf += FVector(55.f, 35.f, 35.f);
    }
    else if (IsPickup(Kind) || Kind == Keypad || Kind == BridgePad || Kind == Note || Kind == Charger)
    {
        BoxHalf = BoxHalf.ComponentMax(FVector(6.f));
    }
    Trigger->SetWorldLocationAndRotation(BoxCenter, Mesh->GetComponentQuat());
    Trigger->SetBoxExtent(BoxHalf, false);
    Trigger->SetCollisionObjectType(ECC_WorldDynamic);
    Trigger->SetCollisionResponseToAllChannels(ECR_Ignore);
    Trigger->SetGenerateOverlapEvents(false);
    // Doors and crates already provide their own trace surfaces. Glyphs never intercept light rays.
    const bool bInteractionBox = !bInvisible && !IsDoor(Kind) && Kind != Crate;
    Trigger->SetCollisionResponseToChannel(ECC_Visibility, bInteractionBox ? ECR_Block : ECR_Ignore);
    Trigger->SetCollisionEnabled(bInteractionBox ? ECollisionEnabled::QueryOnly : ECollisionEnabled::NoCollision);

    const FVector Origin = GetActorLocation();
    const FVector Right = GetActorRightVector(), Up = GetActorUpVector(), Forward = GetActorForwardVector();
    GlyphText->SetWorldLocation(PaintMaterial ? Origin + Right*TextOffset.X + Up*TextOffset.Y : Origin);
    GlyphText->SetWorldSize(FMath::Max(1.f,GlyphSize));
    const FName Filter = RequiredFilter.IsNone() ? UV : RequiredFilter;
    GlyphText->SetTextRenderColor(FilterColor(Filter));
    GlyphText->SetText(FText::FromString(Kind == Glyph ? FilterLabel(Filter) + TEXT("  ") + Value : FString()));
    GlyphText->SetVisibility(false);
    PaintLayer->SetWorldLocationAndRotation(Origin + Forward*.15 + Right*PaintOffset.X + Up*PaintOffset.Y,
        FRotationMatrix::MakeFromXZ(Right, Forward).ToQuat());
    PaintLayer->SetWorldScale3D(FVector(FMath::Max(1.,PaintSize.X)/100., FMath::Max(1.,PaintSize.Y)/100., 1.));
    PaintLayer->SetMaterial(0,PaintMaterial.Get());
    PaintLayer->SetVisibility(false);
    Indicator->SetWorldLocation(Center + GetActorUpVector() * (Half.Z + 5.f));
    SetIndicator(Kind == Plate || Kind == Keypad || Kind == BridgePad || Kind == Charger, Kind == Charger ? Blue : Amber);
}

void AShipFixture::BeginPlay()
{
    Super::BeginPlay();
    Configure();
    ClosedLocation = GetActorLocation();
    DoorOffset = GetActorRightVector() * SlideDistance;
    if (Kind == Crate)
    {
        Mesh->SetSimulatePhysics(true);
        Mesh->SetMassOverrideInKg(NAME_None, 38.f, true);
        Mesh->SetLinearDamping(.65f);
        Mesh->SetAngularDamping(2.f);
    }
    if (Kind == Door && Order < 0)
    {
        bOpen = true;
        DoorFraction = 1.f;
        SetActorLocation(ClosedLocation + DoorOffset);
    }
    if (Kind == Scare && Order < 0)
    {
        DoorFraction = .12f;
        SetActorLocation(ClosedLocation + DoorOffset * DoorFraction);
    }
    LocalAttenuation = NewObject<USoundAttenuation>(this);
    LocalAttenuation->Attenuation.bAttenuate = true;
    LocalAttenuation->Attenuation.bSpatialize = true;
    LocalAttenuation->Attenuation.AttenuationShapeExtents = FVector(160.f);
    LocalAttenuation->Attenuation.FalloffDistance = 1200.f;
    SetActorTickEnabled(IsDoor(Kind) || Kind == Glyph || Kind == Plate || Kind == Roar || Kind == Scare || Kind == Hazard || Kind == ExitKind);
    SetActorTickInterval(IsDoor(Kind) || Kind == Scare ? 0.f : .05f);
}

void AShipFixture::EndPlay(const EEndPlayReason::Type Reason)
{
    if (RepairUser.IsValid()) RepairUser->SetWelding(false);
    Super::EndPlay(Reason);
}

AShipSurvivor* AShipFixture::FindSurvivor()
{
    if (!SurvivorManager.IsValid())
        for (TActorIterator<AShipSurvivor> It(GetWorld()); It; ++It) { SurvivorManager = *It; break; }
    return SurvivorManager.Get();
}

void AShipFixture::SetIndicator(bool bLit, FLinearColor Color)
{
    Indicator->SetLightColor(Color);
    Indicator->SetVisibility(bLit);
}

void AShipFixture::PlayCue(float Volume, float Pitch, USoundBase* Override)
{
    USoundBase* Sound = Override ? Override : Cue.Get();
    if (Sound)
        UGameplayStatics::PlaySoundAtLocation(this, Sound, GetActorLocation(), FRotator::ZeroRotator,
            Volume, Pitch, 0.f, LocalAttenuation.Get());
}

void AShipFixture::ChangeDoor(bool bShouldOpen)
{
    if (bOpen == bShouldOpen) return;
    bOpen = bShouldOpen;
    PlayCue(.7f, bOpen ? 1.f : .84f);
}

void AShipFixture::BroadcastDoors(bool bShouldOpen, bool bLockClosed)
{
    if (Channel.IsNone()) return;
    for (TActorIterator<AShipFixture> It(GetWorld()); It; ++It)
    {
        AShipFixture* F = *It;
        if (F->Kind != Door || F->Channel != Channel || (bShouldOpen && F->bInterlockClosed)) continue;
        if (bLockClosed) F->bInterlockClosed = true;
        F->bActive = bShouldOpen;
        F->ChangeDoor(bShouldOpen);
    }
}

bool AShipFixture::VolumeOccupied(bool bAllowCrates) const
{
    UWorld* World = GetWorld();
    const bool bCrateOnly = bAllowCrates && Value.Equals(TEXT("CrateOnly"), ESearchCase::IgnoreCase);
    const APawn* Pawn = bCrateOnly ? nullptr : UGameplayStatics::GetPlayerPawn(this, 0);
    if (!World || (!Pawn && !bAllowCrates)) return false;
    FCollisionObjectQueryParams Objects;
    Objects.AddObjectTypesToQuery(ECC_Pawn);
    if (bAllowCrates) Objects.AddObjectTypesToQuery(ECC_PhysicsBody);
    TArray<FOverlapResult> Hits;
    World->OverlapMultiByObjectType(Hits, Trigger->GetComponentLocation(), Trigger->GetComponentQuat(), Objects,
        FCollisionShape::MakeBox(Trigger->GetScaledBoxExtent()), FCollisionQueryParams(SCENE_QUERY_STAT(ShipFixtureVolume), false, this));
    for (const FOverlapResult& Hit : Hits)
    {
        const AActor* Actor = Hit.GetActor();
        if (Actor && (Actor == Pawn || (bAllowCrates && Actor->ActorHasTag(PushCrate)))) return true;
    }
    return false;
}

void AShipFixture::CompletePlates(const TArray<AShipFixture*>& Plates, AShipSurvivor* Survivor)
{
    for (AShipFixture* P : Plates)
    {
        P->bSolved = P->bActive = true;
        P->Progress = 1.f;
        P->SetIndicator(true, Green);
    }
    BroadcastDoors(true);
    PlayCue(.8f,1.15f);
    if (Survivor) Survivor->Notify(TEXT("Układ płyt odblokowany."));
}

void AShipFixture::EnterPlate(AShipSurvivor* Survivor)
{
    TArray<AShipFixture*> Group;
    if (Channel.IsNone()) Group.Add(this);
    else for (TActorIterator<AShipFixture> It(GetWorld()); It; ++It)
        if (It->Kind == Plate && It->Channel == Channel) Group.Add(*It);

    if (Order > 0)
    {
        int32 Next = MAX_int32;
        for (const AShipFixture* P : Group)
            if (P->Order > 0 && !P->bStepComplete) Next = FMath::Min(Next,P->Order);
        if (Order != Next || bStepComplete)
        {
            for (AShipFixture* P : Group)
            {
                P->bStepComplete = false;
                P->Progress = 0.f;
                P->SetIndicator(true, Amber);
            }
            PlayCue(.65f,.55f);
            if (Survivor) Survivor->Notify(TEXT("Błędna kolejność. Zacznij od pierwszej płyty."));
            return;
        }
        bStepComplete = true;
        SetIndicator(true, Blue);
    }
    int32 Done = 0;
    for (const AShipFixture* P : Group)
        if (P->Order > 0 ? P->bStepComplete : P->VolumeOccupied(true)) ++Done;
    for (AShipFixture* P : Group) P->Progress = float(Done) / Group.Num();
    if (Done == Group.Num()) CompletePlates(Group, Survivor);
    else PlayCue(.45f,1.f);
}

bool AShipFixture::HasBridgeAuthorization() const
{
    for (TActorIterator<AShipFixture> It(GetWorld()); It; ++It)
        if (It->Kind == BridgePad && It->bSolved && (Channel.IsNone() || Channel == It->Channel)) return true;
    return false;
}

bool AShipFixture::CanWeld(const AShipSurvivor* S) const
{
    return CanAct(S) && S->bHasBattery && S->bHasWelder && S->Charge > 0.f && S->Equipped == TEXT("Welder");
}

void AShipFixture::ActivateExit(AShipSurvivor* S)
{
    if (!CanAct(S) || bSolved) return;
    if (!HasBridgeAuthorization())
    {
        S->Notify(TEXT("Wyjście zablokowane. Autoryzuj kartę i kod na panelu mostka."));
        return;
    }
    bSolved = bActive = true;
    PlayCue();
    S->Escape();
}

void AShipFixture::BeginScare(AShipSurvivor* S)
{
    if (!CanAct(S) || bActive) return;
    bActive = true;
    ScareStage = 1;
    NextScareSound = GetWorld()->GetTimeSeconds() + .75;
    PlayCue(.85f,1.04f);
    S->Notify(TEXT("Za drzwiami coś się poruszyło."));
}

void AShipFixture::CheckVolume(AShipSurvivor* S)
{
    if (!CanAct(S)) return;
    const bool bInside = VolumeOccupied(Kind == Plate);
    if (bInside == bOccupied) return;
    bOccupied = bInside;
    if (Kind == Plate)
    {
        if (bSolved) return;
        bActive = bOccupied;
        SetIndicator(true,bOccupied || bStepComplete ? Blue : Amber);
        if (bOccupied) EnterPlate(S);
        else if (Order == 0)
        {
            // A simultaneous group loses partial progress as soon as a weight is removed.
            int32 Total=0, Done=0;
            for (TActorIterator<AShipFixture> It(GetWorld()); It; ++It)
                if (It->Kind == Plate && It->Channel == Channel && (!Channel.IsNone() || *It == this))
                {
                    ++Total;
                    if (It->Order > 0 ? It->bStepComplete : It->VolumeOccupied(true)) ++Done;
                }
            for (TActorIterator<AShipFixture> It(GetWorld()); It; ++It)
                if (It->Kind == Plate && It->Channel == Channel && (!Channel.IsNone() || *It == this))
                    It->Progress = Total ? float(Done)/Total : 0.f;
        }
    }
    else if (bOccupied)
    {
        if (Kind == Hazard) { PlayCue(); S->Die(); }
        else if (Kind == ExitKind) ActivateExit(S);
        else if (Kind == Scare) BeginScare(S);
        else if (Kind == Roar && !bActive)
        {
            bActive = bSolved = true;
            BroadcastDoors(false,true);
            PlayCue();
            S->Notify(TEXT("Ryk za plecami. Śluza została zamknięta."));
        }
    }
}

void AShipFixture::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    if (IsDoor(Kind) || (Kind == Scare && bActive && DoorFraction > 0.f))
    {
        const float Target = bOpen ? 1.f : 0.f;
        if (DoorFraction != Target)
        {
            DoorFraction = FMath::FInterpConstantTo(DoorFraction, Target, DeltaSeconds, 1.f/FMath::Max(.1f,TravelTime));
            const float Smooth = FMath::SmoothStep(0.f,1.f,DoorFraction);
            SetActorLocation(ClosedLocation + DoorOffset*Smooth);
        }
        if (IsDoor(Kind)) return;
    }
    AShipSurvivor* S = FindSurvivor();
    if (Kind == Glyph)
    {
        const FName Filter = RequiredFilter.IsNone() ? UV : RequiredFilter;
        const bool bShown = CanAct(S) && S->Filter == Filter && S->BeamReaches(GetActorLocation());
        if (bActive != bShown)
        {
            bActive = bShown;
            GlyphText->SetVisibility(bShown && bShowText);
            PaintLayer->SetVisibility(bShown && PaintMaterial != nullptr);
        }
        return;
    }
    if (Kind == Scare && bActive && ScareStage < 4 && GetWorld()->GetTimeSeconds() >= NextScareSound)
    {
        ++ScareStage;
        if (ScareStage == 2) PlayCue(.55f,.85f,BreathCue.Get());
        else PlayCue(ScareStage == 3 ? .8f : 1.f, ScareStage == 3 ? .88f : 1.08f,ImpactCue.Get());
        NextScareSound = GetWorld()->GetTimeSeconds() + (ScareStage == 2 ? 1.1 : .65);
        bSolved = ScareStage == 4;
    }
    if (Kind == Plate && bSolved) return;
    if ((Kind == Scare || Kind == Roar || Kind == ExitKind) && bActive) return;
    CheckVolume(S);
}

FString AShipFixture::Prompt(AShipSurvivor* S) const
{
    if (!CanAct(S) || Kind == Glyph || Kind == Hazard || Kind == Roar || (IsPickup(Kind) && bActive)) return {};
    FString Action;
    if (IsPickup(Kind)) Action = TEXT("E — podnieś ") + PickupName(Kind);
    else if (Kind == Note) Action = TEXT("E — przeczytaj notatkę");
    else if (Kind == Charger) Action = S->bHasBattery ? TEXT("E — naładuj baterię") : TEXT("Potrzebna bateria");
    else if (Kind == Crate) Action = TEXT("Skrzynia fizyczna — chwyć lub przesuń");
    else if (Kind == Plate)
    {
        if (bSolved) Action = TEXT("Układ odblokowany");
        else if (Order > 0) Action = FString::Printf(TEXT("Płyta %d — wejdź w odpowiedniej kolejności"),Order);
        else Action = Value.Equals(TEXT("CrateOnly"), ESearchCase::IgnoreCase) ? TEXT("Postaw skrzynię na płycie") : TEXT("Obciąż płytę sobą lub skrzynią");
    }
    else if (Kind == Door)
        Action = Channel.IsNone() && !bInterlockClosed ? (bOpen ? TEXT("E — zamknij drzwi") : TEXT("E — otwórz drzwi")) : (bOpen ? TEXT("Przejście otwarte") : TEXT("Drzwi zablokowane przez sterownik"));
    else if (Kind == RepairDoor)
        Action = bSolved ? (bOpen ? TEXT("E — zamknij naprawione drzwi") : TEXT("E — otwórz naprawione drzwi"))
            : (CanWeld(S) ? FString::Printf(TEXT("Przytrzymaj E — spawanie %d%%"),FMath::RoundToInt(Progress*100.f)) : TEXT("Wybierz spawarkę z naładowaną baterią"));
    else if (Kind == Keypad || Kind == BridgePad)
        Action = bSolved ? TEXT("Dostęp przyznany") : (Kind == BridgePad && !S->bHasCard ? TEXT("Panel wymaga karty dostępu") : TEXT("E — wpisz kod"));
    else if (Kind == Scare) Action = bActive ? TEXT("Nasłuchuj przy drzwiach") : TEXT("E — sprawdź, co słychać za drzwiami");
    else if (Kind == ExitKind) Action = HasBridgeAuthorization() ? TEXT("E — skorzystaj z wyjścia") : TEXT("Wyjście wymaga autoryzacji mostka");
    if (Action.IsEmpty()) return Caption;
    return Caption.IsEmpty() ? Action : Caption + TEXT(" · ") + Action;
}

void AShipFixture::Use(AShipSurvivor* S)
{
    if (!CanAct(S)) return;
    if (IsPickup(Kind))
    {
        if (bActive) return;
        if (Kind == White) S->bHasWhite = true;
        else if (Kind == ColorLamp) S->bHasColorLamp = true;
        else if (Kind == Welder) S->bHasWelder = true;
        else if (Kind == Battery) { S->bHasBattery = true; S->Charge = 100.f; }
        else if (Kind == Card) S->bHasCard = true;
        S->Notify(TEXT("Zebrano ") + PickupName(Kind) + TEXT("."));
        bActive = bSolved = true;
        PlayCue();
        SetActorEnableCollision(false);
        SetActorHiddenInGame(true);
        return;
    }
    if (Kind == Note)
    {
        bActive = true;
        S->ReadNote(Value.IsEmpty() ? Caption : Value);
        PlayCue(.6f);
    }
    else if (Kind == Charger)
    {
        if (!S->bHasBattery) { S->Notify(TEXT("Najpierw podnieś baterię.")); return; }
        S->Charge = 100.f;
        bActive = true;
        Progress = 1.f;
        SetIndicator(true,Green);
        PlayCue();
        S->Notify(TEXT("Bateria naładowana do 100%."));
    }
    else if (Kind == Door)
    {
        if (Channel.IsNone() && !bInterlockClosed) { ChangeDoor(!bOpen); bActive = bOpen; }
        else S->Notify(bOpen ? TEXT("Przejście otwarte.") : TEXT("Zamek jest sterowany zdalnie."));
    }
    else if (Kind == RepairDoor)
    {
        if (bSolved) { ChangeDoor(!bOpen); return; }
        if (!CanWeld(S)) { S->Notify(TEXT("Do naprawy potrzebna jest wybrana spawarka i naładowana bateria.")); return; }
        RepairUser = S;
        S->SetWelding(true);
    }
    else if (Kind == Keypad || Kind == BridgePad)
    {
        if (bSolved) { S->Notify(TEXT("Panel ma już autoryzację.")); return; }
        if (Kind == BridgePad && !S->bHasCard) { S->Notify(TEXT("Włóż kartę mostka przed wpisaniem kodu.")); return; }
        CodeUser = S;
        S->OpenKeypad(this);
    }
    else if (Kind == Scare) BeginScare(S);
    else if (Kind == ExitKind) ActivateExit(S);
}

void AShipFixture::HeldUse(AShipSurvivor* S, float DeltaSeconds)
{
    if (Kind != RepairDoor || bSolved || !S || RepairUser.Get() != S) return;
    if (!CanWeld(S)) { StopUse(S); return; }
    S->SetWelding(true);
    Progress = FMath::Clamp(Progress + FMath::Max(0.f,DeltaSeconds)/FMath::Max(.25f,RepairTime),0.f,1.f);
    if (Progress < 1.f) return;
    bSolved = bActive = true;
    S->SetWelding(false);
    RepairUser.Reset();
    ChangeDoor(true);
    BroadcastDoors(true);
    S->Notify(TEXT("Spaw zakończony. Rygiel jest trwale odblokowany."));
}

void AShipFixture::StopUse(AShipSurvivor* S)
{
    if (Kind != RepairDoor || !S || RepairUser.Get() != S) return;
    S->SetWelding(false);
    RepairUser.Reset();
    if (!bSolved) Progress = 0.f;
}

void AShipFixture::SubmitCode(AShipSurvivor* S, const FString& Code)
{
    if (!CanAct(S) || (Kind != Keypad && Kind != BridgePad) || CodeUser.Get() != S || bSolved) return;
    if (Kind == BridgePad && !S->bHasCard)
    {
        S->Notify(TEXT("Panel wymaga karty dostępu."));
        return;
    }
    const FString Expected = Value.IsEmpty() ? FString(Kind == BridgePad ? TEXT("6109") : TEXT("4713")) : Value;
    if (Code.TrimStartAndEnd() != Expected.TrimStartAndEnd())
    {
        PlayCue(.5f,.6f);
        S->Notify(TEXT("Nieprawidłowy kod."));
        return;
    }
    CodeUser.Reset();
    bSolved = bActive = true;
    Progress = 1.f;
    SetIndicator(true,Green);
    PlayCue(.75f,1.12f);
    BroadcastDoors(true);
    S->Notify(Kind == BridgePad ? TEXT("Mostek autoryzowany. Wyjście dostępne.") : TEXT("Kod poprawny. Blokada zwolniona."));
}
