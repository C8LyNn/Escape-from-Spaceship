#include "ShipSurvivor.h"
#include "ShipFixture.h"
#include "ShipBackpackGrid.h"
#include "ShipGlowstick.h"
#include "ShipInventoryLibrary.h"
#include "Blueprint/UserWidget.h"
#include "Camera/CameraComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"
#include "UObject/UnrealType.h"

namespace
{
    const TCHAR* const PackKinds[] = {TEXT("White"), TEXT("ColorLamp"), TEXT("Welder"), TEXT("Battery"),
        TEXT("Card"), TEXT("Note"), TEXT("Glowstick"), TEXT("StockLamp")};
    const TCHAR* const PackLabels[] = {TEXT("Latarka zwykła"), TEXT("Latarka zagadkowa"), TEXT("Spawarka WD01"),
        TEXT("Akumulator spawarki"), TEXT("Karta mostka"), TEXT("Notatki serwisowe"), TEXT("Glowsticki"), TEXT("Latarka zwykła")};
    const TCHAR* const StockKinds[] = {TEXT("Bandage"), TEXT("FlashlightBattery"), TEXT("DoorKey")};
    const TCHAR* const StockMeshes[] = {TEXT("Bandage"), TEXT("Battery"), TEXT("DoorKey")};
    constexpr double RefillSeconds = 12.;
}

void AShipSurvivor::InitPack()
{
    for (const TCHAR* Kind : PackKinds)
    {
        const FString Path = FString(TEXT("/Game/ShipMechanics/Pack/BP_Pack_")) + Kind;
        ConstructorHelpers::FClassFinder<AActor> Item(*Path);
        PackClasses.Add(FName(Kind), Item.Class);
    }
    for (int32 I = 0; I < UE_ARRAY_COUNT(StockKinds); ++I)
    {
        const FString Path = FString(TEXT("/Game/Blueprints/Actors/Inventory/BP_InventoryItem_")) + StockKinds[I];
        ConstructorHelpers::FClassFinder<AActor> Item(*Path);
        PackClasses.Add(FName(StockKinds[I]), Item.Class);
        ConstructorHelpers::FObjectFinder<UStaticMesh> Mesh(*(FString(TEXT("/Game/Props/Meshes/")) + StockMeshes[I]));
        PackMeshes.Add(FName(StockKinds[I]), Mesh.Object);
        if (Item.Class && !UShipInventoryLibrary::IsDoorKey(Item.Class))
            PackDisplayScales.Add(FName(StockKinds[I]), FVector(UShipInventoryLibrary::StockPickupScale(Item.Class)));
    }
    ConstructorHelpers::FObjectFinder<UStaticMesh> StockFlash(TEXT("/Game/StudentShowcase/Assets/Oswietlenie/SRC_05/HandFlashlight/SM_HandFlashlight/StaticMeshes/SM_HandFlashlight"));
    PackMeshes.Add(TEXT("StockLamp"), StockFlash.Object);
    PackDisplayScales.Add(TEXT("StockLamp"), FVector(1.15));
    ConstructorHelpers::FObjectFinder<UStaticMesh> GlowTube(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
    PackMeshes.Add(TEXT("Glowstick"), GlowTube.Object);
    HandMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("HeldItemMesh"));
    HandMesh->SetupAttachment(RootComponent);
    HandMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    HandMesh->SetGenerateOverlapEvents(false);
    HandMesh->SetCastShadow(false);
    HandMesh->SetCanEverAffectNavigation(false);
    HandClass = PackClasses.FindRef(TEXT("StockLamp"));
    HandAmount = HandClass ? 1 : 0;
}

FName AShipSurvivor::HandKind(UClass* Type) const
{
    if (Type)
        for (const auto& Entry : PackClasses)
            if (Entry.Value && Type->IsChildOf(Entry.Value.Get())) return Entry.Key;
    return NAME_None;
}

