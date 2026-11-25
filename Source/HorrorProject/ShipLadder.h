#pragma once

#include "ShipFixture.h"
#include "ShipLadder.generated.h"

class ACharacter;
class UInstancedStaticMeshComponent;

/** Origin at the bottom rung, local +X toward the climber; a landing goes behind the top. */
UCLASS(Blueprintable)
class HORRORPROJECT_API AShipLadder : public AShipFixture
{
    GENERATED_BODY()
public:
    AShipLadder();
    virtual void OnConstruction(const FTransform& Transform) override;
    virtual FString Prompt(AShipSurvivor* Survivor) const override;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Ladder", meta=(ClampMin="80",ClampMax="3000",Units="cm")) float ClimbHeight = 280.f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Ladder", meta=(ClampMin="20",Units="cm/s")) float ClimbSpeed = 165.f;

    bool IsAtLanding(const ACharacter* Character) const;
    bool CanMount(const ACharacter* Character) const;
    bool TryMount(ACharacter* Character);
    void Climb(float Axis, float DeltaSeconds);
    void Release(bool bJump = false);
    bool HasRider() const { return Rider.IsValid(); }

protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
private:
    UPROPERTY(VisibleAnywhere, Category="Ladder") TObjectPtr<UInstancedStaticMeshComponent> Rails;
    UPROPERTY(VisibleAnywhere, Category="Ladder") TObjectPtr<UInstancedStaticMeshComponent> Rungs;
    TWeakObjectPtr<ACharacter> Rider;
    bool bLandingBlocked = false;
    void BuildLadder();
    FVector FeetLocal(const ACharacter* Character) const;
    FVector PositionAt(const ACharacter* Character, float Height, bool bOnDeck) const;
    bool PathClear(const ACharacter* Character, FVector Destination) const;
};
