#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ArenaTypes.h"
#include "ArenaProjectile.generated.h"

class USphereComponent;
class UStaticMeshComponent;
class UProjectileMovementComponent;

/**
 * Replicated projectile with Quake-style direct + splash damage. Subclasses only set
 * tuning values in their constructors. The server decides hits and explosions;
 * clients just simulate the flight.
 */
UCLASS(Abstract)
class ARENA_API AArenaProjectile : public AActor
{
	GENERATED_BODY()

public:
	AArenaProjectile();

	static TSubclassOf<AArenaProjectile> ClassForWeapon(EArenaWeapon Weapon);

protected:
	virtual void BeginPlay() override;

	UFUNCTION()
	void OnStop(const FHitResult& Hit);

	UFUNCTION()
	void OnBounce(const FHitResult& Hit, const FVector& ImpactVelocity);

	void Explode(const FVector& Location, AActor* DirectHit);

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USphereComponent> Collision;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UStaticMeshComponent> Mesh;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UProjectileMovementComponent> Movement;

	// Tuning, set by subclasses.
	EArenaWeapon Weapon = EArenaWeapon::RocketLauncher;
	float DirectDamage = 100.f;
	float SplashDamage = 100.f;
	float SplashRadius = QU(120.f);
	/** Seconds until it explodes by itself; 0 = only on impact. */
	float FuseTime = 0.f;
	FLinearColor Color = FLinearColor::White;
	EArenaSound ExplodeSound = EArenaSound::RocketExplode;
	float BlastScale = 0.8f;

private:
	/** Kept separately so kills still count if the shooter dies before impact. */
	TWeakObjectPtr<AController> ShooterController;
	FTimerHandle FuseTimer;
	bool bExploded = false;
};

/** 900 u/s, 100 direct + 100 splash over 120 units. */
UCLASS()
class ARENA_API AArenaRocket : public AArenaProjectile
{
	GENERATED_BODY()

public:
	AArenaRocket();
};

/** 700 u/s lob with gravity, bounces, 2.5 s fuse, explodes on touching a player. */
UCLASS()
class ARENA_API AArenaGrenade : public AArenaProjectile
{
	GENERATED_BODY()

public:
	AArenaGrenade();
};

/** 2000 u/s, 20 direct + 15 splash over 20 units: small, but enough to plasma-climb. */
UCLASS()
class ARENA_API AArenaPlasma : public AArenaProjectile
{
	GENERATED_BODY()

public:
	AArenaPlasma();
};