bool AShipSurvivor::CollectItem(AShipFixture* Fixture)
{
    if (!IsValid(Fixture) || !TrackedPawn.IsValid()) return false;
    if (const int32 Capacity = Fixture->BackpackUpgradeCapacity())
    {
        if (BackpackCapacity >= Capacity)
        {
            Notify(FString::Printf(TEXT("Twój plecak ma już %s."), *ShipBackpackGrid::CapacityLabel(BackpackCapacity)));
            return false;
        }
        if (!UShipInventoryLibrary::ExpandBackpack(LocalPC.Get(), Capacity)) return false;
        BackpackCapacity = Capacity;
        Notify(FString::Printf(TEXT("Założono %s plecak · %s. Tab otwiera plecak."),
            Capacity == 16 ? TEXT("mały") : TEXT("duży"), *ShipBackpackGrid::CapacityLabel(Capacity)));
        return true;
    }
    const FName Kind = Fixture->Kind;
    if (Kind == TEXT("White"))
    {
        if (!UShipInventoryLibrary::StorePackItem(TrackedPawn.Get(), PackClasses.FindRef(TEXT("FlashlightBattery")).Get(), 0))
        {
            Notify(TEXT("Plecak pełny. Zwolnij pole na zapasową baterię."));
            return false;
        }
        Notify(TEXT("Zapasowa bateria w plecaku. Weź ją do ręki i użyj LPM, aby doładować zwykłą latarkę."));
        return true;
    }
    UClass* Item = PackClasses.FindRef(Kind).Get();
    if (!Item) return false;
    // Notes are kept as individual pages of one carried notebook.
    if (HandClass != Item && UShipInventoryLibrary::FindSlot(TrackedPawn.Get(), Item) == INDEX_NONE)
    {
        int32 Preferred = 0;
        while (Preferred < UE_ARRAY_COUNT(PackKinds) && Kind != PackKinds[Preferred]) ++Preferred;
        if (!UShipInventoryLibrary::StorePackItem(TrackedPawn.Get(), Item, Preferred))
        {
            Notify(TEXT("Plecak pełny. Zwolnij pole pod Tab i spróbuj ponownie."));
            return false;
        }
    }
    if (Kind != TEXT("Glowstick") && Fixture->Mesh->GetStaticMesh())
    {
        PackMeshes.Add(Kind, Fixture->Mesh->GetStaticMesh());
        PackDisplayScales.Add(Kind, Fixture->Mesh->GetComponentScale());
    }
    if (Kind == TEXT("ColorLamp") && !bHasColorLamp) { bHasColorLamp = true; PuzzleCharge = 100.f; }
    if (Kind == TEXT("Welder")) bHasWelder = true;
    if (Kind == TEXT("Card")) bHasCard = true;
    if (Kind == TEXT("Battery")) { bHasBattery = true; Charge = 100.f; }
    if (Kind == TEXT("Glowstick")) { GlowstickCharges = MaxGlowsticks; NextGlowstick = 0.; }
    if (Kind == TEXT("Note"))
    {
        LastNote = Fixture->Value.IsEmpty() ? Fixture->Caption : Fixture->Value;
        SelectedNote = Notes.AddUnique(LastNote);
        ScrollLines = 0;
    }
    const int32 Index = UShipInventoryLibrary::FindSlot(TrackedPawn.Get(), Item);
    if (Index == INDEX_NONE) Notify(TEXT("Przedmiot w ręce."));
    else if (BackpackPageCount() == 1)
        Notify(FString::Printf(TEXT("W plecaku: %s · Tab lub %d, aby wziąć do ręki."), *Fixture->Caption, Index+1));
    else
        Notify(FString::Printf(TEXT("W plecaku: %s · strona %d, klawisz %d."), *Fixture->Caption,
            Index / ShipBackpackGrid::PageSize + 1, Index % ShipBackpackGrid::PageSize + 1));
    return true;
}

int32 AShipSurvivor::BackpackPageCount() const
{
    return FMath::DivideAndRoundUp(BackpackCapacity, ShipBackpackGrid::PageSize);
}

int32 AShipSurvivor::QuickSlotIndex(int32 Digit) const
{
    return BackpackPage * ShipBackpackGrid::PageSize + Digit - 1;
}

void AShipSurvivor::ChangeBackpackPage(int32 Delta)
{
    if (!IsStockInventoryVisible()) return;
    const int32 Next = FMath::Clamp(BackpackPage + Delta, 0, BackpackPageCount() - 1);
    if (Next != BackpackPage && ShipBackpackGrid::ShowPage(LocalPC.Get(), Next)) BackpackPage = Next;
}

bool AShipSurvivor::IsStockInventoryVisible() const
{
    const APlayerController* PC = LocalPC.Get();
    const FObjectPropertyBase* Ref = PC ? FindFProperty<FObjectPropertyBase>(PC->GetClass(), TEXT("W_InventoryMenuRef")) : nullptr;
    const UUserWidget* Menu = Ref ? Cast<UUserWidget>(Ref->GetObjectPropertyValue_InContainer(PC)) : nullptr;
    return Menu && Menu->IsInViewport() && Menu->IsVisible();
}

