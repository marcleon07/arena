#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameState.h"
#include "ArenaTypes.h"
#include "ArenaGameState.generated.h"

class AArenaCharacter;
class AArenaPlayerState;

struct FArenaKillFeedEntry
{
	FString Killer;
	FString Victim;
	EArenaWeapon Weapon;
	float Time;
};

UCLASS()
class ARENA_API AArenaGameState : public AGameState
{
	GENERATED_BODY()

public:
	virtual void BeginPlay() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** sv_airaccelerate, owned by the server so prediction matches everywhere. */
	UPROPERTY(Replicated)
	float AirAccelerate = 10.f;

	UPROPERTY(Replicated)
	int32 FragLimit = 20;

	/** Server world time when the match ends by time limit. */
	UPROPERTY(Replicated)
	float MatchEndTime = 0.f;

	UPROPERTY(Replicated)
	FString WinnerName;

	/** Seconds left in the match, or 0. */
	float GetTimeRemaining() const;

	/** Players ordered by frags (desc), then deaths (asc). */
	TArray<AArenaPlayerState*> GetSortedPlayers() const;

	const TArray<FArenaKillFeedEntry>& GetKillFeed() const { return KillFeed; }

	/** Draws the tracer / rail trail for a hitscan shot on this machine. */
	static void SpawnShotVisual(UWorld* World, const AArenaCharacter* Shooter, EArenaWeapon Weapon, const FVector& End);

	UFUNCTION(NetMulticast, Unreliable)
	void MulticastShot(AArenaCharacter* Shooter, EArenaWeapon Weapon, FVector_NetQuantize End);

	UFUNCTION(NetMulticast, Unreliable)
	void MulticastExplosion(FVector_NetQuantize Location, FLinearColor Color, float Radius);

	UFUNCTION(NetMulticast, Reliable)
	void MulticastKill(const FString& Killer, const FString& Victim, EArenaWeapon Weapon);

private:
	TArray<FArenaKillFeedEntry> KillFeed;
};
