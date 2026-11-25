#include "ShipInventoryLibrary.h"
#include "ShipSurvivor.h"
#include "ShipBackpackGrid.h"
#include "GameFramework/PlayerController.h"
#include "Blueprint/UserWidget.h"
#include "Framework/Application/SlateApplication.h"
#include "Kismet/GameplayStatics.h"

#include "CollisionQueryParams.h"
#include "CollisionShape.h"
#include "Components/ActorComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "TimerManager.h"
#include "UObject/Package.h"
#include "UObject/StructOnScope.h"
#include "UObject/TextProperty.h"
#include "UObject/UnrealType.h"

namespace
{
    const FName UnlockedTag(TEXT("Inventory.DoorUnlocked"));
    const FName KeyPackage(TEXT("/Game/Blueprints/Actors/Inventory/BP_InventoryItem_DoorKey"));

    template <typename PropertyType>
    PropertyType* StructField(const UStruct* Struct, const TCHAR* Name)
    {
        // User-defined struct fields on disk have a generated numeric suffix and GUID.
        for (TFieldIterator<PropertyType> It(Struct); It; ++It)
            if (It->GetAuthoredName() == Name || It->GetName().StartsWith(FString(Name) + TEXT("_")))
                return *It;
        return nullptr;
    }

    AShipSurvivor* Equipment(const UObject* Context)
    {
        return Context ? Cast<AShipSurvivor>(UGameplayStatics::GetActorOfClass(Context, AShipSurvivor::StaticClass())) : nullptr;
    }

    class FInventorySlots
    {
        FArrayProperty* Array = nullptr;
        FClassProperty* ItemField = nullptr;
        FIntProperty* AmountField = nullptr;
        UActorComponent* Component;

        FScriptArrayHelper Slots() const
        {
            return FScriptArrayHelper(Array, Array->ContainerPtrToValuePtr<void>(Component));
        }
    public:
        explicit FInventorySlots(UActorComponent* InComponent) : Component(InComponent)
        {
            if (!IsValid(Component)) return;
            Array = FindFProperty<FArrayProperty>(Component->GetClass(), TEXT("InventorySlots"));
            const FStructProperty* Slot = Array ? CastField<FStructProperty>(Array->Inner) : nullptr;
            if (!Slot) return;
            ItemField = StructField<FClassProperty>(Slot->Struct, TEXT("Item"));
            AmountField = StructField<FIntProperty>(Slot->Struct, TEXT("Amount"));
        }
        int32 Num() const { return ItemField && AmountField ? Slots().Num() : 0; }
        void Resize(int32 Count) const { Slots().Resize(Count); }
        UClass* Item(int32 Index) const { return Cast<UClass>(ItemField->GetObjectPropertyValue_InContainer(Slots().GetRawPtr(Index))); }
        int32 Amount(int32 Index) const { return AmountField->GetPropertyValue_InContainer(Slots().GetRawPtr(Index)); }
        void Set(int32 Index, UClass* Class, int32 Count) const
        {
            uint8* Slot = Slots().GetRawPtr(Index);
            ItemField->SetObjectPropertyValue_InContainer(Slot, Count > 0 ? Class : nullptr);
            AmountField->SetPropertyValue_InContainer(Slot, Count);
        }
        bool Accepts(UClass* Class) const { return ItemField && Class && Class->IsChildOf(ItemField->MetaClass); }
        static bool IsKey(const UClass* Class)
        {
            for (; Class; Class = Class->GetSuperClass())
                if (Class->GetOutermost()->GetFName() == KeyPackage) return true;
            return false;
        }
        int32 FirstKey() const
        {
            for (int32 I = 0; I < Num(); ++I)
                if (Amount(I) > 0 && IsKey(Item(I))) return I;
            return INDEX_NONE;
        }
        void RefreshKeyCount() const
        {
            AActor* Owner = Component->GetOwner();
            FByteProperty* KeyCount = Owner ? FindFProperty<FByteProperty>(Owner->GetClass(), TEXT("KeyCollected")) : nullptr;
            if (!KeyCount) return;
            const AShipSurvivor* Kit = Equipment(Owner);
            int64 Count = Kit ? Kit->HeldKeyCount() : 0;
            for (int32 I = 0; I < Num(); ++I)
                if (IsKey(Item(I))) Count += FMath::Max(0, Amount(I));
            KeyCount->SetPropertyValue_InContainer(Owner, static_cast<uint8>(FMath::Min<int64>(255, Count)));
        }
        void Refresh(int32 Index) const
        {
            const FObjectPropertyBase* Menu = FindFProperty<FObjectPropertyBase>(Component->GetClass(), TEXT("W_InventoryMenuRef"));
            if (!Menu || !IsValid(Menu->GetObjectPropertyValue_InContainer(Component))) return;
            UFunction* Update = Component->FindFunction(TEXT("Update Inventory Slot"));
            const FIntProperty* Param = Update ? FindFProperty<FIntProperty>(Update, TEXT("Index")) : nullptr;
            if (!Param) return;
            FStructOnScope Args(Update);
            Param->SetPropertyValue_InContainer(Args.GetStructMemory(), Index);
            Component->ProcessEvent(Update, Args.GetStructMemory());
        }
    };

