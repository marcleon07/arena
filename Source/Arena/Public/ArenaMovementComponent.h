#pragma once

#include "CoreMinimal.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "ArenaMovementComponent.generated.h"

/**
 * CharacterMovementComponent that replaces Unreal's velocity model with the
 * GoldSrc / Quake one (pm_shared.c: PM_Friction, PM_Accelerate, PM_AirAccelerate).
 *
 * That model is what makes bunnyhopping and air strafing work:
 *  - In the air, acceleration is only capped along the wish direction (30 u/s),
 *    so turning the mouse while strafing keeps adding speed.
 *  - Friction is skipped on the tick a jump is queued, so hopping on the
 *    landing frame keeps all horizontal speed.
 *
 * Everything is a pure function of (Velocity, Acceleration, bPressedJump), all of
 * which are part of Unreal's saved moves, so client prediction and server replay agree.
 */
UCLASS(Config = Game)
class ARENA_API UArenaMovementComponent : public UCharacterMovementComponent
{
	GENERATED_BODY()

public:
	UArenaMovementComponent();

	/** sv_accelerate */
	UPROPERTY(EditAnywhere, Config, Category = "Arena|Movement")
	float GroundAccelerate = 10.f;

	/** sv_airaccelerate. Half-Life default 10, Counter-Strike 1.6 servers often 100. */
	UPROPERTY(EditAnywhere, Config, Category = "Arena|Movement")
	float AirAccelerate = 10.f;

	/** sv_stopspeed, in cm/s. */
	UPROPERTY(EditAnywhere, Config, Category = "Arena|Movement")
	float StopSpeed;

	/** Wish-speed clamp used by PM_AirAccelerate (30 units), in cm/s. */
	UPROPERTY(EditAnywhere, Config, Category = "Arena|Movement")
	float AirSpeedCap;

	/**
	 * HL1 patch 1.1.1.0 added PM_PreventMegaBunnyJumping (scale speed back when jumping
	 * above 1.7x maxspeed). The original game had no cap, so it is off by default.
	 */
	UPROPERTY(EditAnywhere, Config, Category = "Arena|Movement")
	bool bCapBunnyhopSpeed = false;

	UPROPERTY(EditAnywhere, Config, Category = "Arena|Movement")
	float BunnyhopCapFactor = 1.7f;

	/** Horizontal speed in GoldSrc units per second, for the HUD speedometer. */
	float GetHorizontalSpeedUPS() const;

protected:
	virtual void CalcVelocity(float DeltaTime, float Friction, bool bFluid, float BrakingDeceleration) override;
	virtual FVector GetFallingLateralAcceleration(float DeltaTime) override;
	virtual FVector NewFallVelocity(const FVector& InitialVelocity, const FVector& Gravity, float DeltaTime) const override;
	virtual bool CanAttemptJump() const override;

private:
	void ApplyFriction(float DeltaTime, float Friction);
	void Accelerate(const FVector& WishDir, float WishSpeed, float Accel, float DeltaTime);
	void AirAccelerateQuake(const FVector& WishDir, float WishSpeed, float Accel, float DeltaTime);
	void PreventMegaBunnyJumping();
};
