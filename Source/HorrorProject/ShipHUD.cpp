#include "ShipHUD.h"

#include "ShipSurvivor.h"
#include "Components/ProgressBar.h"
#include "Kismet/GameplayStatics.h"
#include "Framework/Application/SlateApplication.h"
#include "Fonts/FontMeasure.h"
#include "Rendering/DrawElements.h"
#include "Rendering/SlateRenderer.h"
#include "Styling/CoreStyle.h"
#include "Widgets/SLeafWidget.h"
#include "UObject/UnrealType.h"

namespace
{
    const FLinearColor Ink(.91f,.96f,.96f);
    const FLinearColor Soft(.56f,.69f,.72f);
    const FLinearColor Accent(.25f,.84f,.73f);
    const FLinearColor Warning(1.f,.49f,.20f);
    const FLinearColor Panel(.012f,.024f,.033f,.97f);
    const FLinearColor Rule(.095f,.145f,.165f,.95f);
    const FLinearColor Strip(.015f,.032f,.04f,.86f);

    class FShipCanvas
    {
    public:
        FShipCanvas(const FGeometry& InGeometry, FSlateWindowElementList& InList, int32 InLayer)
            : Geometry(InGeometry), List(InList), Layer(InLayer),
              Measure(FSlateApplication::Get().GetRenderer()->GetFontMeasureService())
        {
            const FVector2D Size = Geometry.GetLocalSize();
            Scale = FMath::Max(.04f, float(FMath::Min(Size.X / 1280., Size.Y / 720.)));
            W = Size.X / Scale; H = Size.Y / Scale;
        }
        float W, H;
        int32 TopLayer() const { return Layer; }
        void Rect(float X, float Y, float Width, float Height, const FLinearColor& Color)
        {
            FSlateDrawElement::MakeBox(List, ++Layer,
                Geometry.ToPaintGeometry(FVector2f(Width * Scale,Height * Scale),FSlateLayoutTransform(FVector2f(X * Scale,Y * Scale))),
                FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")),ESlateDrawEffect::None,Color);
        }
        void Text(const FString& String, float X, float Y, float Size, const FLinearColor& Color, bool bBold = false)
        {
            const FSlateFontInfo Font = Face(Size,bBold);
            FSlateDrawElement::MakeText(List, ++Layer,
                Geometry.ToPaintGeometry(FVector2f(W * Scale,H * Scale),FSlateLayoutTransform(FVector2f(X * Scale,Y * Scale))),
                String,Font,ESlateDrawEffect::None,Color);
        }
        float TextWidth(const FString& String, float Size, bool bBold = false) const
        {
            return Measure->Measure(String,Face(Size,bBold)).X / Scale;
        }
        TArray<FString> Lines(const FString& Value,float Size,float MaxWidth) const
        {
            TArray<FString> Paragraphs, Result;
            Value.ParseIntoArrayLines(Paragraphs,false);
            for (const FString& Para : Paragraphs)
            {
                TArray<FString> Words;
                Para.ParseIntoArrayWS(Words);
                FString Line;
                for (const FString& Word : Words)
                {
                    const FString Candidate = Line.IsEmpty() ? Word : Line + TEXT(" ") + Word;
                    if (!Line.IsEmpty() && TextWidth(Candidate,Size) > MaxWidth)
                    {
                        Result.Add(Line); Line = Word;
                    }
                    else Line = Candidate;
                }
                Result.Add(Line);
            }
            return Result;
        }
        void Center(const FString& String,float Y,float Size,const FLinearColor& Color,bool bBold=false)
        {
            Text(String,(W - TextWidth(String,Size,bBold))*.5f,Y,Size,Color,bBold);
        }

        void Banner(float X,float Y,float Width,const FString& Value,const FLinearColor& Color)
        {
            const TArray<FString> Rows=Lines(Value,16.f,Width-40.f);
            const float Height=20.f+Rows.Num()*24.f;
            Rect(X,Y,Width,Height,Strip);
            Rect(X,Y,3.f,Height,Color);
            for (int32 I=0; I<Rows.Num(); ++I) Text(Rows[I],X+17.f,Y+8.f+24.f*I,16.f,Ink);
        }
    private:
        const FGeometry& Geometry;
        FSlateWindowElementList& List;
        int32 Layer;
        float Scale;
        TSharedRef<FSlateFontMeasure> Measure;
        FSlateFontInfo Face(float Size, bool bBold) const
        {
            return FCoreStyle::GetDefaultFontStyle(bBold ? TEXT("Bold") : TEXT("Regular"), FMath::Max(1,FMath::RoundToInt(Size*Scale)));
        }
    };

