#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameMode.h"
#include "ArenaTypes.h"
#include "ArenaGameMode.generated.h"

class AArenaPlayerController;
class AArenaPlayerState;

/**
 * Free-for-all deathmatch. Options on the URL override config, e.g.
 *   open /Engine/Maps/Entry?listen?FragLimit=30?TimeLimit=15
 */
UCLASS(Config = Game)
class ARENA_API AArenaGameMode : public AGameMode
{
	GENERATED_BODY()

public:
	AArenaGameMode();

	virtual void InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage) override;
	virtual void StartPlay() override;
	virtual void PostLogin(APlayerController* NewPlayer) override;
	virtual void Tick(float DeltaSeconds) override;
	virtual AActor* ChoosePlayerStart_Implementation(AController* Player) override;
	virtual bool ShouldSpawnAtStartSpot(AController* Player) override { return false; }
	virtual bool PlayerCanRestart_Implementation(APlayerController* Player) override;

	void OnPlayerKilled(AController* Killer, AController* Victim, EArenaWeapon Weapon);

	/** Respawns a dead player once AutoRespawnDelay (or ClickRespawnDelay if bRequested) has passed. */
	void TryRespawn(AArenaPlayerController* PC, bool bRequested);

	UPROPERTY(Config)
	int32 FragLimit = 20;

	UPROPERTY(Config)
	float TimeLimitMinutes = 10.f;

	/** Dead players respawn automatically after this long; pressing fire respawns after ClickRespawnDelay. */
	UPROPERTY(Config)
	float AutoRespawnDelay = 3.f;

	UPROPERTY(Config)
	float ClickRespawnDelay = 1.f;

protected:
	virtual void HandleMatchHasStarted() override;
	virtual void HandleMatchHasEnded() override;

	void FinishMatch(AArenaPlayerState* Winner);
	void SendFragMessages(AController* Killer, AController* Victim, AArenaPlayerState* KillerPS, AArenaPlayerState* VictimPS);
	void SpawnPickups();
	void EnsureSpawnPoints();

	UPROPERTY(Transient)
	TArray<TObjectPtr<AActor>> SpawnPoints;

private:
	int32 NextColorIndex = 0;
	FTimerHandle RestartTimer;
};
