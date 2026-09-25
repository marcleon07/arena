#include "ArenaMovementComponent.h"
#include "ArenaGameState.h"
#include "ArenaTypes.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/PhysicsVolume.h"

UArenaMovementComponent::UArenaMovementComponent()
{
	// GoldSrc defaults converted to centimetres (see ArenaTypes.h).
	MaxWalkSpeed = QU(320.f);                  // sv_maxspeed / cl_forwardspeed
	MaxWalkSpeedCrouched = QU(320.f) * 0.333f; // PLAYER_DUCKING_MULTIPLIER
	GroundFriction = 4.f;                      // sv_friction
	StopSpeed = QU(100.f);
	AirSpeedCap = QU(30.f);
	GravityScale = QU(800.f) / 980.f;          // sv_gravity 800
	JumpZVelocity = FMath::Sqrt(2.f * QU(800.f) * QU(45.f)); // 45 unit jump height, ~268 u/s
	AirControl = 1.f;
	FallingLateralFriction = 0.f;
	BrakingDecelerationWalking = 0.f;
	BrakingDecelerationFalling = 0.f;
	MaxAcceleration = 10000.f; // Only used as the input-magnitude scale; see CalcVelocity.
	MaxStepHeight = QU(18.f);  // STEPSIZE
	SetWalkableFloorAngle(45.573f); // GoldSrc: normal.z >= 0.7

	bCanWalkOffLedgesWhenCrouching = true;
	NavAgentProps.bCanCrouch = true;
	NavAgentProps.bCanJump = true;
	bOrientRotationToMovement = false;
	bUseSeparateBrakingFriction = false;
}

float UArenaMovementComponent::GetHorizontalSpeedUPS() const
{
	return ToQU(Velocity.Size2D());
}

void UArenaMovementComponent::CalcVelocity(float DeltaTime, float Friction, bool bFluid, float BrakingDeceleration)
{
	if (bFluid || HasAnimRootMotion() || DeltaTime < MIN_TICK_TIME || !(IsMovingOnGround() || IsFalling()))
	{
		Super::CalcVelocity(DeltaTime, Friction, bFluid, BrakingDeceleration);
		return;
	}

	// Acceleration is InputVector * MaxAcceleration; recover direction and analog scale.
	const FVector WishDir = Acceleration.GetSafeNormal2D();
	const float InputScale = FMath::Clamp(Acceleration.Size2D() / FMath::Max(GetMaxAcceleration(), UE_KINDA_SMALL_NUMBER), 0.f, 1.f);
	const float WishSpeed = GetMaxSpeed() * InputScale;

	if (IsMovingOnGround())
	{
		const bool bJumpQueued = CharacterOwner && CharacterOwner->bPressedJump;
		if (bJumpQueued)
		{
			// PM_Jump runs before PM_Friction and leaves the ground, so no friction this frame.
			if (bCapBunnyhopSpeed)
			{
				PreventMegaBunnyJumping();
			}
		}
		else
		{
			ApplyFriction(DeltaTime, Friction);
		}
		Accelerate(WishDir, WishSpeed, GroundAccelerate, DeltaTime);
	}
	else
	{
		// The server's value (replicated through the game state) wins, so every
		// machine predicts with the same sv_airaccelerate.
		float Accel = AirAccelerate;
		if (const AArenaGameState* GS = GetWorld() ? GetWorld()->GetGameState<AArenaGameState>() : nullptr)
		{
			Accel = GS->AirAccelerate;
		}
		AirAccelerateQuake(WishDir, WishSpeed, Accel, DeltaTime);
	}
}

void UArenaMovementComponent::ApplyFriction(float DeltaTime, float Friction)
{
	const float Speed = Velocity.Size2D();
	if (Speed < 0.1f)
	{
		Velocity.X = Velocity.Y = 0.f;
		return;
	}

	const float Control = FMath::Max(Speed, StopSpeed);
	const float Drop = Control * Friction * DeltaTime;
	const float NewSpeed = FMath::Max(Speed - Drop, 0.f);
	Velocity *= NewSpeed / Speed;
}

void UArenaMovementComponent::Accelerate(const FVector& WishDir, float WishSpeed, float Accel, float DeltaTime)
{
	const float CurrentSpeed = FVector::DotProduct(Velocity, WishDir);
	const float AddSpeed = WishSpeed - CurrentSpeed;
	if (AddSpeed <= 0.f)
	{
		return;
	}
	const float AccelSpeed = FMath::Min(Accel * DeltaTime * WishSpeed, AddSpeed);
	Velocity += AccelSpeed * WishDir;
}

void UArenaMovementComponent::AirAccelerateQuake(const FVector& WishDir, float WishSpeed, float Accel, float DeltaTime)
{
	// Only the projection onto WishDir is capped (at 30 u/s); the gain uses the full
	// wish speed. Strafing perpendicular to velocity therefore always adds speed.
	const float CappedWishSpeed = FMath::Min(WishSpeed, AirSpeedCap);
	const float CurrentSpeed = FVector::DotProduct(Velocity, WishDir);
	const float AddSpeed = CappedWishSpeed - CurrentSpeed;
	if (AddSpeed <= 0.f)
	{
		return;
	}
	const float AccelSpeed = FMath::Min(Accel * WishSpeed * DeltaTime, AddSpeed);
	Velocity += AccelSpeed * WishDir;
}

void UArenaMovementComponent::PreventMegaBunnyJumping()
{
	const float MaxScaledSpeed = BunnyhopCapFactor * MaxWalkSpeed;
	const float Speed = Velocity.Size();
	if (MaxScaledSpeed > 0.f && Speed > MaxScaledSpeed)
	{
		Velocity *= (MaxScaledSpeed / Speed) * 0.65f;
	}
}

FVector UArenaMovementComponent::GetFallingLateralAcceleration(float DeltaTime)
{
	// Skip Unreal's AirControl scaling; AirAccelerateQuake handles air movement.
	return FVector(Acceleration.X, Acceleration.Y, 0.f);
}

FVector UArenaMovementComponent::NewFallVelocity(const FVector& InitialVelocity, const FVector& Gravity, float DeltaTime) const
{
	// The default clamps the whole vector to TerminalVelocity, which would cap bhop speed.
	// Only limit vertical speed.
	FVector Result = InitialVelocity;
	if (DeltaTime > 0.f)
	{
		Result += Gravity * DeltaTime;
		const float Terminal = FMath::Abs(GetPhysicsVolume()->TerminalVelocity);
		Result.Z = FMath::Clamp(Result.Z, -Terminal, Terminal);
	}
	return Result;
}

bool UArenaMovementComponent::CanAttemptJump() const
{
	// Allow crouch-jumping (the default refuses to jump while crouch is held).
	return IsJumpAllowed() && (IsMovingOnGround() || IsFalling());
}
