#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ArenaShowcase.generated.h"

class ACameraActor;
class AArenaCharacter;

/**
 * Development tool: stages a scene, saves screenshots to Saved/Showcase and quits.
 * Start a listen server with -ArenaShowcase=<Scene> (see Scripts/Showcase.bat):
 *   poses    characters running, strafing, crouching, jumping and aiming, from three angles
 *   view     the first-person view of every weapon
 *   items    close-ups of the pickups
 *   ragdoll  a character killed mid-run
 *   map      overview and player's-eye shots of the current map
 *   bots     a chase camera on bots in a live match (add ?Bots=N)
 */
UCLASS(NotPlaceable, Transient)
class ARENA_API AArenaShowcase : public AActor
{
	GENERATED_BODY()

public:
	AArenaShowcase();

	/** Spawns the showcase if the command line asks for one. */
	static void StartFromCommandLine(UWorld* World);

	virtual void Tick(float DeltaSeconds) override;

private:
	void Build();
	void Then(float Delay, TFunction<void()> Action);
	void Shot(const FString& Name);
	void Look(const FVector& From, const FVector& At);
	AArenaCharacter* SpawnPoser(const FVector& Feet, float Yaw, const FLinearColor& Color, int32 Weapon);
	APlayerController* GetPlayer() const;

	FString Scene;

	UPROPERTY()
	TObjectPtr<ACameraActor> Camera;

	TWeakObjectPtr<AArenaCharacter> Follow;

	struct FStep
	{
		float Time;
		TFunction<void()> Action;
	};
	TArray<FStep> Steps;
	float Clock = 0.f;
	float ScriptEnd = 0.f;
	float WaitLog = 0.f;
	bool bBuilt = false;
};
