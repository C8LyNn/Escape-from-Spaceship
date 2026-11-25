#include "ShipBackpackGrid.h"
#include "ShipInventoryLibrary.h"
#include "Blueprint/UserWidget.h"
#include "Components/UniformGridPanel.h"
#include "Components/UniformGridSlot.h"
#include "GameFramework/Character.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/StructOnScope.h"
#include "UObject/UnrealType.h"

namespace ShipBackpackGrid
{
    namespace
    {
        constexpr int32 MaxCapacity = 3 * PageSize;

        bool IsCapacity(int32 Count)
        {
            return Count >= PageSize && Count <= MaxCapacity && Count % PageSize == 0;
        }

        template <typename T>
        T* WidgetReference(UObject* Owner, const TCHAR* Name)
        {
            const FObjectPropertyBase* Field = IsValid(Owner)
                ? FindFProperty<FObjectPropertyBase>(Owner->GetClass(), Name) : nullptr;
            return Field ? Cast<T>(Field->GetObjectPropertyValue_InContainer(Owner)) : nullptr;
        }

        struct FStockGrid
        {
            UUserWidget* Menu = nullptr;
            UUserWidget* Grid = nullptr;
            UUniformGridPanel* Panel = nullptr;
            FArrayProperty* SlotsArray = nullptr;
            FObjectPropertyBase* SlotObject = nullptr;
            TArray<UUserWidget*, TInlineAllocator<MaxCapacity>> Widgets;
            FIntProperty* Index = nullptr;
            FObjectPropertyBase* PlayerRef = nullptr;
            UFunction* Initialize = nullptr;
            FObjectPropertyBase* MenuArgument = nullptr;

            FScriptArrayHelper Slots() const
            {
                return FScriptArrayHelper(SlotsArray, SlotsArray->ContainerPtrToValuePtr<void>(Grid));
            }

            bool Read(APlayerController* PC)
            {
                Menu = WidgetReference<UUserWidget>(PC, TEXT("W_InventoryMenuRef"));
                Grid = WidgetReference<UUserWidget>(Menu, TEXT("W_InventoryGrid"));
                Panel = WidgetReference<UUniformGridPanel>(Grid, TEXT("InventoryGridPanel"));
                if (!IsValid(Panel)) return false;

                SlotsArray = FindFProperty<FArrayProperty>(Grid->GetClass(), TEXT("SlotsArray"));
                SlotObject = SlotsArray ? CastField<FObjectPropertyBase>(SlotsArray->Inner) : nullptr;
                if (!SlotObject) return false;
                FScriptArrayHelper Items = Slots();
                if (!IsCapacity(Items.Num())) return false;

                for (int32 I = 0; I < Items.Num(); ++I)
                {
                    UUserWidget* Widget = Cast<UUserWidget>(SlotObject->GetObjectPropertyValue(Items.GetRawPtr(I)));
                    if (!IsValid(Widget) || Widget->GetParent() != Panel || !Cast<UUniformGridSlot>(Widget->Slot))
                        return false;
                    if (I == 0) Index = FindFProperty<FIntProperty>(Widget->GetClass(), TEXT("Index"));
                    else if (Widget->GetClass() != Widgets[0]->GetClass()) return false;
                    if (!Index || Index->GetPropertyValue_InContainer(Widget) != I) return false;
                    Widgets.Add(Widget);
                }

                UClass* SlotClass = Widgets[0]->GetClass();
                PlayerRef = FindFProperty<FObjectPropertyBase>(SlotClass, TEXT("Player Ref"));
                Initialize = Widgets[0]->FindFunction(TEXT("Initialize"));
                MenuArgument = Initialize ? FindFProperty<FObjectPropertyBase>(Initialize, TEXT("InventoryMenu")) : nullptr;
                // The stock slot event obtains player character 0 and casts it before setting both references.
                ACharacter* Character = UGameplayStatics::GetPlayerCharacter(PC, 0);
                return IsValid(Character) && Character == PC->GetPawn() && PlayerRef && Character->IsA(PlayerRef->PropertyClass)
                    && MenuArgument && Initialize->NumParms == 1 && MenuArgument->HasAnyPropertyFlags(CPF_Parm)
                    && Menu->IsA(MenuArgument->PropertyClass);
            }

            void InitializeSlot(UUserWidget* Widget) const
            {
                FStructOnScope Args(Initialize);
                MenuArgument->SetObjectPropertyValue_InContainer(Args.GetStructMemory(), Menu);
                Widget->ProcessEvent(Initialize, Args.GetStructMemory());
            }
        };
    }

    FString CapacityLabel(int32 Count)
    {
        return FString::Printf(TEXT("%d %s"), Count, Count == 24 ? TEXT("pola") : TEXT("pól"));
    }

