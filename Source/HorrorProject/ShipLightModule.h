#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ShipLightModule.generated.h"

class AShipSurvivor;
class UShipModulePhysics;
class UStaticMeshComponent;
class UMaterialInstanceDynamic;

/** Local X is the working face. The same receiver can extend, hold cargo, rotate and launch. */
UCLASS(Blueprintable)
class HORRORPROJECT_API AShipLightModule : public AActor
{
    GENERATED_BODY()
public:
    AShipLightModule();
    virtual void OnConstruction(const FTransform& Transform) override;
    virtual void Tick(float DeltaSeconds) override;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Module", meta=(Units="cm"))
    FVector BlockSize = FVector(30,112,100);
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Module", meta=(ClampMin="0", Units="cm"))
    float Travel = 96.f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Module") bool bGuide = false;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Module") TObjectPtr<UStaticMeshComponent> Block;
    UPROPERTY(BlueprintReadOnly, Category="Module") bool bExtended = false;
    UPROPERTY(BlueprintReadOnly, Category="Module") float Extension = 0.f;
    UPROPERTY(BlueprintReadOnly, Category="Module") int32 QuarterTurns = 0;
    UFUNCTION(BlueprintPure, Category="Module") bool HasCargo() const;
    UFUNCTION(BlueprintPure, Category="Module") int32 GetLaunchCount() const;

protected:
    virtual void BeginPlay() override;
private:
    UPROPERTY() TObjectPtr<USceneComponent> Carriage;
    UPROPERTY() TObjectPtr<USceneComponent> Track;
    UPROPERTY() TObjectPtr<UStaticMeshComponent> Ramp;
    UPROPERTY() TObjectPtr<UStaticMeshComponent> LeftRail;
    UPROPERTY() TObjectPtr<UStaticMeshComponent> RightRail;
    UPROPERTY() TObjectPtr<UStaticMeshComponent> LightStrip;
    UPROPERTY() TObjectPtr<UShipModulePhysics> Contacts;

    UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> StripFinish;
    TWeakObjectPtr<AShipSurvivor> Operator;
    FName LastColor;
    float DarkTime = 0.f;
    float Roll = 0.f;
    void ConfigureGeometry();
    bool IsFaceLit(const AShipSurvivor& Player) const;
    void ApplyColor(FName Color, const AShipSurvivor* Player);
};
