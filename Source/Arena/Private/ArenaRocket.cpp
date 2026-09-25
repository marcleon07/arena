#include "ArenaRocket.h"
#include "ArenaAudio.h"
#include "ArenaCharacter.h"
#include "ArenaGameState.h"
#include "ArenaTypes.h"
#include "ArenaVisuals.h"
#include "Components/CapsuleComponent.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "EngineUtils.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	const float SplashRadius = QU(120.f);
	const FLinearColor RocketColor(1.f, 0.45f, 0.1f);
}

AArenaRocket::AArenaRocket()
{
	bReplicates = true;
	SetReplicatingMovement(true);
	InitialLifeSpan = 10.f;

	static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereMesh(TEXT("/Engine/BasicShapes/Sphere.Sphere"));

	Collision = CreateDefaultSubobject<USphereComponent>(TEXT("Collision"));
	Collision->InitSphereRadius(8.f);
	Collision->SetCollisionObjectType(ECC_WorldDynamic);
	Collision->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Collision->SetCollisionResponseToAllChannels(ECR_Ignore);
	Collision->SetCollisionResponseToChannel(ECC_WorldStatic, ECR_Block);
	Collision->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);
	RootComponent = Collision;

	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	Mesh->SetupAttachment(Collision);
	ArenaVisuals::SetupCosmeticMesh(Mesh, SphereMesh.Object);
	Mesh->SetRelativeScale3D(FVector(0.35f, 0.18f, 0.18f));

	Movement = CreateDefaultSubobject<UProjectileMovementComponent>(TEXT("Movement"));
	Movement->UpdatedComponent = Collision;
	Movement->InitialSpeed = QU(900.f);
	Movement->MaxSpeed = QU(900.f);
	Movement->ProjectileGravityScale = 0.f;
	Movement->bRotationFollowsVelocity = true;
	Movement->bShouldBounce = false;
}

void AArenaRocket::BeginPlay()
{
	Super::BeginPlay();

	ArenaVisuals::SetColor(Mesh, RocketColor);
	APawn* Shooter = GetInstigator();
	if (Shooter)
	{
		Collision->IgnoreActorWhenMoving(Shooter, true);
		ShooterController = Shooter->GetController();
	}
	// The shooter heard their own launch when they pulled the trigger.
	if (!Shooter || !Shooter->IsLocallyControlled())
	{
		UArenaAudio::PlayAt(this, EArenaSound::RocketFire, GetActorLocation());
	}
	Movement->OnProjectileStop.AddDynamic(this, &AArenaRocket::OnStop);
}

void AArenaRocket::OnStop(const FHitResult& Hit)
{
	if (HasAuthority())
	{
		Explode(Hit.ImpactPoint + Hit.ImpactNormal * 2.f, Hit.GetActor());
	}
	else
	{
		// Clients wait for the server's explosion; just stop drawing the rocket.
		Mesh->SetVisibility(false);
	}
}

void AArenaRocket::Explode(const FVector& Location, AActor* DirectHit)
{
	if (bExploded)
	{
		return;
	}
	bExploded = true;

	UWorld* World = GetWorld();
	AController* Shooter = ShooterController.Get();
	const FVector Forward = GetVelocity().GetSafeNormal();

	AArenaCharacter* DirectVictim = Cast<AArenaCharacter>(DirectHit);
	if (DirectVictim)
	{
		DirectVictim->ApplyArenaDamage(DirectDamage, Shooter, ArenaKnockback(Forward, DirectDamage), EArenaWeapon::RocketLauncher, Location);
	}

	// Splash, measured to the nearest point of each capsule (like Quake's bbox test),
	// so a rocket at your feet does full damage and gives a full rocket jump.
	FCollisionQueryParams LosParams(SCENE_QUERY_STAT(ArenaSplashLOS), false, this);
	for (TActorIterator<AArenaCharacter> It(World); It; ++It)
	{
		AArenaCharacter* Victim = *It;
		if (Victim == DirectVictim || Victim->IsDead())
		{
			continue;
		}

		const UCapsuleComponent* Capsule = Victim->GetCapsuleComponent();
		const float Radius = Capsule->GetScaledCapsuleRadius();
		const float HalfSegment = Capsule->GetScaledCapsuleHalfHeight() - Radius;
		const FVector Center = Victim->GetActorLocation();
		const FVector Closest = FMath::ClosestPointOnSegment(Location, Center - FVector(0.f, 0.f, HalfSegment), Center + FVector(0.f, 0.f, HalfSegment));
		const float Dist = FMath::Max(0.f, FVector::Dist(Location, Closest) - Radius);
		if (Dist >= SplashRadius)
		{
			continue;
		}

		LosParams.ClearIgnoredActors();
		LosParams.AddIgnoredActor(this);
		if (World->LineTraceTestByObjectType(Location, Center, FCollisionObjectQueryParams(ECC_WorldStatic), LosParams))
		{
			continue;
		}

		const float Points = SplashDamage * (1.f - Dist / SplashRadius);
		// Quake 3 biases splash knockback upward (dir.z += 24) to make rocket jumps pop.
		const FVector Dir = (Center - Location) + FVector(0.f, 0.f, QU(24.f));
		Victim->ApplyArenaDamage(Points, Shooter, ArenaKnockback(Dir, Points), EArenaWeapon::RocketLauncher, Location);
	}

	if (AArenaGameState* GS = World->GetGameState<AArenaGameState>())
	{
		GS->MulticastExplosion(Location, RocketColor, SplashRadius * 0.8f, EArenaSound::RocketExplode);
	}
	Destroy();
}