    bool Expand(APlayerController* PC, int32 ExistingCapacity, int32 TargetCapacity)
    {
        if (!IsValid(PC) || !IsCapacity(ExistingCapacity) || !IsCapacity(TargetCapacity) ||
            TargetCapacity < ExistingCapacity || UShipInventoryLibrary::SlotCount(PC->GetPawn()) != TargetCapacity)
            return false;

        FStockGrid Stock;
        if (!Stock.Read(PC) || Stock.Widgets.Num() < ExistingCapacity) return false;

        // A controller may keep its menu across pawns. Only append missing widgets to its actual grid.
        TArray<TStrongObjectPtr<UUserWidget>, TInlineAllocator<MaxCapacity>> NewWidgets;
        for (int32 I = Stock.Widgets.Num(); I < TargetCapacity; ++I)
        {
            UUserWidget* Widget = CreateWidget<UUserWidget>(PC, Stock.Widgets[0]->GetClass());
            if (!Widget) return false;
            NewWidgets.Emplace(Widget);
            Stock.Index->SetPropertyValue_InContainer(Widget, I);
            Widget->SetVisibility(ESlateVisibility::Collapsed);
            Stock.InitializeSlot(Widget);
            if (Stock.PlayerRef->GetObjectPropertyValue_InContainer(Widget) != PC->GetPawn()) return false;
        }

        for (int32 I = 0; I < NewWidgets.Num(); ++I)
        {
            const int32 Cell = (Stock.Widgets.Num() + I) % PageSize;
            const UUniformGridSlot* Template = CastChecked<UUniformGridSlot>(Stock.Widgets[Cell]->Slot);
            UUniformGridSlot* Layout = Stock.Panel->AddChildToUniformGrid(NewWidgets[I].Get(), Template->GetRow(), Template->GetColumn());
            if (!Layout)
            {
                for (int32 Added = 0; Added < I; ++Added) NewWidgets[Added]->RemoveFromParent();
                return false;
            }
            Layout->SetHorizontalAlignment(Template->GetHorizontalAlignment());
            Layout->SetVerticalAlignment(Template->GetVerticalAlignment());
        }

        // Publish the whole batch together so BPC refresh and all menu actions use absolute slot indices.
        FScriptArrayHelper Items = Stock.Slots();
        for (const TStrongObjectPtr<UUserWidget>& Widget : NewWidgets)
        {
            const int32 I = Items.AddValue();
            Stock.SlotObject->SetObjectPropertyValue(Items.GetRawPtr(I), Widget.Get());
        }
        for (int32 I = ExistingCapacity; I < FMath::Min(TargetCapacity, Stock.Widgets.Num()); ++I)
            Stock.InitializeSlot(Stock.Widgets[I]);
        return true;
    }

    bool ShowPage(APlayerController* PC, int32 Page)
    {
        if (!IsValid(PC) || Page < 0 || Page >= UShipInventoryLibrary::SlotCount(PC->GetPawn()) / PageSize)
            return false;

        FStockGrid Stock;
        if (!Stock.Read(PC) || Page >= Stock.Widgets.Num() / PageSize) return false;
        UFunction* CloseMenu = Stock.Menu->FindFunction(TEXT("CloseDropDownMenu"));
        UFunction* HideInfo = Stock.Menu->FindFunction(TEXT("HideItemInfo"));
        if (!CloseMenu || CloseMenu->NumParms || !HideInfo || HideInfo->NumParms) return false;
        UUserWidget* Dropdown = WidgetReference<UUserWidget>(Stock.Menu, TEXT("W_InventoryDropdown"));
        const FObjectPropertyBase* DropdownPlayer = IsValid(Dropdown)
            ? FindFProperty<FObjectPropertyBase>(Dropdown->GetClass(), TEXT("Player Ref")) : nullptr;
        if (!DropdownPlayer || !PC->GetPawn()->IsA(DropdownPlayer->PropertyClass)) return false;

        Stock.Menu->ProcessEvent(CloseMenu, nullptr);
        Stock.Menu->ProcessEvent(HideInfo, nullptr);
        // The persistent dropdown has its own player reference for Use and Drop.
        DropdownPlayer->SetObjectPropertyValue_InContainer(Dropdown, PC->GetPawn());
        const ESlateVisibility Shown = Stock.Widgets[0]->GetClass()->GetDefaultObject<UUserWidget>()->GetVisibility();
        for (int32 I = 0; I < Stock.Widgets.Num(); ++I)
        {
            UUserWidget* Widget = Stock.Widgets[I];
            const bool bOnPage = I / PageSize == Page;
            if (bOnPage && Stock.PlayerRef->GetObjectPropertyValue_InContainer(Widget) != PC->GetPawn())
                Stock.InitializeSlot(Widget);
            Widget->SetVisibility(bOnPage ? Shown : ESlateVisibility::Collapsed);
        }
        return true;
    }
}
