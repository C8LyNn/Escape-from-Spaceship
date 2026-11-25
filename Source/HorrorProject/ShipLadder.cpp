#include "ShipLadder.h"
#include "ShipSurvivor.h"

#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
    constexpr float HalfWidth = 51.f;
    constexpr float FloorGap = 3.f;
}

AShipLadder::AShipLadder()
{
    Kind = TEXT("Ladder");
    Rails = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("SideRails"));
    Rungs = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("Rungs"));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> Nickel(TEXT("/Game/StarterContent/Materials/M_Metal_Brushed_Nickel.M_Metal_Brushed_Nickel"));
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> Amber(TEXT("/Game/StudentShowcase/Display/MI_Amber.MI_Amber"));
    for (UInstancedStaticMeshComponent* Part : {Rails.Get(), Rungs.Get()})
    {
        Part->SetupAttachment(RootComponent);
        Part->SetStaticMesh(Cube.Object);
        Part->SetCollisionProfileName(TEXT("BlockAllDynamic"));
        Part->SetCanEverAffectNavigation(false);
    }
    Rails->SetMaterial(0, Nickel.Object);
    Rungs->SetMaterial(0, Amber.Object);
}

void AShipLadder::BuildLadder()
{
    Mesh->SetVisibility(false);
    Mesh->SetHiddenInGame(true);
    Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Rails->ClearInstances();
    Rungs->ClearInstances();
    const float Height = FMath::Clamp(ClimbHeight,80.f,3000.f);
    const float RailHeight = Height + 38.f;
    for (const float Side : {-HalfWidth, HalfWidth})
        Rails->AddInstance(FTransform(FQuat::Identity, FVector(0.f,Side,RailHeight*.5f), FVector(.065f,.065f,RailHeight/100.f)));
    const int32 Count = FMath::Max(2,FMath::RoundToInt(Height / 28.f));
    for (int32 I = 0; I < Count; ++I)
        Rungs->AddInstance(FTransform(FQuat::Identity,FVector(0.f,0.f,(I+.5f)*Height/Count), FVector(.085f,2.f*HalfWidth/100.f,.045f)));
    Trigger->SetRelativeLocation(FVector(0.f,0.f,(Height+24.f)*.5f));
    Trigger->SetBoxExtent(FVector(9.f,HalfWidth,(Height+24.f)*.5f));
}

void AShipLadder::OnConstruction(const FTransform& Transform)
{
    Super::OnConstruction(Transform);
    BuildLadder();
}
void AShipLadder::BeginPlay()
{
    Super::BeginPlay();
    BuildLadder();
}
void AShipLadder::EndPlay(const EEndPlayReason::Type Reason)
{
    Release();
    Super::EndPlay(Reason);
}

FVector AShipLadder::FeetLocal(const ACharacter* Character) const
{
    const FVector Feet = Character->GetActorLocation() - FVector::UpVector * Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
    return GetActorTransform().InverseTransformPosition(Feet);
}
bool AShipLadder::IsAtLanding(const ACharacter* Character) const
{
    if (!Character) return false;
    const FVector Feet = FeetLocal(Character);
    return FMath::Abs(Feet.Z - ClimbHeight) <= 15.f && Feet.X >= -100.f && Feet.X <= 58.f && FMath::Abs(Feet.Y) < HalfWidth;
}
bool AShipLadder::CanMount(const ACharacter* Character) const
{
    if (!Character || HasRider() || Character->IsCrouched()) return false;
    const UCharacterMovementComponent* Move = Character->GetCharacterMovement();
    if (!Move->IsMovingOnGround() && !Move->IsFalling()) return false;
    if (IsAtLanding(Character)) return true;
    const FVector Feet = FeetLocal(Character);
    return Feet.X >= 0.f && Feet.X <= 145.f && FMath::Abs(Feet.Y) < HalfWidth + 24.f
        && Feet.Z >= -16.f && Feet.Z < ClimbHeight;
}
FVector AShipLadder::PositionAt(const ACharacter* Character, float Height, bool bOnDeck) const
{
    const UCapsuleComponent* Capsule = Character->GetCapsuleComponent();
    const float Radius = Capsule->GetScaledCapsuleRadius();
    const FVector Shaft = GetActorTransform().TransformPosition(FVector(0.f,0.f,Height));
    return Shaft + GetActorForwardVector() * (bOnDeck ? -Radius - 38.f : Radius + 12.f)
        + FVector::UpVector * (Capsule->GetScaledCapsuleHalfHeight() + FloorGap);
}
bool AShipLadder::PathClear(const ACharacter* Character, FVector Destination) const
{
    const UCapsuleComponent* Capsule = Character->GetCapsuleComponent();
    const FCollisionQueryParams Params(SCENE_QUERY_STAT(LadderClearance), false, Character);
    return !GetWorld()->SweepTestByProfile(Character->GetActorLocation(),Destination,FQuat::Identity,
        Capsule->GetCollisionProfileName(),Capsule->GetCollisionShape(-.3f),Params);
}

