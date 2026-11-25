#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "ShipInventoryLibrary.generated.h"

class UActorComponent;

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

    UFUNCTION(BlueprintPure, Category="Inventory", meta=(DefaultToSelf="Door"))
    static bool CanUnlockWithKey(AActor* Door, AActor* Player);

    UFUNCTION(BlueprintCallable, Category="Inventory", meta=(DefaultToSelf="Door"))
    static bool UnlockWithKey(AActor* Door, AActor* Player);
};