    UActorComponent* PlayerInventory(AActor* Player)
    {
        if (!IsValid(Player)) return nullptr;
        for (UActorComponent* Component : Player->GetComponents())
            if (FInventorySlots(Component).Num() > 0) return Component;
        return nullptr;
    }

    struct FItemRules
    {
        int32 MaxStack = 0;
        bool bMayDrop = false;
        explicit FItemRules(UClass* Class)
        {
            const FStructProperty* Data = Class ? FindFProperty<FStructProperty>(Class, TEXT("ItemData")) : nullptr;
            if (!Data) return;
            const void* Values = Data->ContainerPtrToValuePtr<void>(Class->GetDefaultObject());
            if (const FIntProperty* Max = StructField<FIntProperty>(Data->Struct, TEXT("MaxStackAmount")))
                MaxStack = Max->GetPropertyValue_InContainer(Values);
            if (const FBoolProperty* Drop = StructField<FBoolProperty>(Data->Struct, TEXT("CanBeDropped")))
                bMayDrop = Drop->GetPropertyValue_InContainer(Values);
        }
    };

    UClass* MatchingPickup(UClass* Item, const TArrayView<const TSubclassOf<AActor>> Classes)
    {
        for (const TSubclassOf<AActor>& Candidate : Classes)
        {
            UClass* Class = Candidate.Get();
            const FClassProperty* ItemField = Class ? FindFProperty<FClassProperty>(Class, TEXT("Item")) : nullptr;
            if (!ItemField || Class->HasAnyClassFlags(CLASS_Abstract)) continue;
            const UClass* DefaultItem = Cast<UClass>(ItemField->GetObjectPropertyValue_InContainer(Class->GetDefaultObject()));
            if (DefaultItem && Item->IsChildOf(DefaultItem)) return Class;
        }
        return nullptr;
    }