bool AShipLadder::TryMount(ACharacter* Character)
{
    if (!CanMount(Character)) return false;
    const float Height = IsAtLanding(Character) ? ClimbHeight : FMath::Clamp(float(FeetLocal(Character).Z) - FloorGap,0.f,ClimbHeight);
    const FVector Snap = PositionAt(Character,Height,false);
    if (!PathClear(Character,Snap)) return false;
    UCharacterMovementComponent* Move = Character->GetCharacterMovement();
    Character->StopJumping();
    Move->StopMovementImmediately();
    Move->DisableMovement();
    Character->SetActorLocation(Snap,true);
    Rider = Character;
    bLandingBlocked = false;
    return true;
}

void AShipLadder::Climb(float Axis, float DeltaSeconds)
{
    ACharacter* Character = Rider.Get();
    if (!Character || FMath::IsNearlyZero(Axis)) return;
    const float Current = FeetLocal(Character).Z - FloorGap;
    const float Height = FMath::Clamp(Current + Axis * FMath::Max(20.f,ClimbSpeed) * DeltaSeconds, 0.f, ClimbHeight);
    const FVector Target = PositionAt(Character,Height,false);
    FHitResult Hit;
    Character->SetActorLocation(Target,true,&Hit);
    if (Hit.bBlockingHit && !Character->GetActorLocation().Equals(Target,1.f)) return;

    if (Axis < 0.f)
    {
        bLandingBlocked = false;
        if (Height <= KINDA_SMALL_NUMBER) Release();
        return;
    }
    if (Height < ClimbHeight) return;
    const FVector Landing = PositionAt(Character,ClimbHeight,true);
    FFindFloorResult Floor;
    Character->GetCharacterMovement()->FindFloor(Landing,Floor,false);
    bLandingBlocked = !Floor.IsWalkableFloor() || Floor.FloorDist > 24.f || !PathClear(Character,Landing);
    if (bLandingBlocked) return;
    Character->SetActorLocation(Landing,true,&Hit);
    if (Character->GetActorLocation().Equals(Landing,1.f)) Release();
}

void AShipLadder::Release(bool bJump)
{
    ACharacter* Character = Rider.Get();
    Rider.Reset();
    bLandingBlocked = false;
    if (!Character) return;
    UCharacterMovementComponent* Move = Character->GetCharacterMovement();
    Move->StopMovementImmediately();
    Move->SetMovementMode(MOVE_Falling);
    if (bJump)
        Character->LaunchCharacter(GetActorForwardVector()*230.f + FVector::UpVector*150.f,true,true);
}

FString AShipLadder::Prompt(AShipSurvivor* S) const
{
    if (!S || S->bDead || S->bEscaped) return {};
    if (HasRider())
        return bLandingBlocked ? TEXT("Przejście na górze jest zablokowane · S w dół · E puść") : TEXT("Drabina · W w górę · S w dół · E puść · Spacja odskocz");
    const ACharacter* Character = UGameplayStatics::GetPlayerCharacter(this,0);
    if (!CanMount(Character)) return {};
    return IsAtLanding(Character) ? TEXT("E — zejdź na drabinę") : TEXT("E — chwyć drabinę");
}
