#include "ShipModulePhysics.h"

#include "CollisionShape.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/OverlapResult.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "PhysicsEngine/PhysicsHandleComponent.h"
#include "UObject/UObjectIterator.h"

namespace
{
    const FName GreenLight(TEXT("Green")), BlueLight(TEXT("Blue")), WhiteLight(TEXT("White"));
    const FName CargoTag(TEXT("PushCrate"));
    constexpr float ContactDepth = 8.f;
    constexpr float CheckInterval = 1.f / 30.f;
    constexpr double LaunchCooldown = .55;

    bool IsHeld(const UPrimitiveComponent* Body)
    {
        // The survivor's grab handle lives on a separate actor from both the crate and the pawn.
        for (TObjectIterator<UPhysicsHandleComponent> Handle; Handle; ++Handle)
            if (Handle->GetGrabbedComponent() == Body) return true;
        return false;
    }
}

UShipModulePhysics::UShipModulePhysics()
{
    PrimaryComponentTick.bCanEverTick = false;
}

void UShipModulePhysics::SetSurface(UStaticMeshComponent* InSurface)
{
    if (InSurface == Surface.Get()) return;
    ReleaseCargo();
    Surface = InSurface;
    BlueContacts.Reset();
    LaunchCooldowns.Reset();
    TimeUntilCheck = 0.f;
}

void UShipModulePhysics::SetLight(FName Color)
{
    if (ActiveLight == Color) return;
    ActiveLight = Color;
    TimeUntilCheck = 0.f;
    BlueContacts.Reset();
    if (Color == WhiteLight) ReleaseCargo();
}

bool UShipModulePhysics::HasCargo() const
{
    return Cargo.ContainsByPredicate([](const FAttachedCargo& Item) { return Item.Body.IsValid(); });
}

void UShipModulePhysics::Stick(UPrimitiveComponent* Body, const FVector& Face, const FVector& Normal)
{
    const FTransform& Transform = Body->GetComponentTransform();
    const FBoxSphereBounds Local = Body->GetLocalBounds();
    const FVector Half = Local.BoxExtent * Transform.GetScale3D().GetAbs();
    const double Depth = FVector::DotProduct(Transform.InverseTransformVectorNoScale(Normal).GetAbs(), Half);
    const double Gap = FVector::DotProduct(Transform.TransformPosition(Local.Origin) - Face, Normal) - Depth;

    Body->SetSimulatePhysics(false);
    if (!Body->AttachToComponent(Surface.Get(), FAttachmentTransformRules::KeepWorldTransform))
    {
        Body->SetSimulatePhysics(true);
        return;
    }
    if (Gap > .1 && Gap <= ContactDepth)
        Body->AddWorldOffset(-Gap * Normal, true);

    Cargo.RemoveAllSwap([](const FAttachedCargo& Item) { return !Item.Body.IsValid(); });
    Cargo.Add(FAttachedCargo{ Body, Body->CanCharacterStepUpOn, Body->GetCollisionResponseToChannel(ECC_Pawn) });
    Body->CanCharacterStepUpOn = ECB_Yes;
    Body->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);
}

void UShipModulePhysics::ReleaseCargo()
{
    for (const FAttachedCargo& Item : Cargo)
    {
        UPrimitiveComponent* Body = Item.Body.Get();
        if (!Body) continue;
        if (Body->GetAttachParent() == Surface.Get())
            Body->DetachFromComponent(FDetachmentTransformRules::KeepWorldTransform);
        Body->CanCharacterStepUpOn = Item.PreviousStepUp;
        Body->SetCollisionResponseToChannel(ECC_Pawn, Item.PreviousPawnResponse);
        Body->SetSimulatePhysics(true);
        Body->WakeAllRigidBodies();
    }
    Cargo.Reset();
}

float UShipModulePhysics::MakeRoomForExtension(const FVector& Direction, float Distance)
{
    UStaticMeshComponent* Panel = Surface.Get();
    if (!Panel || !GetWorld() || Distance <= 0.f) return Distance;
    FVector Min, Max;
    Panel->GetLocalBounds(Min, Max);
    const FTransform& Frame = Panel->GetComponentTransform();
    const FVector Half = (Max-Min)*.5 * Frame.GetScale3D().GetAbs();
    const FVector Normal = Panel->GetForwardVector();
    const FVector Face = Frame.TransformPosition((Min+Max)*.5) + Normal * Half.X;
    const double AlongNormal = FVector::DotProduct(Direction, Normal);
    if (AlongNormal <= UE_SMALL_NUMBER) return Distance;
    // The moving mount is kinematic. Sweep its leading face before a long frame can advance
    // it past the centre of a crate, where depenetration would push the crate backwards.
    const double Reach = Distance * AlongNormal;
    TArray<FOverlapResult> Touching;
    GetWorld()->OverlapMultiByObjectType(Touching, Face + .5 * Distance * Direction, Panel->GetComponentQuat(),
        FCollisionObjectQueryParams(ECC_PhysicsBody),
        FCollisionShape::MakeBox(FVector(.5 * Reach + 2., Half.Y - .5, Half.Z - .5)),
        FCollisionQueryParams(SCENE_QUERY_STAT(ShipPanelPush), false, GetOwner()));
    float Allowed = Distance;
    for (const FOverlapResult& Contact : Touching)
    {
        UPrimitiveComponent* Body = Contact.GetComponent();
        if (!Body || !Body->IsSimulatingPhysics()) continue;
        const FTransform& BodyFrame = Body->GetComponentTransform();
        const FBoxSphereBounds Box = Body->GetLocalBounds();
        const double Centre = FVector::DotProduct(BodyFrame.TransformPosition(Box.Origin) - Face, Normal);
        if (Centre < 0.) continue;
        const FVector Axis = BodyFrame.InverseTransformVectorNoScale(Normal).GetAbs();
        const double Depth = FVector::DotProduct(Axis, Box.BoxExtent * BodyFrame.GetScale3D().GetAbs());
        const double Gap = FMath::Max(0., (Centre - Depth) / AlongNormal);
        if (Gap >= Allowed) continue;
        FHitResult Hit;
        Body->WakeAllRigidBodies();
        Body->MoveComponent(Direction * (Allowed - Gap), Body->GetComponentQuat(), true, &Hit, MOVECOMP_NoFlags, ETeleportType::TeleportPhysics);
        if (Hit.IsValidBlockingHit()) Allowed = FMath::Min(Allowed, float(Gap + (Allowed - Gap) * Hit.Time));
    }
    return Allowed;
}

