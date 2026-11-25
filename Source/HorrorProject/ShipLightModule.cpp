#include "ShipLightModule.h"
#include "ShipModulePhysics.h"
#include "ShipSurvivor.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"

AShipLightModule::AShipLightModule()
{
    PrimaryActorTick.bCanEverTick = true;
    SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Mount")));
    Carriage = CreateDefaultSubobject<USceneComponent>(TEXT("Carriage"));
    Carriage->SetupAttachment(RootComponent);
    Track = CreateDefaultSubobject<USceneComponent>(TEXT("Track"));
    Track->SetupAttachment(Carriage);
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
    auto MakePart = [&](const TCHAR* Name, USceneComponent* Parent)
    {
        UStaticMeshComponent* Part = CreateDefaultSubobject<UStaticMeshComponent>(Name);
        Part->SetupAttachment(Parent);
        Part->SetStaticMesh(Cube.Object);
        Part->SetCollisionProfileName(TEXT("BlockAllDynamic"));
        Part->SetCanEverAffectNavigation(false);
        return Part;
    };
    Block = MakePart(TEXT("Block"), Carriage);
    Block->CanCharacterStepUpOn = ECB_Yes;
    Ramp = MakePart(TEXT("Ramp"), Track);
    LeftRail = MakePart(TEXT("LeftRail"), Track);
    RightRail = MakePart(TEXT("RightRail"), Track);
    LightStrip = MakePart(TEXT("ReceiverLight"), Carriage);
    LightStrip->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> Housing(TEXT("/Game/StudentShowcase/Display/MI_Pedestal.MI_Pedestal"));
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> Marker(TEXT("/Game/StudentShowcase/Display/MI_Teal.MI_Teal"));
    Block->SetMaterial(0,Housing.Object);
    Ramp->SetMaterial(0,Housing.Object);
    for (UStaticMeshComponent* Part : {LightStrip.Get(),LeftRail.Get(),RightRail.Get()}) Part->SetMaterial(0,Marker.Object);
    Contacts = CreateDefaultSubobject<UShipModulePhysics>(TEXT("SurfaceContacts"));
}

void AShipLightModule::OnConstruction(const FTransform& Transform)
{
    Super::OnConstruction(Transform);
    ConfigureGeometry();
}

void AShipLightModule::ConfigureGeometry()
{
    BlockSize = BlockSize.ComponentMax(FVector(4,24,24));
    Block->SetRelativeScale3D(BlockSize / 100.);
    const double HalfX = BlockSize.X * .5;
    LightStrip->SetRelativeLocation(FVector(HalfX + .5, 0, BlockSize.Z * .36));
    LightStrip->SetRelativeScale3D(FVector(1., BlockSize.Y * .65, 4.) / 100.);
    LightStrip->SetVisibility(!bGuide);
    // Rotate the whole trough about local Z: +Y runs downhill towards the open exit.
    constexpr double Slope = 15.;
    const double Run = BlockSize.Y;
    Track->SetRelativeLocation(FVector(HalfX + 5. + .5 * Run * FMath::Tan(FMath::DegreesToRadians(Slope)), 0, 0));
    Track->SetRelativeRotation(FRotator(0,Slope,0));
    const double Length = Run / FMath::Cos(FMath::DegreesToRadians(Slope));
    Ramp->SetRelativeScale3D(FVector(5., Length, BlockSize.Z) / 100.);
    for (UStaticMeshComponent* Rail : {LeftRail.Get(), RightRail.Get()})
    {
        Rail->SetRelativeLocation(FVector(10.,0,(Rail == LeftRail ? 1. : -1.) * (BlockSize.Z * .5 - 2.)));
        Rail->SetRelativeScale3D(FVector(22.,Length,4.) / 100.);
    }
    for (UStaticMeshComponent* Part : {Ramp.Get(),LeftRail.Get(),RightRail.Get()})
    {
        Part->SetVisibility(bGuide);
        Part->SetCollisionEnabled(bGuide ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision);
    }
}

void AShipLightModule::BeginPlay()
{
    Super::BeginPlay();
    ConfigureGeometry();
    Operator = Cast<AShipSurvivor>(UGameplayStatics::GetActorOfClass(this, AShipSurvivor::StaticClass()));
    if (Operator.IsValid()) AddTickPrerequisiteActor(Operator.Get());
    Contacts->SetSurface(bGuide ? Ramp : Block);

    StripFinish = (bGuide ? LeftRail : LightStrip)->CreateDynamicMaterialInstance(0);
    if (bGuide && StripFinish) RightRail->SetMaterial(0,StripFinish);
    ApplyColor(NAME_None,nullptr);
}

