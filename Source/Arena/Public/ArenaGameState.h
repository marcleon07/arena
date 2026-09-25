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

	/** True for a standalone world: the main menu, before hosting or joining. */
	static bool IsMenuWorld(const UWorld* World);

	/** Seconds left in the match, or 0. */
	float GetTimeRemaining() const;

	/** Players ordered by frags (desc), then deaths (asc). */
	TArray<AArenaPlayerState*> GetSortedPlayers() const;

	const TArray<FArenaKillFeedEntry>& GetKillFeed() const { return KillFeed; }

	/** Draws tracers / rail trail / lightning beam for a hitscan shot on this machine. */
	static void SpawnShotVisual(UWorld* World, const AArenaCharacter* Shooter, EArenaWeapon Weapon, const TArray<FVector_NetQuantize>& Ends);

	/** One end point per pellet (shotgun) or one for single-trace weapons. */
	UFUNCTION(NetMulticast, Unreliable)
	void MulticastShot(AArenaCharacter* Shooter, EArenaWeapon Weapon, const TArray<FVector_NetQuantize>& Ends);

	UFUNCTION(NetMulticast, Unreliable)
	void MulticastExplosion(FVector_NetQuantize Location, FLinearColor Color, float Radius, EArenaSound Sound);

	UFUNCTION(NetMulticast, Reliable)
	void MulticastKill(const FString& Killer, const FString& Victim, EArenaWeapon Weapon);

private:
	TArray<FArenaKillFeedEntry> KillFeed;
};
