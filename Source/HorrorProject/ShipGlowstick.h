#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ShipGlowstick.generated.h"

class UBoxComponent;
class UMaterialInstanceDynamic;
class UPointLightComponent;
class UProjectileMovementComponent;
class UStaticMeshComponent;

UCLASS()
class HORRORPROJECT_API AShipGlowstick : public AActor
{
    GENERATED_BODY()

public:
    AShipGlowstick();

    /** Launch in world space, in cm/s. Set the owner or instigator in the spawn parameters. */
    UFUNCTION(BlueprintCallable, Category="Glowstick")
    void Launch(FVector Velocity);

    virtual void Tick(float DeltaSeconds) override;

protected:
    virtual void BeginPlay() override;

private:
    UPROPERTY(VisibleAnywhere, Category="Glowstick") TObjectPtr<UBoxComponent> Collision;
    UPROPERTY(VisibleAnywhere, Category="Glowstick") TObjectPtr<UProjectileMovementComponent> Projectile;
    UPROPERTY(VisibleAnywhere, Category="Glowstick") TObjectPtr<UPointLightComponent> Light;
    UPROPERTY() TObjectPtr<UStaticMeshComponent> Tube;
    UPROPERTY() TObjectPtr<UStaticMeshComponent> FrontCap;
    UPROPERTY() TObjectPtr<UStaticMeshComponent> RearCap;
    UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> TubeMaterial;

    UFUNCTION()
    void Settle(const FHitResult& Hit);

    void SetBrightness(float Fraction);
};
