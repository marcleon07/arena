#include "ArenaProjectile.h"
#include "ArenaAudio.h"
#include "ArenaCharacter.h"
#include "ArenaGameState.h"
#include "ArenaVisuals.h"
#include "Components/CapsuleComponent.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "EngineUtils.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"

TSubclassOf<AArenaProjectile> AArenaProjectile::ClassForWeapon(EArenaWeapon Weapon)
{
	switch (Weapon)
	{
	case EArenaWeapon::GrenadeLauncher: return AArenaGrenade::StaticClass();
	case EArenaWeapon::PlasmaGun:       return AArenaPlasma::StaticClass();
	default:                            return AArenaRocket::StaticClass();
	}
}

AArenaProjectile::AArenaProjectile()
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

	Movement = CreateDefaultSubobject<UProjectileMovementComponent>(TEXT("Movement"));
	Movement->UpdatedComponent = Collision;
	Movement->ProjectileGravityScale = 0.f;
	Movement->bRotationFollowsVelocity = true;
	Movement->bShouldBounce = false;
}

void AArenaProjectile::BeginPlay()
{
	Super::BeginPlay();

	ArenaVisuals::SetColor(Mesh, Color);
	APawn* Shooter = GetInstigator();
	if (Shooter)
	{
		Collision->IgnoreActorWhenMoving(Shooter, true);
		ShooterController = Shooter->GetController();
	}
	// The shooter heard their own shot when they pulled the trigger.
	const AArenaCharacter* ShooterCharacter = Cast<AArenaCharacter>(Shooter);
	if (!ShooterCharacter || !ShooterCharacter->IsLocalPlayerView())
	{
		UArenaAudio::PlayAt(this, GetWeaponInfo(Weapon).FireSound, GetActorLocation());
	}

	Movement->OnProjectileStop.AddDynamic(this, &AArenaProjectile::OnStop);
	Movement->OnProjectileBounce.AddDynamic(this, &AArenaProjectile::OnBounce);

	if (HasAuthority() && FuseTime > 0.f)
	{
		GetWorldTimerManager().SetTimer(FuseTimer, FTimerDelegate::CreateWeakLambda(this, [this]
		{
			Explode(GetActorLocation(), nullptr);
		}), FuseTime, false);
	}
}

void AArenaProjectile::OnStop(const FHitResult& Hit)
{
	// A bouncing projectile stops when it comes to rest; it waits for its fuse.
	if (Movement->bShouldBounce)
	{
		return;
	}
	if (HasAuthority())
	{
		Explode(Hit.ImpactPoint + Hit.ImpactNormal * 2.f, Hit.GetActor());
	}
	else
	{
		// Clients wait for the server's explosion; just stop drawing the projectile.
		Mesh->SetVisibility(false);
	}
}

void AArenaProjectile::OnBounce(const FHitResult& Hit, const FVector& ImpactVelocity)
{
	if (Cast<AArenaCharacter>(Hit.GetActor()))
	{
		if (HasAuthority())
		{
			Explode(GetActorLocation(), Hit.GetActor());
		}
		return;
	}
	if (ImpactVelocity.SizeSquared() > FMath::Square(QU(100.f)))
	{
		UArenaAudio::PlayAt(this, EArenaSound::GrenadeBounce, GetActorLocation(), 0.7f, FMath::FRandRange(0.9f, 1.1f));
	}
}

void AArenaProjectile::Explode(const FVector& Location, AActor* DirectHit)
{
	if (bExploded)
	{
		return;
	}
	bExploded = true;
	GetWorldTimerManager().ClearTimer(FuseTimer);

	UWorld* World = GetWorld();
	AController* Shooter = ShooterController.Get();
	const FVector Forward = GetVelocity().GetSafeNormal();

	AArenaCharacter* DirectVictim = Cast<AArenaCharacter>(DirectHit);
	if (DirectVictim)
	{
		DirectVictim->ApplyArenaDamage(DirectDamage, Shooter, ArenaKnockback(Forward, DirectDamage), Weapon, Location);
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
		Victim->ApplyArenaDamage(Points, Shooter, ArenaKnockback(Dir, Points), Weapon, Location);
	}

	if (AArenaGameState* GS = World->GetGameState<AArenaGameState>())
	{
		GS->MulticastExplosion(Location, Color, FMath::Max(SplashRadius * BlastScale, 30.f), ExplodeSound);
	}
	Destroy();
}

// ---------------------------------------------------------------------------
// Subclasses: tuning only
// ---------------------------------------------------------------------------

AArenaRocket::AArenaRocket()
{
	Weapon = EArenaWeapon::RocketLauncher;
	Color = FLinearColor(1.f, 0.45f, 0.1f);
	Mesh->SetRelativeScale3D(FVector(0.35f, 0.18f, 0.18f));
	Movement->InitialSpeed = Movement->MaxSpeed = QU(900.f);
}

AArenaGrenade::AArenaGrenade()
{
	Weapon = EArenaWeapon::GrenadeLauncher;
	SplashRadius = QU(150.f);
	FuseTime = 2.5f;
	Color = FLinearColor(0.2f, 0.45f, 0.1f);
	Mesh->SetRelativeScale3D(FVector(0.22f));
	Movement->InitialSpeed = Movement->MaxSpeed = QU(700.f);
	Movement->ProjectileGravityScale = QU(800.f) / 980.f; // sv_gravity 800
	Movement->bShouldBounce = true;
	Movement->Bounciness = 0.55f;
	Movement->Friction = 0.25f;
	Movement->bRotationFollowsVelocity = false;
}

AArenaPlasma::AArenaPlasma()
{
	Weapon = EArenaWeapon::PlasmaGun;
	DirectDamage = 20.f;
	SplashDamage = 15.f;
	SplashRadius = QU(20.f);
	Color = FLinearColor(0.4f, 0.5f, 1.f);
	ExplodeSound = EArenaSound::PlasmaExplode;
	BlastScale = 1.5f;
	Collision->InitSphereRadius(5.f);
	Mesh->SetRelativeScale3D(FVector(0.16f));
	Movement->InitialSpeed = Movement->MaxSpeed = QU(2000.f);
}
