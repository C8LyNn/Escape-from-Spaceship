#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "ShipHUD.generated.h"

class AShipSurvivor;

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