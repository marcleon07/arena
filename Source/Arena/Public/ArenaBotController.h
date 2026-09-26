#pragma once

#include "CoreMinimal.h"
#include "AIController.h"
#include "ArenaTypes.h"
#include "ArenaBotController.generated.h"

class AArenaCharacter;
class AArenaPickup;
class FArenaBotNav;

/**
 * Server-side deathmatch bot. Each tick it:
 *  - picks the nearest enemy it can see (after a skill-based reaction delay),
 *  - chooses a weapon for the range, aims with skill-based error and turn speed
 *    (leading projectiles), fires when on target, and strafes;
 *  - otherwise walks the waypoint graph to the most useful item, or roams.
 * It drives the pawn through the same inputs a player has (movement input,
 * control rotation, jump, fire), so it moves with the same bhop physics.
 */
UCLASS()
class ARENA_API AArenaBotController : public AAIController
{
	GENERATED_BODY()

public:
	AArenaBotController();

	/** Skill 1 (easy) to 5 (hard). */
	void InitBot(int32 InSkill);
	int32 GetSkill() const { return Skill; }

	virtual void Tick(float DeltaSeconds) override;

	/** Server time the bot's pawn died, for the respawn delay. */
	float DeathTime = -1.f;

protected:
	virtual void OnPossess(APawn* InPawn) override;
	virtual void OnUnPossess() override;

private:
	AArenaCharacter* GetBot() const;
	const FArenaBotNav* GetNav() const;

	void UpdateEnemy(AArenaCharacter* Bot, float Now);
	bool CanSee(const AArenaCharacter* Bot, const AArenaCharacter* Target) const;
	void ChooseWeapon(AArenaCharacter* Bot, float Distance);
	void AimAndFire(AArenaCharacter* Bot, float DeltaSeconds, float Now);
	void MoveInCombat(AArenaCharacter* Bot, float Now);
	void MoveAlongPath(AArenaCharacter* Bot, float Now);
	/** Advances along the path (repathing when needed) and returns the direction to the current node. */
	FVector UpdatePath(AArenaCharacter* Bot, float Now);
	/** Only a machinegun/gauntlet, or hurt: worth collecting items even mid-fight. */
	bool NeedsItems(const AArenaCharacter* Bot) const;
	void PickGoal(AArenaCharacter* Bot);
	float ScorePickup(const AArenaCharacter* Bot, const AArenaPickup* Pickup) const;
	void Steer(AArenaCharacter* Bot, FVector Direction);
	/** Ground within a safe drop at Distance along Direction (default scales with speed). */
	bool HasGroundAhead(const AArenaCharacter* Bot, const FVector& Direction, float Distance = -1.f) const;
	void TurnToward(AArenaCharacter* Bot, const FRotator& Desired, float DeltaSeconds);

	int32 Skill = 3;

	TWeakObjectPtr<AArenaCharacter> Enemy;
	float EnemyAcquiredTime = 0.f;
	float EnemyLastSeenTime = -100.f;
	FVector EnemyLastSeenLocation = FVector::ZeroVector;

	TArray<FVector> Path;
	int32 PathIndex = 0;
	float NextRepathTime = 0.f;

	float StrafeSign = 1.f;
	float NextStrafeSwitch = 0.f;
	FRotator AimError = FRotator::ZeroRotator;
	float NextAimErrorTime = 0.f;
	float NextWeaponCheck = 0.f;

	FVector LastProgressLocation = FVector::ZeroVector;
	float LastProgressTime = 0.f;
};
