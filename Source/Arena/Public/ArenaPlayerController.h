#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "ArenaTypes.h"
#include "ArenaPlayerController.generated.h"

class UInputAction;
class UInputMappingContext;

/**
 * Owns the input actions and mapping context (built in code, no assets needed),
 * client-side preferences (autohop, sensitivity) and the respawn flow.
 */
UCLASS(Config = Game)
class ARENA_API AArenaPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	AArenaPlayerController();

	virtual void PostInitializeComponents() override;
	virtual void SetupInputComponent() override;
	virtual void OnUnPossess() override;

	/** Holding jump re-jumps on landing. Turn off for scroll-wheel HL1 purism. */
	UPROPERTY(Config)
	bool bAutoHop = true;

	/** Quake-style sensitivity: degrees per mouse count = 0.022 * Sensitivity. */
	UPROPERTY(Config)
	float Sensitivity = 2.5f;

	bool IsScoreboardHeld() const { return bScoreboardHeld; }

	// Console commands
	UFUNCTION(Exec)
	void AutoHop(int32 bEnabled);

	UFUNCTION(Exec)
	void Sens(float NewSensitivity);

	UFUNCTION(Exec)
	void AirAccel(float Value);

	UPROPERTY(Transient)
	TObjectPtr<UInputMappingContext> MappingContext;

	UPROPERTY(Transient) TObjectPtr<UInputAction> MoveForwardAction;
	UPROPERTY(Transient) TObjectPtr<UInputAction> MoveRightAction;
	UPROPERTY(Transient) TObjectPtr<UInputAction> LookAction;
	UPROPERTY(Transient) TObjectPtr<UInputAction> JumpAction;
	UPROPERTY(Transient) TObjectPtr<UInputAction> JumpWheelAction;
	UPROPERTY(Transient) TObjectPtr<UInputAction> CrouchAction;
	UPROPERTY(Transient) TObjectPtr<UInputAction> FireAction;
	UPROPERTY(Transient) TObjectPtr<UInputAction> NextWeaponAction;
	UPROPERTY(Transient) TObjectPtr<UInputAction> LastWeaponAction;
	UPROPERTY(Transient) TObjectPtr<UInputAction> ScoreboardAction;
	UPROPERTY(Transient) TArray<TObjectPtr<UInputAction>> WeaponActions;

	/** Server time the pawn died, for the respawn delay. */
	float DeathTime = -1.f;

	/** Server -> shooter: you hit someone (hit sound, marker, damage number). */
	UFUNCTION(Client, Unreliable)
	void ClientHitConfirmed(FVector_NetQuantize VictimLocation, int32 Damage, bool bKilled);

	/** Server -> victim: you took damage from SourceLocation (flash, direction indicator). */
	UFUNCTION(Client, Unreliable)
	void ClientTookDamage(FVector_NetQuantize SourceLocation, int32 Damage);

	/** Server -> killer or victim: centered frag message. bGood plays the frag sound. */
	UFUNCTION(Client, Reliable)
	void ClientFragMessage(const FString& Text, bool bGood);

protected:
	virtual void BeginPlay() override;

	void BuildInput();
	void OnFirePressed();
	void OnScoreboardPressed() { bScoreboardHeld = true; }
	void OnScoreboardReleased() { bScoreboardHeld = false; }

	UFUNCTION(Server, Reliable)
	void ServerRequestRespawn();

	UFUNCTION(Server, Reliable)
	void ServerSetAirAccel(float Value);

private:
	bool bScoreboardHeld = false;
};