    AActor* PlacePickup(AActor* Owner, UClass* Class, UClass* Item, int32 Amount)
    {
        UWorld* World = Owner->GetWorld();
        if (!World) return nullptr;
        const FIntProperty* AmountField = FindFProperty<FIntProperty>(Class, TEXT("Amount"));
        const FClassProperty* ItemField = FindFProperty<FClassProperty>(Class, TEXT("Item"));
        if (!AmountField || !ItemField || !Item->IsChildOf(ItemField->MetaClass)) return nullptr;

        FVector Eye;
        FRotator Look;
        Owner->GetActorEyesViewPoint(Eye, Look);
        const FVector Forward = FRotator(0., Look.Yaw, 0.).Vector();
        const FVector Right = FVector::CrossProduct(FVector::UpVector, Forward);
        const FVector Centre = Owner->GetActorLocation();
        FCollisionQueryParams Query(SCENE_QUERY_STAT(InventoryDrop), false, Owner);
        FHitResult StandingFloor;
        if (!World->LineTraceSingleByChannel(StandingFloor, Centre, Centre - FVector(0,0,210), ECC_Visibility, Query)) return nullptr;
        // Try the open space in front first, then the sides and rear. A shorter ring
        // also fits inside narrow aisles where a point 80 cm from the player cannot.
        const FVector2D Offsets[] = {
            {80,0}, {52,61}, {52,-61}, {0,80}, {0,-80}, {-56,56}, {-56,-56}, {-80,0},
            {54,0}, {38,38}, {38,-38}, {0,54}, {0,-54}, {-38,38}, {-38,-38}, {-54,0}
        };
        for (const FVector2D& OffsetXY : Offsets)
        {
            const FVector Above = Centre + Forward * OffsetXY.X + Right * OffsetXY.Y;
            if (World->SweepTestByChannel(Centre, Above, FQuat::Identity, ECC_Visibility, FCollisionShape::MakeSphere(12.f), Query)) continue;
            FHitResult Floor;
            if (!World->LineTraceSingleByChannel(Floor, Above, Above - FVector(0,0,210), ECC_Visibility, Query) || Floor.ImpactNormal.Z < .65f) continue;
            if (FMath::Abs(Floor.ImpactPoint.Z - StandingFloor.ImpactPoint.Z) > 45.) continue;
            const FTransform Transform(FRotator(0.,Look.Yaw,0.), Floor.ImpactPoint + FVector(0,0,5));
            AActor* Pickup = World->SpawnActorDeferred<AActor>(Class, Transform, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
            if (!Pickup) continue;
            AmountField->SetPropertyValue_InContainer(Pickup, Amount);
            ItemField->SetObjectPropertyValue_InContainer(Pickup, Item);
            Pickup->FinishSpawning(Transform);
            if (!IsValid(Pickup)) continue;
            UStaticMeshComponent* Visual = Pickup->FindComponentByClass<UStaticMeshComponent>();
            if (Visual && Visual->GetStaticMesh())
            {
                const FBox Bounds = Visual->Bounds.GetBox();
                const FVector Offset = Floor.ImpactPoint + FVector(0,0,2) - FVector(Bounds.GetCenter().X, Bounds.GetCenter().Y, Bounds.Min.Z);
                Pickup->AddActorWorldOffset(Offset, false, nullptr, ETeleportType::TeleportPhysics);
                const FBox Placed = Visual->Bounds.GetBox();
                FCollisionQueryParams Clearance = Query;
                Clearance.AddIgnoredActor(Pickup);
                const FVector Extent = Placed.GetExtent().ComponentMax(FVector(1.));
                if (FVector::Dist(Eye, Pickup->GetActorLocation()) <= 210.
                    && !World->OverlapBlockingTestByChannel(Placed.GetCenter(), FQuat::Identity, ECC_Visibility, FCollisionShape::MakeBox(Extent), Clearance)
                    && !World->LineTraceTestByChannel(Eye, Placed.GetCenter(), ECC_Visibility, Clearance))
                {
                    // Stock pickups compare their floor-level origin with the player's camera.
                    // Allow a standing player to reach this dropped item, including camera bob.
                    if (const FNumericProperty* Range = FindFProperty<FNumericProperty>(Class, TEXT("PickUpDistance")); Range && Range->IsFloatingPoint())
                    {
                        void* RangeValue = Range->ContainerPtrToValuePtr<void>(Pickup);
                        const double StandingReach = FMath::Clamp(FVector::Dist(Eye, Pickup->GetActorLocation()) + 25., 185., 220.);
                        Range->SetFloatingPointPropertyValue(RangeValue, FMath::Max(Range->GetFloatingPointPropertyValue(RangeValue), StandingReach));
                    }
                    return Pickup;
                }
            }
            Pickup->Destroy();
        }
        return nullptr;
    }

    void ShowNoDropSpace(AActor* Owner)
    {
        // W_MainHUD already binds its hint text to this property on L1_Character.
        FTextProperty* Hint = FindFProperty<FTextProperty>(Owner->GetClass(), TEXT("HintMessage"));
        if (!Hint || !Owner->GetWorld()) return;
        const FText Message = NSLOCTEXT("StockInventory", "NoDropSpace", "Brak miejsca na podłodze. Odsuń się i spróbuj ponownie.");
        const FText Previous = Hint->GetPropertyValue_InContainer(Owner);
        if (Previous.EqualTo(Message)) return;
        Hint->SetPropertyValue_InContainer(Owner, Message);
        FTimerHandle Timer;
        Owner->GetWorldTimerManager().SetTimer(Timer, FTimerDelegate::CreateWeakLambda(Owner, [Owner, Message, Previous]()
        {
            FTextProperty* Current = FindFProperty<FTextProperty>(Owner->GetClass(), TEXT("HintMessage"));
            if (Current && Current->GetPropertyValue_InContainer(Owner).EqualTo(Message))
                Current->SetPropertyValue_InContainer(Owner, Previous);
        }), 3.f, false);
    }
}

void UShipInventoryLibrary::StoreItems(UActorComponent* Inventory, TSubclassOf<AActor> Item, int32 Amount, bool& bStored, int32& Remainder)
{
    bStored = false;
    Remainder = FMath::Max(0, Amount);
    FInventorySlots Slots(Inventory);
    if (!Remainder || !Slots.Accepts(Item.Get())) return;
    const int32 Max = FItemRules(Item.Get()).MaxStack;
    if (Max < 1) return;
    TArray<int32, TInlineAllocator<8>> Changed;
    for (int32 Pass = 0; Pass < 2 && Remainder > 0; ++Pass)
    {
        for (int32 I = 0; I < Slots.Num() && Remainder > 0; ++I)
        {
            UClass* Current = Slots.Item(I);
            if (Pass == 0 ? Current != Item.Get() : Current != nullptr) continue;
            const int32 Count = Current ? Slots.Amount(I) : 0;
            if (Count < 0 || Count >= Max) continue;
            const int32 Added = FMath::Min(Remainder, Max - Count);
            Slots.Set(I, Item.Get(), Count + Added);
            Remainder -= Added;
            Changed.Add(I);
        }
    }
    bStored = !Changed.IsEmpty();
    if (bStored && FInventorySlots::IsKey(Item.Get())) Slots.RefreshKeyCount();
    for (int32 I : Changed) Slots.Refresh(I);
}

bool UShipInventoryLibrary::DropSlot(UActorComponent* Inventory, int32 Index, TSubclassOf<AActor> BandagePickup,
    TSubclassOf<AActor> BatteryPickup, TSubclassOf<AActor> KeyPickup)
{
    FInventorySlots Slots(Inventory);
    if (Index < 0 || Index >= Slots.Num() || !IsValid(Inventory->GetOwner())) return false;
    UClass* Item = Slots.Item(Index);
    const int32 Amount = Slots.Amount(Index);
    if (!Item || Amount <= 0 || !FItemRules(Item).bMayDrop) return false;
    const TSubclassOf<AActor> Pickups[] = {BandagePickup, BatteryPickup, KeyPickup};
    UClass* PickupClass = MatchingPickup(Item, MakeArrayView(Pickups));
    if (!PickupClass) return false;
    AActor* Spawned = PlacePickup(Inventory->GetOwner(), PickupClass, Item, Amount);
    if (!Spawned)
    {
        ShowNoDropSpace(Inventory->GetOwner());
        return false;
    }
    if (!IsValid(Inventory) || Index >= Slots.Num() || Slots.Item(Index) != Item || Slots.Amount(Index) != Amount)
    {
        Spawned->Destroy();
        return false;
    }
    Slots.Set(Index, nullptr, 0);
    if (FInventorySlots::IsKey(Item)) Slots.RefreshKeyCount();
    Slots.Refresh(Index);
    return true;
}

int32 UShipInventoryLibrary::SlotCount(AActor* Player)
{
    return FInventorySlots(PlayerInventory(Player)).Num();
}

bool UShipInventoryLibrary::ExpandBackpack(APlayerController* PC, int32 Capacity)
{
    FInventorySlots Slots(PlayerInventory(PC ? PC->GetPawn() : nullptr));
    const int32 OldCount = Slots.Num();
    if (OldCount <= 0 || OldCount >= Capacity) return false;
    Slots.Resize(Capacity);
    for (int32 I = OldCount; I < Capacity; ++I) Slots.Set(I, nullptr, 0);
    if (!ShipBackpackGrid::Expand(PC, OldCount, Capacity))
    {
        Slots.Resize(OldCount);
        return false;
    }
    for (int32 I = OldCount; I < Capacity; ++I) Slots.Refresh(I);
    return true;
}

bool UShipInventoryLibrary::StorePackItem(AActor* Player, UClass* Item, int32 PreferredIndex)
{
    FInventorySlots Slots(PlayerInventory(Player));
    if (!Slots.Accepts(Item)) return false;
    int32 Index = PreferredIndex;
    if (Index < 0 || Index >= Slots.Num() || Slots.Item(Index))
    {
        for (Index = 0; Index < Slots.Num() && Slots.Item(Index); ++Index) {}
        if (Index == Slots.Num()) return false;
    }
    Slots.Set(Index, Item, 1);
    Slots.Refresh(Index);
    return true;
}

int32 UShipInventoryLibrary::FindSlot(AActor* Player, UClass* Item)
{
    FInventorySlots Slots(PlayerInventory(Player));
    for (int32 I = 0; I < Slots.Num(); ++I)
        if (Slots.Item(I) == Item) return I;
    return INDEX_NONE;
}

bool UShipInventoryLibrary::SwapWithHand(AActor* Player, int32 Index, TSubclassOf<AActor>& Hand, int32& Amount)
{
    FInventorySlots Slots(PlayerInventory(Player));
    if (Index < 0 || Index >= Slots.Num() || (Hand && !Slots.Accepts(Hand.Get()))) return false;
    UClass* Selected = Slots.Item(Index);
    const int32 Count = Slots.Amount(Index);
    if (!Hand && !Selected) return false;
    const bool bKey = Slots.IsKey(Selected) || Slots.IsKey(Hand.Get());
    Slots.Set(Index, Hand.Get(), Amount);
    Hand = Selected;
    Amount = Count;
    if (bKey) Slots.RefreshKeyCount();
    Slots.Refresh(Index);
    return true;
}

bool UShipInventoryLibrary::EquipClickedSlot(UUserWidget* Slot, int32 Index)
{
    // Shift-click retains the pack's original Use / Drop context menu.
    if (!Slot || FSlateApplication::Get().GetModifierKeys().IsShiftDown()) return false;
    AShipSurvivor* Kit = Equipment(Slot);
    return Kit && Kit->IsStockInventoryVisible() && Kit->EquipSlot(Index);
}

bool UShipInventoryLibrary::IsDoorKey(UClass* Item) { return FInventorySlots::IsKey(Item); }

bool UShipInventoryLibrary::UseStockConsumable(AActor* Player, UClass* Item)
{
    if (!Player || !Item) return false;
    AActor* Action = Player->GetWorld()->SpawnActor<AActor>(Item);
    if (!Action) return false;
    UFunction* Use = Action->FindFunction(TEXT("UseItem"));
    FBoolProperty* Success = FindFProperty<FBoolProperty>(Item, TEXT("UseItemSuccess"));
    if (Use && Use->NumParms == 0 && Success) Action->ProcessEvent(Use, nullptr);
    const bool bUsed = Success && Success->GetPropertyValue_InContainer(Action);
    Action->Destroy();
    return bUsed;
}

bool UShipInventoryLibrary::CanUnlockWithKey(AActor* Door, AActor* Player)
{
    const AShipSurvivor* Kit = Equipment(Player);
    return IsValid(Door) && (Door->ActorHasTag(UnlockedTag) || (Kit && Kit->HeldKeyCount() > 0)
        || FInventorySlots(PlayerInventory(Player)).FirstKey() != INDEX_NONE);
}

bool UShipInventoryLibrary::UnlockWithKey(AActor* Door, AActor* Player)
{
    if (!IsValid(Door)) return false;
    if (Door->ActorHasTag(UnlockedTag)) return true;
    FInventorySlots Slots(PlayerInventory(Player));
    if (AShipSurvivor* Kit = Equipment(Player); Kit && Kit->HeldKeyCount() > 0)
    {
        Kit->ConsumeOneInHand();
        Door->Tags.AddUnique(UnlockedTag);
        Slots.RefreshKeyCount();
        return true;
    }
    const int32 I = Slots.FirstKey();
    if (I == INDEX_NONE) return false;
    Slots.Set(I, Slots.Item(I), Slots.Amount(I) - 1);
    Door->Tags.AddUnique(UnlockedTag);
    Slots.RefreshKeyCount();
    Slots.Refresh(I);
    return true;
}
