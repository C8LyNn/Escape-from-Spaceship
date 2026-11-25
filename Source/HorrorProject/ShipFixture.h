#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ShipFixture.generated.h"

class AShipSurvivor;
class UBoxComponent;
class UMaterialInterface;
class UPointLightComponent;
class USoundAttenuation;
class USoundBase;
class UStaticMeshComponent;
class UTextRenderComponent;

UCLASS(Blueprintable)
class HORRORPROJECT_API AShipFixture : public AActor
{
    GENERATED_BODY()

public:
    AShipFixture();
    virtual void OnConstruction(const FTransform& Transform) override;
    virtual void Tick(float DeltaSeconds) override;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Fixture") TObjectPtr<UStaticMeshComponent> Mesh;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Fixture") TObjectPtr<UBoxComponent> Trigger;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Fixture") TObjectPtr<UTextRenderComponent> GlyphText;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Fixture") TObjectPtr<UStaticMeshComponent> PaintLayer;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Fixture") TObjectPtr<UPointLightComponent> Indicator;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Fixture") FName Kind = TEXT("Door");
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Fixture") FName Channel;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Fixture") FString Value;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Fixture") FString Caption;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Fixture") int32 Order = 0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Fixture") TObjectPtr<USoundBase> Cue;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Fixture|Scare") TObjectPtr<USoundBase> BreathCue;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Fixture|Scare") TObjectPtr<USoundBase> ImpactCue;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Fixture|Glyph") FName RequiredFilter = TEXT("UV");
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Fixture|Glyph", meta=(ClampMin="1.0")) float GlyphSize = 12.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Fixture|Glyph") TObjectPtr<UMaterialInterface> PaintMaterial;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Fixture|Glyph", meta=(Units="cm")) FVector2D PaintSize = FVector2D(38.,38.);
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Fixture|Glyph", meta=(Units="cm")) FVector2D PaintOffset = FVector2D(0.,33.);
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Fixture|Glyph", meta=(Units="cm")) FVector2D TextOffset = FVector2D(0.,-12.);
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Fixture|Glyph") bool bShowText = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Fixture|Door", meta=(Units="cm")) float SlideDistance = 235.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Fixture|Door", meta=(ClampMin="0.1", Units="s")) float TravelTime = 0.85f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Fixture|Door", meta=(ClampMin="0.25", Units="s")) float RepairTime = 3.f;

    UPROPERTY(BlueprintReadOnly, Category="Fixture|State") bool bOpen = false;
    /** The close command is still pending while a pawn or movable crate occupies the leaf's path. */
    UPROPERTY(BlueprintReadOnly, Category="Fixture|State") bool bDoorObstructed = false;
    UPROPERTY(BlueprintReadOnly, Category="Fixture|State") bool bActive = false;
    UPROPERTY(BlueprintReadOnly, Category="Fixture|State") bool bSolved = false;
    UPROPERTY(BlueprintReadOnly, Category="Fixture|State") float Progress = 0.f;
    UPROPERTY(BlueprintReadOnly, Category="Fixture|State") int32 ScareStage = 0;

    UFUNCTION(BlueprintPure, Category="Fixture") virtual FString Prompt(AShipSurvivor* Survivor) const;
    UFUNCTION(BlueprintCallable, Category="Fixture") virtual void Use(AShipSurvivor* Survivor);
    UFUNCTION(BlueprintCallable, Category="Fixture") virtual void HeldUse(AShipSurvivor* Survivor, float DeltaSeconds);
    UFUNCTION(BlueprintCallable, Category="Fixture") void StopUse(AShipSurvivor* Survivor);
    UFUNCTION(BlueprintCallable, Category="Fixture") void SubmitCode(AShipSurvivor* Survivor, const FString& Code);

protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    void SetIndicator(bool bLit, FLinearColor Color);
    void BroadcastDoors(bool bShouldOpen, bool bLockClosed = false);
    void PlayCue(float Volume = 1.f, float Pitch = 1.f, USoundBase* Override = nullptr);
    void PlayFeedback(float Volume, float Pitch);
    AShipSurvivor* FindSurvivor();

private:
    TWeakObjectPtr<AShipSurvivor> SurvivorManager;
    TWeakObjectPtr<AShipSurvivor> CodeUser;
    TWeakObjectPtr<AShipSurvivor> RepairUser;
    UPROPERTY(Transient) TObjectPtr<USoundAttenuation> LocalAttenuation;
    UPROPERTY() TObjectPtr<USoundBase> FeedbackTone;
    FVector ClosedLocation = FVector::ZeroVector;
    FVector DoorOffset = FVector::ZeroVector;
    float DoorFraction = 0.f;
    int32 PlateGroupSize = 1;
    bool bOccupied = false;
    bool bStepComplete = false;
    bool bInterlockClosed = false;
    double NextScareSound = 0.0;

    void Configure();
    void ChangeDoor(bool bShouldOpen);
    bool ClosingPathOccupied(const FVector& Step) const;
    void SetDoorObstructed(bool bObstructed);
    TArray<AShipFixture*> PlateGroup() const;
    int32 UpdatePlateProgress(const TArray<AShipFixture*>& Plates);
    void EnterPlate(AShipSurvivor* Survivor);
    void CompletePlates(const TArray<AShipFixture*>& Plates, AShipSurvivor* Survivor);
    void BeginScare(AShipSurvivor* Survivor);
    void ActivateExit(AShipSurvivor* Survivor);
    void CheckVolume(AShipSurvivor* Survivor);
    bool VolumeOccupied(bool bAllowCrates) const;
    bool HasBridgeAuthorization() const;
    bool CanWeld(const AShipSurvivor* Survivor) const;
};