void AShipLightModule::ApplyColor(FName Color, const AShipSurvivor* Player)
{
    const FLinearColor Accent = Color.IsNone() || !Player ? FLinearColor(.15f,.70f,.74f) : Player->GetFilterColor();

    if (StripFinish) StripFinish->SetVectorParameterValue(TEXT("Color"),Accent);
}

bool AShipLightModule::IsFaceLit(const AShipSurvivor& Player) const
{
    if (Player.Equipped != TEXT("ColorLamp") || !Player.bLampOn) return false;
    // The receiver covers the entire front of the assembly, including the guide's two rails.
    // Its viewing side stays fixed when the sloped contact surface turns.
    const FVector Half = BlockSize * .5;
    const FVector Normal = Block->GetForwardVector();
    const FVector Face = Block->GetComponentLocation() + Normal * (Half.X + .75);
    const FVector Eye = Player.ViewOrigin();
    const double FrontDistance = FVector::DotProduct(Eye - Face, Normal);
    if (FrontDistance <= 0.) return false;
    if (Player.BeamReachesTarget(Face,this)) return true;
    const FVector Axis = Player.ViewDirection();
    const double Incidence = -FVector::DotProduct(Axis, Normal);
    if (Incidence > UE_SMALL_NUMBER)
    {
        // A tight beam can strike any part of the face, including the middle of an edge.
        const FVector Aim = Eye + Axis * (FrontDistance / Incidence) - Face;
        const FVector Right = Block->GetRightVector(), Top = Block->GetUpVector();
        const double Y = FMath::Clamp(FVector::DotProduct(Aim,Right), -Half.Y, Half.Y);
        const double Z = FMath::Clamp(FVector::DotProduct(Aim,Top), -Half.Z, Half.Z);
        if (Player.BeamReachesTarget(Face + Right*Y + Top*Z,this)) return true;
    }
    const FVector Across = Block->GetRightVector() * (Half.Y * .80);
    const FVector Up = Block->GetUpVector() * (Half.Z * .80);
    for (const FVector& Point : {Face+Across+Up,Face+Across-Up,Face-Across+Up,Face-Across-Up})
        if (Player.BeamReachesTarget(Point,this)) return true;
    return false;
}

void AShipLightModule::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    const AShipSurvivor* Player = Operator.Get();
    const FName Color = Player && IsFaceLit(*Player) ? Player->Filter : NAME_None;
    Contacts->SetLight(Color);
    if (Color.IsNone())
    {
        DarkTime += DeltaSeconds;
        if (!LastColor.IsNone() && DarkTime >= .16f)
        {
            LastColor = NAME_None;
            ApplyColor(NAME_None,nullptr);
        }
    }
    else
    {
        DarkTime = 0.f;
        if (Color != LastColor)
        {
            if (Color == TEXT("Red")) bExtended = !bExtended;
            if (Color == TEXT("UV")) QuarterTurns = (QuarterTurns + 1) % 4;
            LastColor = Color;
            ApplyColor(Color,Player);
        }
    }
    float Next = FMath::FInterpConstantTo(Extension,bExtended ? Travel : 0.f,DeltaSeconds,150.f);
    if (Next > Extension) Next = Extension + Contacts->MakeRoomForExtension(GetActorForwardVector(), Next-Extension);
    const float TurnDelta = FMath::FindDeltaAngleDegrees(Roll,QuarterTurns * 90.f);
    const float NextRoll = FRotator::NormalizeAxis(Roll + FMath::Clamp(TurnDelta,-180.f*DeltaSeconds,180.f*DeltaSeconds));
    if (Next != Extension || NextRoll != Roll)
    {
        Extension = Next;
        Roll = NextRoll;
        Carriage->SetRelativeLocationAndRotation(FVector(Extension,0,0),FQuat(FVector::ForwardVector,FMath::DegreesToRadians(Roll)));
    }
    Contacts->UpdateContacts(DeltaSeconds);
}

bool AShipLightModule::HasCargo() const { return Contacts->HasCargo(); }
int32 AShipLightModule::GetLaunchCount() const { return Contacts->GetLaunchCount(); }
