#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ArenaTypes.h"
#include "ArenaMap.generated.h"

class UBoxComponent;
class UStaticMeshComponent;

/** Solid box of level geometry. Spawned locally on every machine, never replicated. */
UCLASS(NotPlaceable, Transient)
class ARENA_API AArenaBlock : public AActor
{
	GENERATED_BODY()

public:
	AArenaBlock();
	void Init(const FVector& HalfExtent, const FLinearColor& Color);

private:
	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> Mesh;
};

/**
 * Launches players toward a target. Runs on the server and on the owning client so
 * the launch is predicted, the same way Quake 3 jump pads are.
 */
UCLASS(NotPlaceable, Transient)
class ARENA_API AArenaJumpPad : public AActor
{
	GENERATED_BODY()

public:
	AArenaJumpPad();
	void Init(const FVector& InLaunchVelocity);

private:
	UFUNCTION()
	void OnOverlap(UPrimitiveComponent* OverlappedComp, AActor* Other, UPrimitiveComponent* OtherComp, int32 BodyIndex, bool bFromSweep, const FHitResult& Sweep);

	UPROPERTY()
	TObjectPtr<UBoxComponent> Trigger;

	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> Mesh;

	FVector LaunchVelocity = FVector::ZeroVector;
};

struct FArenaPickupSpot
{
	EArenaPickupType Type;
	FVector Location;
};

/** The map is defined in code: one layout shared by server and clients. */
namespace ArenaMap
{
	/** Player spawn points (feet location + yaw facing the middle). */
	ARENA_API const TArray<FTransform>& GetSpawnPoints();

	ARENA_API const TArray<FArenaPickupSpot>& GetPickupSpots();

	/** Spawns geometry, jump pads and lighting on this machine. */
	ARENA_API void BuildLocal(UWorld* World);
}
