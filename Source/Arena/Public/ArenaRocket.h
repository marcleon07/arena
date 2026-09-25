#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ArenaRocket.generated.h"

class USphereComponent;
class UStaticMeshComponent;
class UProjectileMovementComponent;

/** Quake 3 rocket: 900 u/s, 100 direct damage, 100 splash over 120 units. */
UCLASS()
class ARENA_API AArenaRocket : public AActor
{
	GENERATED_BODY()

public:
	AArenaRocket();

	static constexpr float DirectDamage = 100.f;
	static constexpr float SplashDamage = 100.f;

protected:
	virtual void BeginPlay() override;

	UFUNCTION()
	void OnStop(const FHitResult& Hit);

	void Explode(const FVector& Location, AActor* DirectHit);

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USphereComponent> Collision;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UStaticMeshComponent> Mesh;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UProjectileMovementComponent> Movement;

private:
	/** Kept separately so kills still count if the shooter dies before impact. */
	TWeakObjectPtr<AController> ShooterController;
	bool bExploded = false;
};