void UShipModulePhysics::UpdateContacts(float DeltaSeconds)
{
    UStaticMeshComponent* Panel = Surface.Get();
    if (!Panel || (ActiveLight != GreenLight && ActiveLight != BlueLight)) return;
    TimeUntilCheck -= DeltaSeconds;
    if (TimeUntilCheck > 0.f) return;
    TimeUntilCheck = CheckInterval;

    UWorld* World = GetWorld();
    if (!World || !Panel->IsRegistered()) return;
    const bool bBlue = ActiveLight == BlueLight;
    FVector Min, Max;
    Panel->GetLocalBounds(Min, Max);
    const FTransform& Transform = Panel->GetComponentTransform();
    const FVector Half = (Max - Min) * .5 * Transform.GetScale3D().GetAbs();
    const FVector Normal = Panel->GetForwardVector();
    const FVector Face = Transform.TransformPosition((Max + Min) * .5) + Normal * Half.X;
    const FVector QueryCenter = Face + Normal * (ContactDepth * .5f);

    FCollisionObjectQueryParams Objects;
    Objects.AddObjectTypesToQuery(ECC_PhysicsBody);
    if (bBlue)
    {
        Objects.AddObjectTypesToQuery(ECC_Pawn);
        Objects.AddObjectTypesToQuery(ECC_WorldDynamic);
    }
    TArray<FOverlapResult> Hits;
    World->OverlapMultiByObjectType(Hits, QueryCenter, Panel->GetComponentQuat(), Objects,
        FCollisionShape::MakeBox(FVector(ContactDepth * .5f, Half.Y, Half.Z)),
        FCollisionQueryParams(SCENE_QUERY_STAT(ShipModuleContact), false, GetOwner()));

    const double Now = World->GetTimeSeconds();
    for (auto It = LaunchCooldowns.CreateIterator(); It; ++It)
        if (!It.Key().IsValid() || It.Value() <= Now) It.RemoveCurrent();

    TSet<TWeakObjectPtr<UPrimitiveComponent>> CurrentContacts;
    for (const FOverlapResult& Hit : Hits)
    {
        UPrimitiveComponent* Body = Hit.GetComponent();
        AActor* Actor = Hit.GetActor();
        if (!Body || !Actor || FVector::DotProduct(Body->Bounds.Origin - Face, Normal) < 0.) continue;
        ACharacter* Character = Cast<ACharacter>(Actor);
        if (Character)
        {
            if (!bBlue || !Character->IsPlayerControlled() || Body != Character->GetCapsuleComponent()) continue;
        }
        else
        {
            if (!Body->IsSimulatingPhysics() || Body->GetAttachParent()) continue;
            const bool bCargo = Body->ComponentHasTag(CargoTag) || Actor->ActorHasTag(CargoTag);
            if ((!bCargo && (!bBlue || Body != Actor->GetRootComponent())) || IsHeld(Body)) continue;
            if (!bBlue)
            {
                Stick(Body, Face, Normal);
                continue;
            }
        }

        CurrentContacts.Add(Body);
        if (BlueContacts.Contains(Body) || LaunchCooldowns.Contains(Body)) continue;
        const FVector Velocity = Character ? Character->GetVelocity() : Body->GetPhysicsLinearVelocity();
        const double OutwardSpeed = FVector::DotProduct(Velocity, Normal);
        const FVector Boost = Normal * FMath::Max(0., LaunchSpeed - OutwardSpeed);
        if (Character)
            Character->LaunchCharacter(Velocity + Boost, true, true);
        else
            // Two coplanar lit panels can touch the same crate before the next Chaos step.
            Body->SetPhysicsLinearVelocity(Velocity + Boost);
        LaunchCooldowns.Add(Body, Now + LaunchCooldown);
        ++LaunchCount;
    }
    BlueContacts = MoveTemp(CurrentContacts);
}

void UShipModulePhysics::EndPlay(const EEndPlayReason::Type Reason)
{
    ReleaseCargo();
    Super::EndPlay(Reason);
}
