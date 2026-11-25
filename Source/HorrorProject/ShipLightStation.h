#pragma once

#include "ShipFixture.h"
#include "ShipLightStation.generated.h"

UENUM(BlueprintType)
enum class ELightStationMode : uint8
{
    Latch, Shutter, Sequence, Memory, Dark, Switch, Alarm, Dynamo
};

/** Optional receiver. Only doors on the same non-empty Channel receive its signal. */
UCLASS(Blueprintable)
class HORRORPROJECT_API AShipLightStation : public AShipFixture
{
    GENERATED_BODY()

public:
    AShipLightStation();
    virtual void Tick(float DeltaSeconds) override;
    virtual FString Prompt(AShipSurvivor* Survivor) const override;
    virtual void Use(AShipSurvivor* Survivor) override;
    virtual void HeldUse(AShipSurvivor* Survivor, float DeltaSeconds) override;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Light Station") ELightStationMode Mode = ELightStationMode::Latch;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Light Station") bool bStationEnabled = true;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Light Station", meta=(ClampMin="1",ClampMax="5")) int32 MinimumPower = 1;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Light Station", meta=(ClampMin="0.1",Units="s")) float ExposureSeconds = 1.6f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Light Station", meta=(ClampMin="0.1",Units="s")) float HoldSeconds = 3.f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Light Station", meta=(ClampMin="1.0",Units="cm")) float ProximityRadius = 450.f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Light Station") FName AlternateColor = TEXT("Red");
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Light Station") TArray<FName> ColorSequence;

protected:
    virtual void BeginPlay() override;

private:
    int32 NextColor = 0;
    FName LastIncidentColor;
    double ActiveUntil = -1.;
    bool bInitializedSignal = false;
    FString LastDisplay;

    FName PrimaryColor() const;
    FName ClosingColor() const;
    int32 SecondsRemaining() const;
    void SetSignal(bool bOn);
    void SetDisplay(const FString& Text);
    void ReadSequence(FName Incident, double Now);
    bool AccumulateExposure(bool bExposed, float DeltaSeconds);
};
