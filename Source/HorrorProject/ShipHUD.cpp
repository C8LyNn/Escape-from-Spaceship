#include "ShipHUD.h"

#include "ShipSurvivor.h"
#include "Framework/Application/SlateApplication.h"
#include "Fonts/FontMeasure.h"
#include "Rendering/DrawElements.h"
#include "Rendering/SlateRenderer.h"
#include "Styling/CoreStyle.h"
#include "Widgets/SLeafWidget.h"

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
        void Meter(float X, float Y, float Width, const FString& Label,float Value,const FLinearColor& Color)
        {
            const FString Caption = FString::Printf(TEXT("%s   %d%%"),*Label,FMath::RoundToInt(Value));
            Text(Caption,X,Y,14.f,Ink);
            Rect(X,Y+25.f,Width,4.f,Rule);
            Rect(X,Y+25.f,Width*FMath::Clamp(Value/100.f,0.f,1.f),4.f,Color);
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
            if (!P || P->IsBaseMenuActive()) return Layer;
            FShipCanvas C(Geometry,List,Layer);
            const FString Prompt=P->GetPrompt();
            if (P->bDead || P->bEscaped) Ending(C,*P);
            else if (P->IsKeypadOpen()) Keypad(C,*P);
            else if (P->IsInventoryOpen()) Inventory(C,*P);
            else if (P->bKitEnabled) Play(C,*P,Prompt);
            else if (!Prompt.IsEmpty()) C.Banner((C.W-650.f)*.5f,C.H-97.f,650.f,Prompt,Accent);
            if (P->bKitEnabled && !P->bDead && !P->bEscaped)
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
            const bool bLamp = P.Equipped != TEXT("Welder") && (P.Equipped == TEXT("White") ? P.bHasWhite : P.bHasColorLamp);
            const float Height = bLamp ? 142.f : 107.f;
            const float X=24.f, Y=C.H-Height-25.f;
            C.Rect(X,Y,365.f,Height,Strip);
            C.Rect(X,Y,3.f,Height,P.bWelding?Warning:Accent);
            C.Text(P.GetToolLabel(),X+16.f,Y+9.f,19.f,Ink,true);
            if (P.Equipped==TEXT("Welder"))
                C.Text(P.bWelding?TEXT("Łuk aktywny"):TEXT("Przyłóż do spoiny i trzymaj E"),X+16.f,Y+41.f,14.f,P.bWelding?Warning:Soft);
            else
            {
                C.Rect(X+16.f,Y+45.f,10.f,10.f,P.GetFilterColor());
                C.Text(FString::Printf(TEXT("%s  /  %s"),*P.GetFilterLabel(),P.bLampOn?TEXT("światło włączone"):TEXT("światło wyłączone")),X+34.f,Y+39.f,14.f,Soft);
                if (bLamp)
                {
                    C.Text(FString::Printf(TEXT("Z/X  Moc %d/5      C/V  Promień %d/5"),P.PowerLevel,P.RadiusLevel),X+16.f,Y+65.f,14.f,Ink);
                    C.Text(FString::Printf(TEXT("Pobór %.2f%%/s"),P.bLampOn ? P.GetLampDrainPerSecond() : 0.f),X+216.f,Y+99.f,13.f,Soft);
                }
            }
            const FLinearColor MeterColor=P.Charge<18.f?Warning:Accent;
            C.Meter(X+16.f,Y+Height-41.f,330.f,P.bHasBattery?TEXT("Wspólna bateria"):TEXT("Brak baterii"),P.Charge,MeterColor);
            C.Text(TEXT("I  Wyposażenie i sterowanie     G  Wyłącz zestaw"),C.W-365.f,C.H-41.f,13.f,Soft);
            if (!Prompt.IsEmpty()) C.Banner((C.W-650.f)*.5f,C.H*.66f,650.f,Prompt,Accent);
        }
        static void Inventory(FShipCanvas& C,AShipSurvivor& P)
        {
            const float PW=970.f,PH=520.f,X=(C.W-PW)*.5f,Y=(C.H-PH)*.5f;
            C.Rect(0,0,C.W,C.H,FLinearColor(0.f,.008f,.013f,.72f));
            C.Rect(X,Y,PW,PH,Panel); C.Rect(X,Y,PW,3.f,Accent);
            C.Text(TEXT("Wyposażenie"),X+32.f,Y+22.f,29.f,Ink,true);
            C.Text(TEXT("I / Esc / Tab   Zamknij"),X+PW-201.f,Y+35.f,14.f,Soft);
            C.Rect(X+32.f,Y+77.f,PW-64.f,1.f,Rule);
            const FString Names[]={TEXT("Latarka biała"),TEXT("Latarka z kolorami"),TEXT("Spawarka"),TEXT("Bateria przenośna"),TEXT("Karta dostępu")};
            const bool Have[]={P.bHasWhite,P.bHasColorLamp,P.bHasWelder,P.bHasBattery,P.bHasCard};
            for (int32 I=0;I<5;++I)
            {
                const float R=Y+99.f+I*37.f;
                C.Rect(X+34.f,R+7.f,8.f,8.f,Have[I]?Accent:Rule);
                C.Text(Names[I],X+55.f,R,16.f,Have[I]?Ink:Soft);
                C.Text(Have[I]?TEXT("jest"):TEXT("brak"),X+290.f,R,13.f,Have[I]?Accent:Soft);
            }
            C.Meter(X+33.f,Y+303.f,286.f,TEXT("Energia"),P.Charge,P.Charge<18.f?Warning:Accent);
            C.Rect(X+347.f,Y+96.f,1.f,260.f,Rule);
            const float TX=X+374.f;
            C.Text(TEXT("Dziennik"),TX,Y+94.f,19.f,Accent,true);
            if (P.Notes.IsValidIndex(P.NoteIndex()))
            {
                C.Text(FString::Printf(TEXT("Notatka %d z %d"),P.NoteIndex()+1,P.Notes.Num()),TX,Y+127.f,14.f,Soft);
                TArray<FString> Rows=C.Lines(P.Notes[P.NoteIndex()],17.f,PW-412.f);
                constexpr int32 Visible=7;
                P.ClampNoteScroll(Rows.Num()-Visible);
                const int32 End=FMath::Min(Rows.Num(),P.NoteScroll()+Visible);
                for (int32 I=P.NoteScroll();I<End;++I) C.Text(Rows[I],TX,Y+160.f+(I-P.NoteScroll())*25.f,17.f,Ink);
                C.Text(TEXT("← / →   notatki      PgUp / PgDn   przewiń"),TX,Y+342.f,13.f,Soft);
            }
            else C.Text(TEXT("Przeczytane notatki pojawią się tutaj."),TX,Y+143.f,16.f,Soft);
            C.Rect(X+32.f,Y+384.f,PW-64.f,1.f,Rule);
            C.Text(TEXT("1  Biała latarka       2  Latarka z kolorami       3  Spawarka       F  Światło       Q  Następny kolor"),X+32.f,Y+396.f,15.f,Ink);
            C.Text(TEXT("Z / X   Zmniejsz / zwiększ moc        C / V   Zmniejsz / zwiększ promień wiązki"),X+32.f,Y+425.f,15.f,Ink);
            C.Text(TEXT("E  Interakcja / przytrzymaj       LPM  Przytrzymaj, aby przesunąć skrzynię       G  Wyłącz zestaw"),X+32.f,Y+454.f,14.f,Ink);
            C.Text(TEXT("WASD  Ruch       Mysz  Rozglądanie       Spacja  Skok       Shift  Sprint       Ctrl  Kucanie       Tab  Ekwipunek bazowy"),X+32.f,Y+484.f,13.f,Soft);
        }
        static void Keypad(FShipCanvas& C,AShipSurvivor& P)
        {
            const float W=600.f,H=260.f,X=(C.W-W)*.5f,Y=(C.H-H)*.5f;
            C.Rect(0,0,C.W,C.H,FLinearColor(0.f,.008f,.013f,.66f));
            C.Rect(X,Y,W,H,Panel); C.Rect(X,Y,W,3.f,Accent);
            C.Text(TEXT("Terminal dostępu"),X+30.f,Y+20.f,25.f,Ink,true);
            C.Text(TEXT("Wpisz kod za pomocą klawiszy numerycznych."),X+30.f,Y+71.f,16.f,Soft);
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

void UShipHUDOverlay::SetSurvivor(AShipSurvivor* InSurvivor) { Survivor=InSurvivor; }
TSharedRef<SWidget> UShipHUDOverlay::RebuildWidget() { return SNew(SShipOverlay).Survivor(Survivor); }
