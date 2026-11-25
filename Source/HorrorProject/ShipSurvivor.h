#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ShipSurvivor.generated.h"

class AShipFixture;
class AShipLadder;
class IInputProcessor;
class UAudioComponent;
class UPhysicsHandleComponent;
class UPrimitiveComponent;
class UShipHUDOverlay;
class USoundBase;
class USpotLightComponent;

UCLASS()
class HORRORPROJECT_API AShipSurvivor : public AActor
{
    GENERATED_BODY()
public:
    AShipSurvivor();
    virtual void Tick(float DeltaSeconds) override;

    UPROPERTY(BlueprintReadOnly, Category="Ship|Inventory") bool bHasWhite = false;
    UPROPERTY(BlueprintReadOnly, Category="Ship|Inventory") bool bHasColorLamp = false;
    UPROPERTY(BlueprintReadOnly, Category="Ship|Inventory") bool bHasWelder = false;
    UPROPERTY(BlueprintReadOnly, Category="Ship|Inventory") bool bHasBattery = false;
    UPROPERTY(BlueprintReadOnly, Category="Ship|Inventory") bool bHasCard = false;
    UPROPERTY(BlueprintReadOnly, Category="Ship|Equipment") int32 PowerLevel = 3;
    UPROPERTY(BlueprintReadOnly, Category="Ship|Equipment") int32 RadiusLevel = 3;
    UPROPERTY(BlueprintReadOnly, Category="Ship|Equipment") bool bLampOn = false;
    UPROPERTY(BlueprintReadOnly, Category="Ship|Equipment") bool bWelding = false;
    UPROPERTY(BlueprintReadOnly, Category="Ship|Equipment") FName Filter = TEXT("White");
    UPROPERTY(BlueprintReadOnly, Category="Ship|Equipment") FString Equipped = TEXT("White");
    UPROPERTY(BlueprintReadOnly, Category="Ship|Equipment") float Charge = 0.f;
    UPROPERTY(BlueprintReadOnly, Category="Ship|Player") float Stamina = 100.f;
    UPROPERTY(BlueprintReadOnly, Category="Ship|Player") bool bDead = false;
    UPROPERTY(BlueprintReadOnly, Category="Ship|Player") bool bEscaped = false;
    UPROPERTY(BlueprintReadOnly, Category="Ship|Player") bool bKitEnabled = false;
    UPROPERTY(BlueprintReadOnly, Category="Ship|Journal") FString LastNote;
    UPROPERTY(BlueprintReadOnly, Category="Ship|Journal") TArray<FString> Notes;

    UFUNCTION(BlueprintCallable) void Notify(const FString& Message);
    UFUNCTION(BlueprintCallable) void OpenKeypad(AShipFixture* Fixture);
    UFUNCTION(BlueprintCallable) void ReadNote(const FString& Text);
    UFUNCTION(BlueprintCallable) void Die();
    UFUNCTION(BlueprintCallable) void Escape();
    UFUNCTION(BlueprintCallable) void SetWelding(bool bActive);
    UFUNCTION(BlueprintPure) bool BeamReaches(const FVector& Point) const;
    bool BeamReachesTarget(const FVector& Point, const AActor* Receiver) const;
    UFUNCTION(BlueprintPure) float GetLampDrainPerSecond() const;
    UFUNCTION(BlueprintPure) FVector ViewOrigin() const;
    UFUNCTION(BlueprintPure) FVector ViewDirection() const;
    UFUNCTION(BlueprintPure) bool IsOnLadder() const;

    bool IsInventoryOpen() const { return bInventoryOpen; }
    UFUNCTION(BlueprintPure, Category="Ship|Keypad") bool IsKeypadOpen() const { return Keypad.IsValid(); }
    bool IsDragging() const;
    bool IsBaseMenuActive() const;
    bool HandleModalShortcut(FKey Key, bool bRepeat);
    int32 NoteIndex() const { return SelectedNote; }
    int32 NoteScroll() const { return ScrollLines; }
    void ClampNoteScroll(int32 Maximum);
    const FString& GetCode() const { return EnteredCode; }
    UFUNCTION(BlueprintPure, Category="Ship|Keypad") int32 GetCodeLength() const;
    FString GetPrompt() const;
    UFUNCTION(BlueprintPure, Category="Ship|Feedback") FString GetNotice() const;
    FString GetFilterLabel() const;
    FString GetToolLabel() const;
    FLinearColor GetFilterColor() const;

protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
private:
    UPROPERTY(VisibleAnywhere) TObjectPtr<USpotLightComponent> Beam;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UAudioComponent> WelderAudio;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UPhysicsHandleComponent> Grabber;
    UPROPERTY() TObjectPtr<USoundBase> FeedbackSound;
    UPROPERTY() TObjectPtr<UShipHUDOverlay> Overlay;
    TSharedPtr<IInputProcessor> ModalShortcuts;
    TWeakObjectPtr<APlayerController> LocalPC;
    TWeakObjectPtr<APawn> TrackedPawn;
    TWeakObjectPtr<UActorComponent> MovementStats;
    TWeakObjectPtr<USpotLightComponent> OriginalLight;
    TWeakObjectPtr<AShipFixture> Focus;
    TWeakObjectPtr<AShipFixture> HeldFixture;
    TWeakObjectPtr<AShipFixture> Keypad;
    TWeakObjectPtr<AShipLadder> MountedLadder;
    TWeakObjectPtr<UPrimitiveComponent> FocusCrate;
    FVector FocusHit = FVector::ZeroVector;
    FName LastFilter = TEXT("Red");
    FString EnteredCode;
    FString Notice;
    double NoticeUntil = 0.;
    float GrabDistance = 0.f;
    float GrabHeight = 0.f;
    bool bOriginalLightVisible = false;
    bool bOwnUse = false;
    bool bLadderUseDown = false;
    bool bWarnedLowCharge = false;
    bool bOwnDrag = false;
    bool bInventoryOpen = false;
    bool bLockedControls = false;
    int32 SelectedNote = INDEX_NONE;
    int32 ScrollLines = 0;

    void BindToPlayer(APlayerController* PC);
    void ConfigureInput();
    void UpdateInputRouting();
    void SyncPawn();
    void ToggleKit();
    void SetKit(bool bEnable);
    void RefreshFocus();
    void EndUse();
    void ReleaseInteraction();
    void LeaveLadder();
    void JumpFromLadder();
    void StartUse();
    void StartDrag();
    void EndDrag();
    void UpdateDrag();
    void ToggleLamp();
    void CycleFilter();
    void ScrollEquipment(FKey Key);
    void ChangeFilter(int32 Step);
    void AdjustLamp(FKey Key);
    bool CanAdjustLamp() const;
    void ApplyBeamSettings();
    void UpdateBeam();
    void SetTool(const FString& Tool);
    void PressDigit(FKey Key);
    void DeleteDigit();
    void ConfirmCode();
    void ClosePanel();
    void ToggleInventory();
    void PriorNote();
    void NextNote();
    void PageUp();
    void PageDown();
    void LockControls(bool bLock);
    void Restart();
    void Beep(float Pitch = 1.f) const;
};
