#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "ShipHUD.generated.h"

class AShipSurvivor;
class UProgressBar;

/** Base for the existing three-bar player HUD. */
UCLASS()
class HORRORPROJECT_API UShipVitalsWidget : public UUserWidget
{
    GENERATED_BODY()
public:
    UFUNCTION(BlueprintPure, Category="Ship|HUD")
    float DisplayedBatteryPercent(float StockPercent) const;

protected:
    virtual void NativeOnInitialized() override;
    virtual void NativeConstruct() override;
    virtual void NativeTick(const FGeometry& Geometry, float DeltaTime) override;

    UPROPERTY(meta=(BindWidget)) TObjectPtr<UProgressBar> BatteryBar;

private:
    TWeakObjectPtr<AShipSurvivor> Kit;
    FLinearColor StockBatteryTint = FLinearColor::White;
};

/** Draw the quick-select number on each of the stock backpack's actual buttons. */
UCLASS()
class HORRORPROJECT_API UShipNumberedSlot : public UUserWidget
{
    GENERATED_BODY()
protected:
    virtual int32 NativePaint(const FPaintArgs& Args, const FGeometry& Geometry, const FSlateRect& Clip,
        FSlateWindowElementList& List, int32 Layer, const FWidgetStyle& Style, bool bEnabled) const override;
};

/** Hit-test transparent overlay for the existing player HUD. */
UCLASS()
class HORRORPROJECT_API UShipHUDOverlay : public UUserWidget
{
    GENERATED_BODY()
public:
    void SetSurvivor(AShipSurvivor* InSurvivor);

protected:
    virtual TSharedRef<SWidget> RebuildWidget() override;

private:
    TWeakObjectPtr<AShipSurvivor> Survivor;
};