bool AShipSurvivor::EquipSlot(int32 Index)
{
    if (bDead || bEscaped || IsKeypadOpen() || bReadingNote || !TrackedPawn.IsValid()) return false;
    if (!UShipInventoryLibrary::SwapWithHand(TrackedPawn.Get(), Index, HandClass, HandAmount)) return false;
    EndUse();
    EndDrag();
    const FName Kind = HandKind(HandClass.Get());
    Equipped = Kind.ToString();
    if (Kind == TEXT("StockLamp")) LastStockSlot = Index;
    bLampOn = false;
    SetKit(!IsWalkingLamp());
    // Stowing switches the light off. Drawing it again does not start draining its cell.
    if (bKitEnabled) bOriginalLightVisible = false;
    if (IsStockInventoryVisible()) CloseStockInventory();
    ApplyBeamSettings();
    UpdateBeam();
    UpdateHandMesh();
    UpdateInputRouting();
    Notify(FString::Printf(TEXT("W ręce: %s"), *GetToolLabel()));
    return true;
}

const TCHAR* AShipSurvivor::LampSlotLabel(int32 Index) const
{
    const UClass* Item = UShipInventoryLibrary::ItemAt(TrackedPawn.Get(), Index);
    if (!Item) return nullptr;
    if (Item == PackClasses.FindRef(TEXT("ColorLamp")).Get()) return TEXT("Zagadkowa");
    if (Item == PackClasses.FindRef(TEXT("StockLamp")).Get() || Item == PackClasses.FindRef(TEXT("White")).Get()) return TEXT("Zwykła");
    return nullptr;
}

FString AShipSurvivor::GetToolLabel() const
{
    for (int32 I = 0; I < UE_ARRAY_COUNT(PackKinds); ++I)
        if (Equipped == PackKinds[I]) return PackLabels[I];
    if (Equipped == TEXT("Bandage")) return TEXT("Bandaż");
    if (Equipped == TEXT("FlashlightBattery")) return TEXT("Bateria latarki osobistej");
    if (Equipped == TEXT("DoorKey")) return TEXT("Klucz do drzwi");
    return HandClass ? HandClass->GetName() : TEXT("Pusta ręka");
}

FString AShipSurvivor::HandHint() const
{
    if (Equipped == TEXT("Glowstick"))
    {
        FString Hint = FString::Printf(TEXT("LPM  Rzuć · %d/%d"), GlowstickCharges, MaxGlowsticks);
        if (NextGlowstick > 0.)
            Hint += FString::Printf(TEXT(" · kolejny za %d s"), FMath::Max(0, FMath::CeilToInt(float(NextGlowstick - GetWorld()->GetTimeSeconds()))));
        return Hint;
    }
    if (Equipped == TEXT("Note")) return TEXT("LPM  Czytaj notatkę");
    if (Equipped == TEXT("Welder")) return TEXT("Przytrzymaj E przy spoinie");
    if (Equipped == TEXT("Bandage") || Equipped == TEXT("FlashlightBattery")) return TEXT("LPM  Użyj przedmiotu");
    if (Equipped == TEXT("Card") || Equipped == TEXT("DoorKey")) return TEXT("E  Użyj przy zamku");
    if (Equipped == TEXT("Battery")) return TEXT("Zasila spawarkę także z plecaka");
    if (Equipped == TEXT("StockLamp")) return TEXT("F  Włącz / zgaś");
    return TEXT("Tab  Wybierz przedmiot");
}

