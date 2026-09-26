#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ArenaTypes.h"
#include "ArenaPickup.generated.h"

class USphereComponent;
class UStaticMeshComponent;

/** Server-spawned, replicated item that respawns on a timer (Quake 3 timings). */
UCLASS()
class ARENA_API AArenaPickup : public AActor
{
	GENERATED_BODY()

public:
	AArenaPickup();

	/** Server: must be called right after spawning. */
	void InitPickup(EArenaPickupType InType);

	bool IsAvailable() const { return bAvailable; }
	EArenaPickupType GetPickupType() const { return Type; }

	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
	virtual void BeginPlay() override;

	UFUNCTION()
	void OnOverlap(UPrimitiveComponent* OverlappedComp, AActor* Other, UPrimitiveComponent* OtherComp, int32 BodyIndex, bool bFromSweep, const FHitResult& Sweep);

	UFUNCTION()
	void OnRep_Type();

	UFUNCTION()
	void OnRep_Available();

	bool TryGive(class AArenaCharacter* Character) const;
	float GetRespawnTime() const;
	void Respawn();

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USphereComponent> Trigger;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UStaticMeshComponent> Mesh;

	/** Pedestal on the floor; its ring glows while the item is there. */
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UStaticMeshComponent> Base;

	UPROPERTY(ReplicatedUsing = OnRep_Type)
	EArenaPickupType Type = EArenaPickupType::Health;

	UPROPERTY(ReplicatedUsing = OnRep_Available)
	bool bAvailable = true;

private:
	FTimerHandle RespawnTimer;
	float SpinTime = 0.f;
	/** Offset that keeps the model spinning about its middle rather than its origin. */
	FVector SpinPivot = FVector::ZeroVector;
};
