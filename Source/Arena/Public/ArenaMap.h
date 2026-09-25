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

/**
 * One arena layout. Maps are defined in code and all load on the same engine level;
 * the server picks one with the "?Arena=<Id>" URL option and replicates the choice,
 * then every machine builds the same geometry locally.
 */
struct FArenaMapDef
{
	FName Id;
	FText DisplayName;
	FText Description;
	void (*BuildGeometry)(UWorld* World);
	/** Player spawns (capsule centre + yaw facing the middle). */
	TArray<FTransform> Spawns;
	TArray<FArenaPickupSpot> Pickups;
	/** Falling below this height kills you (void maps). */
	float KillZ;
	// Main-menu camera orbit around the map.
	float MenuOrbitRadius;
	float MenuOrbitHeight;
};

namespace ArenaMap
{
	ARENA_API const TArray<FArenaMapDef>& GetMaps();

	/** The map with this id, or the first map if unknown. */
	ARENA_API const FArenaMapDef& Get(FName Id);

	/** Spawns the map's geometry, jump pads and lighting on this machine. */
	ARENA_API void BuildLocal(UWorld* World, FName MapId);
}