void AShipSurvivor::UpdateHandMesh()
{
    if (!TrackedPawn.IsValid()) { HandMesh->SetVisibility(false); return; }
    UCameraComponent* Camera = TrackedPawn->FindComponentByClass<UCameraComponent>();
    if (!Camera) return;
    HandMesh->AttachToComponent(Camera, FAttachmentTransformRules::SnapToTargetNotIncludingScale);
    const FName Kind = HandKind(HandClass.Get());
    UStaticMesh* Mesh = PackMeshes.FindRef(Kind);
    HandMesh->EmptyOverrideMaterials();
    HandMesh->SetStaticMesh(Mesh);
    HandMesh->SetVisibility(Mesh && HasPlayerView());
    if (!Mesh) return;
    if (Kind == TEXT("Welder"))
    {
        HandMesh->SetRelativeTransform(FTransform(FRotator(0,-5,0), FVector(29,16,-20)));
        return;
    }
    if (Kind == TEXT("Glowstick"))
    {
        HandMesh->SetRelativeTransform(FTransform(FRotator(-24,0,-12), FVector(34,17,-12), FVector(.033,.033,.21)));
        if (UMaterialInstanceDynamic* Material = HandMesh->CreateDynamicMaterialInstance(0))
            Material->SetVectorParameterValue(TEXT("Color"), FLinearColor(.10f,1.f,.48f));
        return;
    }
    const FBoxSphereBounds Bounds = Mesh->GetBounds();
    const FVector* DisplayScale = PackDisplayScales.Find(Kind);
    const FVector Scale = DisplayScale ? *DisplayScale : FVector(Kind == TEXT("DoorKey") ? 21. / FMath::Max(1., 2. * Bounds.BoxExtent.GetMax()) : 1.);
    FVector Axis = FVector::ForwardVector;
    if (Bounds.BoxExtent.Y >= Bounds.BoxExtent.X && Bounds.BoxExtent.Y > Bounds.BoxExtent.Z) Axis = FVector::RightVector;
    else if (Bounds.BoxExtent.Z > Bounds.BoxExtent.X) Axis = FVector::UpVector;
    FQuat Orientation = FQuat::FindBetweenNormals(Axis, FVector::ForwardVector);
    FVector Center(34,15,-12);
    if (Kind == TEXT("Note"))
    {
        // Tilt the face of the page towards the camera.
        Orientation = FRotator(62,-12,-5).Quaternion();
        Center = FVector(42,18,-13);
    }
    else if (Kind == TEXT("Battery") || Kind == TEXT("FlashlightBattery") || Kind == TEXT("Bandage"))
    {
        Orientation = FRotator(38,-8,0).Quaternion() * Orientation;
        Center = FVector(39,16,-13);
    }
    else if (Kind == TEXT("StockLamp") || Kind == TEXT("ColorLamp"))
        Center = FVector(37,15,-12);
    HandMesh->SetRelativeTransform(FTransform(Orientation, Center - Orientation.RotateVector(Bounds.Origin * Scale), Scale));
}

int32 AShipSurvivor::HeldKeyCount() const { return UShipInventoryLibrary::IsDoorKey(HandClass.Get()) ? HandAmount : 0; }
void AShipSurvivor::ConsumeOneInHand()
{
    if (--HandAmount > 0) return;
    HandClass = nullptr;
    Equipped = TEXT("None");
    UpdateHandMesh();
}

void AShipSurvivor::RechargeGlowsticks()
{
    if (NextGlowstick <= 0. || GlowstickCharges >= MaxGlowsticks) return;
    const double Now = GetWorld()->GetTimeSeconds();
    while (Now >= NextGlowstick && GlowstickCharges < MaxGlowsticks)
    {
        ++GlowstickCharges;
        NextGlowstick += RefillSeconds;
    }
    if (GlowstickCharges == MaxGlowsticks) NextGlowstick = 0.;
}

void AShipSurvivor::UseHeldItem()
{
    if (Equipped == TEXT("Note") && Notes.IsValidIndex(SelectedNote)) { ReadNote(Notes[SelectedNote]); return; }
    if (Equipped == TEXT("Glowstick"))
    {
        if (GlowstickCharges <= 0) { Notify(TEXT("Zapas glowsticków się odnawia.")); return; }
        FActorSpawnParameters Params;
        Params.Owner = this;
        Params.Instigator = TrackedPawn.Get();
        Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        const FVector Aim = ViewDirection();
        // Start at the eye so the initial sweep also catches walls directly in front of the player.
        AShipGlowstick* Stick = GetWorld()->SpawnActor<AShipGlowstick>(ViewOrigin(), Aim.Rotation(), Params);
        if (!Stick) return;
        Stick->Launch(Aim * 1050.f + FVector::UpVector * 110.f);
        if (GlowstickCharges-- == MaxGlowsticks) NextGlowstick = GetWorld()->GetTimeSeconds() + RefillSeconds;
        return;
    }
    if (UShipInventoryLibrary::UseStockConsumable(TrackedPawn.Get(), HandClass.Get())) ConsumeOneInHand();
}
