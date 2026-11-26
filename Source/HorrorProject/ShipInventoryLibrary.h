#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "ShipInventoryLibrary.generated.h"

class UActorComponent;
class UUserWidget;

UCLASS()
class HORRORPROJECT_API UShipInventoryLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:
    UFUNCTION(BlueprintCallable, Category="Inventory", meta=(DefaultToSelf="Inventory"))
    static void StoreItems(UActorComponent* Inventory, TSubclassOf<AActor> Item, int32 Amount,
        bool& bStored, int32& Remainder);

    UFUNCTION(BlueprintCallable, Category="Inventory", meta=(DefaultToSelf="Inventory"))
    static bool DropSlot(UActorComponent* Inventory, int32 Index, TSubclassOf<AActor> BandagePickup,
        TSubclassOf<AActor> BatteryPickup, TSubclassOf<AActor> KeyPickup);

    // Exchanges a whole stack with the selected slot. The stock grid remains authoritative.
    static bool SwapWithHand(AActor* Player, int32 Index, TSubclassOf<AActor>& Hand, int32& Amount);
    static bool StorePackItem(AActor* Player, UClass* Item, int32 PreferredIndex);
    static int32 SlotCount(AActor* Player);
    static UClass* ItemAt(AActor* Player, int32 Index);
    static bool ExpandBackpack(APlayerController* PC, int32 Capacity);
    static int32 FindSlot(AActor* Player, UClass* Item);
    static bool IsDoorKey(UClass* Item);
    static double StockPickupScale(const UClass* Item);
    static bool UseStockConsumable(AActor* Player, UClass* Item);
    UFUNCTION(BlueprintCallable, Category="Inventory")
    static bool EquipClickedSlot(UUserWidget* Slot, int32 Index);

    UFUNCTION(BlueprintPure, Category="Inventory", meta=(DefaultToSelf="Door"))
    static bool CanUnlockWithKey(AActor* Door, AActor* Player);

    UFUNCTION(BlueprintCallable, Category="Inventory", meta=(DefaultToSelf="Door"))
    static bool UnlockWithKey(AActor* Door, AActor* Player);
};
