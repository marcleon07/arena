#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimInstanceProxy.h"
#include "ArenaAnimInstance.generated.h"

class UAnimSequence;

/** What the pose needs, gathered on the game thread and handed to the worker thread each frame. */
struct FArenaAnimState
{
	// Clip times (seconds) and blend weights.
	float IdleTime = 0.f;
	float WalkTime = 0.f;
	float RunTime = 0.f;
	float JumpTime = 0.f;
	float FallTime = 0.f;
	float LandTime = 0.f;
	float MoveAlpha = 0.f; // idle -> walk/run
	float RunAlpha = 0.f;  // walk -> run
	float LandAlpha = 0.f; // ground -> landing
	float FallAlpha = 0.f; // jump -> fall loop
	float AirAlpha = 0.f;  // ground -> air

	// Procedural layers.
	float CrouchAlpha = 0.f;
	/** Degrees the legs turn toward the movement direction (the upper body keeps aiming). */
	float LegYaw = 0.f;
	/** Degrees, positive up. */
	float AimPitch = 0.f;

	/** Gun in component space, origin at the right hand's grip. */
	FTransform Gun = FTransform::Identity;
	/** Where the left hand holds the gun, in component space. */
	FVector LeftGrip = FVector::ZeroVector;
	bool bHoldingGun = false;
	bool bLeftGrip = false;
};

/**
 * Worker-thread side of UArenaAnimInstance: blends the mannequin's clips, then turns
 * the legs toward the movement direction, bends the spine to aim, squats for crouching
 * and puts both hands on the gun with two-bone IK.
 */
struct FArenaAnimProxy : public FAnimInstanceProxy
{
	FArenaAnimProxy() = default;
	explicit FArenaAnimProxy(UAnimInstance* Instance) : FAnimInstanceProxy(Instance) {}

	virtual void PreUpdate(UAnimInstance* InAnimInstance, float DeltaSeconds) override;
	virtual bool Evaluate(FPoseContext& Output) override;

private:
	FArenaAnimState State;
	TArray<const UAnimSequence*, TInlineAllocator<6>> Clips;
};

/**
 * Native (Blueprint-free) animation for AArenaCharacter on the UE5 mannequin: idle, walk,
 * run, jump, fall and land clips, distance-matched to the movement speed, plus the
 * procedural layers in FArenaAnimProxy.
 */
UCLASS(Transient, NotBlueprintable)
class ARENA_API UArenaAnimInstance : public UAnimInstance
{
	GENERATED_BODY()

public:
	enum EClip : int32
	{
		Idle,
		Walk,
		Run,
		Jump,
		Fall,
		Land,
		NumClips
	};

protected:
	virtual void NativeInitializeAnimation() override;
	virtual void NativeUpdateAnimation(float DeltaSeconds) override;
	virtual FAnimInstanceProxy* CreateAnimInstanceProxy() override;

private:
	friend struct FArenaAnimProxy;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UAnimSequence>> Clips;

	FArenaAnimState State;
	float Phase = 0.f;
	float AirTime = 0.f;
	float LandWeight = 0.f;
	float LastVelocityZ = 0.f;
	bool bWasOnGround = true;
	bool bJumpStart = false;
	bool bBackward = false;
};