    class SShipOverlay : public SLeafWidget
    {
    public:
        SLATE_BEGIN_ARGS(SShipOverlay) {}
            SLATE_ARGUMENT(TWeakObjectPtr<AShipSurvivor>, Survivor)
        SLATE_END_ARGS()
        void Construct(const FArguments& Args)
        {
            Player = Args._Survivor;
            SetVisibility(EVisibility::HitTestInvisible);
            ForceVolatile(true);
        }
        FVector2D ComputeDesiredSize(float) const override { return FVector2D(1.f,1.f); }
        int32 OnPaint(const FPaintArgs&, const FGeometry& Geometry, const FSlateRect&,
            FSlateWindowElementList& List, int32 Layer, const FWidgetStyle&, bool) const override
        {
            AShipSurvivor* P=Player.Get();
            if (!P) return Layer;
            FShipCanvas C(Geometry,List,Layer);
            if (P->IsStockInventoryVisible())
            {
                const float X = (C.W-840.f)*.5f, Y = C.H-80.f;
                C.Rect(X,Y,840.f,69.f,Panel);
                C.Text(TEXT("W ręce: ") + P->GetToolLabel(),X+16.f,Y+6.f,18.f,Ink,true);
                if (P->bHasBattery)
                    C.Text(FString::Printf(TEXT("Akumulator %d%%    Pobór %.2f%%/s"),
                        FMath::RoundToInt(P->Charge),P->bLampOn ? P->GetLampDrainPerSecond() : 0.f),X+515.f,Y+8.f,15.f,Soft);
                C.Text(TEXT("Kliknij przedmiot lub naciśnij 1–8, aby zamienić go z ręką.   Shift + klik: opcje przedmiotu."),X+16.f,Y+38.f,15.f,Soft);
                return C.TopLayer();
            }
            if (P->IsBaseMenuActive()) return Layer;
            const FString Prompt=P->GetPrompt();
            if (P->bDead || P->bEscaped) Ending(C,*P);
            else if (P->IsKeypadOpen()) Keypad(C,*P);
            else if (P->IsReadingNote()) NoteReader(C,*P);
            else if (P->bKitEnabled) Play(C,*P,Prompt);
            else
            {
                C.Text(TEXT("Tab  Plecak   ·   1–8  Wybór przedmiotu"),24.f,36.f,15.f,Ink);
                if (!Prompt.IsEmpty()) C.Banner((C.W-650.f)*.5f,C.H-97.f,650.f,Prompt,Accent);
            }
            if (!P->bDead && !P->bEscaped)
            {
                const FString Notice=P->GetNotice();
                if (!Notice.IsEmpty()) C.Banner((C.W-590.f)*.5f,20.f,590.f,Notice,Accent);
            }
            return C.TopLayer();
        }
    private:
        TWeakObjectPtr<AShipSurvivor> Player;
        static void Play(FShipCanvas& C,AShipSurvivor& P,const FString& Prompt)
        {
            const bool bLamp = P.Equipped == TEXT("White") || P.Equipped == TEXT("ColorLamp");
            const float Height = bLamp ? 97.f : 69.f;
            const float X=24.f, Y=85.f;
            C.Rect(X,Y,365.f,Height,Strip);
            C.Rect(X,Y,3.f,Height,P.bWelding?Warning:Accent);
            C.Text(P.GetToolLabel(),X+16.f,Y+9.f,19.f,Ink,true);
            if (P.Equipped==TEXT("Welder"))
                C.Text(P.bWelding?TEXT("Łuk aktywny"):TEXT("Przyłóż do spoiny i trzymaj E"),X+16.f,Y+41.f,14.f,P.bWelding?Warning:Soft);
            else if (!bLamp) C.Text(P.HandHint(),X+16.f,Y+41.f,14.f,Soft);
            else
            {
                C.Rect(X+16.f,Y+45.f,10.f,10.f,P.GetFilterColor());
                C.Text(FString::Printf(TEXT("%s  /  %s"),*P.GetFilterLabel(),P.bLampOn?TEXT("światło włączone"):TEXT("światło wyłączone")),X+34.f,Y+39.f,14.f,Soft);
                if (bLamp)
                {
                    C.Text(FString::Printf(TEXT("Z/X  Moc %d/5      C/V  Promień %d/5"),P.PowerLevel,P.RadiusLevel),X+16.f,Y+65.f,14.f,Ink);

                }
            }

            const FString Controls = TEXT("Tab  Plecak     1–8  Zamień przedmiot z ręką");
            C.Text(Controls,C.W-C.TextWidth(Controls,13.f)-24.f,C.H-41.f,13.f,Soft);
            if (!Prompt.IsEmpty()) C.Banner((C.W-650.f)*.5f,C.H*.66f,650.f,Prompt,Accent);
        }
        static void NoteReader(FShipCanvas& C,AShipSurvivor& P)
        {
            const float W=760.f, H=410.f, X=(C.W-W)*.5f, Y=(C.H-H)*.5f;
            C.Rect(0,0,C.W,C.H,FLinearColor(0.f,.008f,.013f,.72f));
            C.Rect(X,Y,W,H,Panel);
            C.Text(FString::Printf(TEXT("Notatka serwisowa   %d / %d"),P.NoteIndex()+1,P.Notes.Num()),X+30.f,Y+24.f,24.f,Ink,true);
            if (P.Notes.IsValidIndex(P.NoteIndex()))
            {
                const TArray<FString> Rows=C.Lines(P.Notes[P.NoteIndex()],18.f,W-68.f);
                constexpr int32 Visible=10;
                P.ClampNoteScroll(Rows.Num()-Visible);
                for (int32 I=P.NoteScroll(); I<FMath::Min(Rows.Num(),P.NoteScroll()+Visible); ++I)
                    C.Text(Rows[I],X+34.f,Y+85.f+25.f*(I-P.NoteScroll()),18.f,Ink);
            }
            C.Text(TEXT("← / →   Inna notatka      PgUp / PgDn   Przewiń      Tab / Esc   Zamknij"),X+32.f,Y+H-43.f,15.f,Soft);
        }
        static void Keypad(FShipCanvas& C,AShipSurvivor& P)
        {
            const float W=600.f,H=260.f,X=(C.W-W)*.5f,Y=(C.H-H)*.5f;
            C.Rect(0,0,C.W,C.H,FLinearColor(0.f,.008f,.013f,.66f));
            C.Rect(X,Y,W,H,Panel); C.Rect(X,Y,W,3.f,Accent);
            C.Text(TEXT("Terminal dostępu"),X+30.f,Y+20.f,25.f,Ink,true);
            C.Text(FString::Printf(TEXT("Wpisane cyfry   %d / %d"),P.GetCode().Len(),P.GetCodeLength()),X+30.f,Y+71.f,16.f,Soft);
            C.Rect(X+30.f,Y+116.f,W-60.f,60.f,Rule);
            C.Text(P.GetCode().IsEmpty()?TEXT("—"):P.GetCode(),X+48.f,Y+124.f,26.f,Accent,true);
            C.Text(TEXT("Enter  Zatwierdź       Backspace  Usuń cyfrę       Esc / Tab  Anuluj"),X+30.f,Y+209.f,14.f,Ink);
        }
        static void Ending(FShipCanvas& C,AShipSurvivor& P)
        {
            C.Rect(0,0,C.W,C.H,FLinearColor(.009f,.018f,.022f,.92f));
            C.Center(P.bEscaped?TEXT("Ucieczka udana"):TEXT("Nie żyjesz"),C.H*.40f,39.f,P.bEscaped?Accent:Warning,true);
            C.Center(P.bEscaped?TEXT("Droga ewakuacyjna jest otwarta."):TEXT("Zagrożenie przerwało próbę."),C.H*.40f+75.f,20.f,Ink);
            C.Center(P.bEscaped?TEXT("G   Wróć do zwiedzania       R   Uruchom tę mapę od nowa"):TEXT("R lub G   Uruchom tę mapę od nowa"),C.H*.40f+135.f,16.f,Soft);
        }
    };
}

int32 UShipNumberedSlot::NativePaint(const FPaintArgs& Args, const FGeometry& Geometry, const FSlateRect& Clip,
    FSlateWindowElementList& List, int32 Layer, const FWidgetStyle& Style, bool bEnabled) const
{
    Layer = Super::NativePaint(Args,Geometry,Clip,List,Layer,Style,bEnabled);
    const FIntProperty* Field = FindFProperty<FIntProperty>(GetClass(),TEXT("Index"));
    if (!Field) return Layer;
    const int32 Index = Field->GetPropertyValue_InContainer(this);
    if (Index < 0 || Index > 7) return Layer;
    const auto Badge = Geometry.ToPaintGeometry(FVector2f(21,21), FSlateLayoutTransform(FVector2f(4,4)));
    FSlateDrawElement::MakeBox(List,++Layer,Badge,FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")),ESlateDrawEffect::None,Panel);
    FSlateDrawElement::MakeText(List,++Layer,Geometry.ToPaintGeometry(FVector2f(20,20),FSlateLayoutTransform(FVector2f(9,4))),
        FString::FromInt(Index+1),FCoreStyle::GetDefaultFontStyle(TEXT("Bold"),12),ESlateDrawEffect::None,Ink);
    return Layer;
}

void UShipVitalsWidget::NativeOnInitialized()
{
    Super::NativeOnInitialized();
    if (BatteryBar) StockBatteryTint = BatteryBar->GetFillColorAndOpacity();
}

void UShipVitalsWidget::NativeConstruct()
{
    Super::NativeConstruct();
    Kit = Cast<AShipSurvivor>(UGameplayStatics::GetActorOfClass(this, AShipSurvivor::StaticClass()));
}

float UShipVitalsWidget::DisplayedBatteryPercent(float StockPercent) const
{
    const AShipSurvivor* Equipment = Kit.Get();
    if (!Equipment || !Equipment->bKitEnabled) return StockPercent;
    return Equipment->bHasBattery ? FMath::Clamp(Equipment->Charge / 100.f, 0.f, 1.f) : 0.f;
}

void UShipVitalsWidget::NativeTick(const FGeometry& Geometry, float DeltaTime)
{
    Super::NativeTick(Geometry, DeltaTime);
    if (!BatteryBar) return;
    const AShipSurvivor* Equipment = Kit.Get();
    const bool bUsingFilters = Equipment && Equipment->bKitEnabled && Equipment->Equipped == TEXT("ColorLamp");
    const FLinearColor Color = bUsingFilters ? Equipment->GetFilterColor() : StockBatteryTint;
    if (!BatteryBar->GetFillColorAndOpacity().Equals(Color)) BatteryBar->SetFillColorAndOpacity(Color);
}

void UShipHUDOverlay::SetSurvivor(AShipSurvivor* InSurvivor) { Survivor=InSurvivor; }
TSharedRef<SWidget> UShipHUDOverlay::RebuildWidget() { return SNew(SShipOverlay).Survivor(Survivor); }
