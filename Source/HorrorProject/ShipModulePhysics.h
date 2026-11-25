#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Components/PrimitiveComponent.h"
#include "ShipModulePhysics.generated.h"

class UStaticMeshComponent;

/** Contact effects on the local +X face of a light module. The owning module drives updates. */
UCLASS(ClassGroup=(Ship), meta=(BlueprintSpawnableComponent))
class HORRORPROJECT_API UShipModulePhysics : public UActorComponent
{
    GENERATED_BODY()

public:
    UShipModulePhysics();
    void SetSurface(UStaticMeshComponent* InSurface);
    void SetLight(FName Color);
    void UpdateContacts(float DeltaSeconds);
    float MakeRoomForExtension(const FVector& Direction, float Distance);
    bool HasCargo() const;
    int32 GetLaunchCount() const { return LaunchCount; }

    UPROPERTY(EditAnywhere, Category="Ship|Light Module", meta=(ClampMin="1", Units="cm/s"))
    float LaunchSpeed = 850.f;

protected:
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;

private:
    struct FAttachedCargo
    {
        TWeakObjectPtr<UPrimitiveComponent> Body;
        ECanBeCharacterBase PreviousStepUp;
        ECollisionResponse PreviousPawnResponse;
    };

    TWeakObjectPtr<UStaticMeshComponent> Surface;
    TArray<FAttachedCargo> Cargo;
    TSet<TWeakObjectPtr<UPrimitiveComponent>> BlueContacts;
    TMap<TWeakObjectPtr<UPrimitiveComponent>, double> LaunchCooldowns;
    FName ActiveLight;
    float TimeUntilCheck = 0.f;
    int32 LaunchCount = 0;

    void Stick(UPrimitiveComponent* Body, const FVector& Face, const FVector& Normal);
    void ReleaseCargo();
};
