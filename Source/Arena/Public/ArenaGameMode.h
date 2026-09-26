#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameMode.h"
#include "ArenaBotNav.h"
#include "ArenaTypes.h"
#include "ArenaGameMode.generated.h"

class AArenaBotController;
class AArenaPlayerController;
class AArenaPlayerState;

/**
 * Free-for-all deathmatch. Options on the URL override config, e.g.
 *   open /Engine/Maps/Entry?listen?Arena=Skyline?FragLimit=30?TimeLimit=15?Bots=4?BotSkill=3
 * When a match ends, players vote on the next map for VoteDuration seconds.
 */
UCLASS(Config = Game)
class ARENA_API AArenaGameMode : public AGameMode
{
	GENERATED_BODY()

public:
	AArenaGameMode();

	virtual void InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage) override;
	virtual void InitGameState() override;
	virtual void StartPlay() override;
	virtual void PostLogin(APlayerController* NewPlayer) override;
	virtual void Tick(float DeltaSeconds) override;
	virtual AActor* ChoosePlayerStart_Implementation(AController* Player) override;
	virtual bool ShouldSpawnAtStartSpot(AController* Player) override { return false; }
	virtual bool PlayerCanRestart_Implementation(APlayerController* Player) override;
	virtual bool ReadyToStartMatch_Implementation() override;

	void OnPlayerKilled(AController* Killer, AController* Victim, EArenaWeapon Weapon);

	/** Server: a player picked a map in the end-of-match vote. */
	void CastVote(APlayerController* Voter, FName Map);

	FName GetMapId() const { return MapId; }

	/** Adds a bot (skill 1-5; <= 0 uses BotSkill). Returns false if the server is full. */
	bool AddBot(int32 Skill = 0);
	void RemoveBot();
	int32 GetNumBots() const { return Bots.Num(); }

	/** Waypoint graph for bots, built on first use for the current map. */
	const FArenaBotNav* GetBotNav();

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

	/** Bots added when a match starts (URL option Bots=N). */
	UPROPERTY(Config)
	int32 BotCount = 0;

	/** 1 (easy) to 5 (hard); URL option BotSkill=N. */
	UPROPERTY(Config)
	int32 BotSkill = 3;

	/** Seconds the end-of-match scoreboard and map vote stay up. */
	UPROPERTY(Config)
	float VoteDuration = 15.f;

protected:
	virtual void HandleMatchHasStarted() override;
	virtual void HandleMatchHasEnded() override;

	void FinishMatch(AArenaPlayerState* Winner);
	void SendFragMessages(AController* Killer, AController* Victim, AArenaPlayerState* KillerPS, AArenaPlayerState* VictimPS, EArenaWeapon Weapon);
	void SpawnPickups();
	void EnsureSpawnPoints();
	void StartMapVote();
	void FinishMapVote();

	UPROPERTY(Transient)
	TArray<TObjectPtr<AActor>> SpawnPoints;

	UPROPERTY(Transient)
	TArray<TObjectPtr<AArenaBotController>> Bots;

	FArenaBotNav BotNav;

private:
	FName MapId;
	int32 NextColorIndex = 0;
	FTimerHandle RestartTimer;
};
