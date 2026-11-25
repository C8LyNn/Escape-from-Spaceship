#include "ShipGlowstick.h"

#include "Components/BoxComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Scene.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Math/RotationMatrix.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
    constexpr float Radius = 2.5f;
    constexpr float LightLumens = 850.f;
    constexpr float FadeSeconds = 8.f;
    const FLinearColor GlowColor(.045f, 1.f, .52f);
    const FName ColorParameter(TEXT("Color"));
}

AShipGlowstick::AShipGlowstick()
{
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.TickInterval = .1f;
    InitialLifeSpan = 45.f;

    Collision = CreateDefaultSubobject<UBoxComponent>(TEXT("Collision"));
    SetRootComponent(Collision);
    Collision->SetMobility(EComponentMobility::Movable);
    Collision->InitBoxExtent(FVector(12.f, Radius, Radius));
    Collision->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    Collision->SetCollisionObjectType(ECC_WorldDynamic);
    Collision->SetCollisionResponseToAllChannels(ECR_Ignore);
    Collision->SetCollisionResponseToChannel(ECC_WorldStatic, ECR_Block);
    Collision->SetCollisionResponseToChannel(ECC_WorldDynamic, ECR_Block);
    Collision->SetCollisionResponseToChannel(ECC_PhysicsBody, ECR_Block);
    Collision->SetGenerateOverlapEvents(false);
    Collision->SetCanEverAffectNavigation(false);
    Collision->CanCharacterStepUpOn = ECB_No;

    static ConstructorHelpers::FObjectFinder<UStaticMesh> Cylinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> Emissive(TEXT("/Engine/ArtTools/RenderToTexture/Materials/Debug/M_Emissive_Color.M_Emissive_Color"));
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> Casing(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));

    auto AddCylinder = [&](FName Name, float Offset, float Diameter, float Length, UMaterialInterface* Material)
    {
        UStaticMeshComponent* Part = CreateDefaultSubobject<UStaticMeshComponent>(Name);
        Part->SetupAttachment(Collision);
        Part->SetMobility(EComponentMobility::Movable);
        Part->SetStaticMesh(Cylinder.Object);
        Part->SetMaterial(0, Material);
        Part->SetRelativeLocation(FVector(Offset, 0.f, 0.f));
        Part->SetRelativeRotation(FRotator(90.f, 0.f, 0.f));
        Part->SetRelativeScale3D(FVector(Diameter, Diameter, Length) / 100.f);
        Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Part->SetGenerateOverlapEvents(false);
        Part->SetCanEverAffectNavigation(false);
        Part->SetCastShadow(false);
        return Part;
    };

    Tube = AddCylinder(TEXT("LuminousTube"), 0.f, 3.6f, 19.f, Emissive.Object);
    FrontCap = AddCylinder(TEXT("FrontCollar"), 10.5f, 2.f * Radius, 3.f, Casing.Object);
    RearCap = AddCylinder(TEXT("RearCollar"), -10.5f, 2.f * Radius, 3.f, Casing.Object);

    Light = CreateDefaultSubobject<UPointLightComponent>(TEXT("AreaLight"));
    Light->SetupAttachment(Collision);
    Light->SetMobility(EComponentMobility::Movable);
    Light->SetRelativeLocation(FVector(0.f, 0.f, 3.f));
    Light->SetIntensityUnits(ELightUnits::Lumens);
    Light->SetIntensity(LightLumens);
    Light->SetLightColor(GlowColor);
    Light->SetAttenuationRadius(500.f);
    Light->SetSourceRadius(2.f);
    Light->SetCastShadows(true);

    Projectile = CreateDefaultSubobject<UProjectileMovementComponent>(TEXT("Projectile"));
    Projectile->SetUpdatedComponent(Collision);
    Projectile->SetAutoActivate(false);
    Projectile->bInitialVelocityInLocalSpace = false;
    Projectile->bShouldBounce = true;
    Projectile->Bounciness = .38f;
    Projectile->Friction = .48f;
    Projectile->BounceVelocityStopSimulatingThreshold = 18.f;
    Projectile->Velocity = FVector::ZeroVector;
}

void AShipGlowstick::BeginPlay()
{
    Super::BeginPlay();
    TubeMaterial = Tube->CreateDynamicMaterialInstance(0);
    if (UMaterialInstanceDynamic* CapMaterial = FrontCap->CreateDynamicMaterialInstance(0))
    {
        CapMaterial->SetVectorParameterValue(ColorParameter, FLinearColor(.06f, .085f, .10f));
        RearCap->SetMaterial(0, CapMaterial);
    }
    SetBrightness(1.f);
    Projectile->OnProjectileStop.AddDynamic(this, &AShipGlowstick::Settle);
}

void AShipGlowstick::Launch(FVector Velocity)
{
    Collision->IgnoreActorWhenMoving(GetOwner(), true);
    Collision->IgnoreActorWhenMoving(GetInstigator(), true);
    const FVector Heading = Velocity.GetSafeNormal2D();
    if (!Heading.IsNearlyZero())
        SetActorRotation(Heading.Rotation());
    Projectile->SetUpdatedComponent(Collision);
    Projectile->Velocity = Velocity;
    Projectile->UpdateComponentVelocity();
    Projectile->Activate(true);
}

void AShipGlowstick::Settle(const FHitResult& Hit)
{
    if (!Hit.bBlockingHit || Hit.ImpactNormal.Z < .5f) return;

    // Keep both collars on the surface when the last bounce lands on a slope.
    const FVector AlongFloor = FVector::VectorPlaneProject(GetActorForwardVector(), Hit.ImpactNormal);
    const double SurfaceDistance = FVector::DotProduct(GetActorLocation() - Hit.ImpactPoint, Hit.ImpactNormal);
    SetActorLocationAndRotation(GetActorLocation() + Hit.ImpactNormal * (Radius - SurfaceDistance),
        FRotationMatrix::MakeFromXZ(AlongFloor, Hit.ImpactNormal).Rotator());
    if (UPrimitiveComponent* Surface = Hit.GetComponent(); Surface && Surface->Mobility == EComponentMobility::Movable)
        AttachToComponent(Surface, FAttachmentTransformRules::KeepWorldTransform, Hit.BoneName);
}

void AShipGlowstick::SetBrightness(float Fraction)
{
    Light->SetIntensity(LightLumens * Fraction);
    if (TubeMaterial)
        TubeMaterial->SetVectorParameterValue(ColorParameter, GlowColor * (6.f * Fraction));
}

void AShipGlowstick::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    const float Remaining = GetLifeSpan();
    if (Remaining < FadeSeconds)
        SetBrightness(FMath::Clamp(Remaining / FadeSeconds, 0.f, 1.f));
}
