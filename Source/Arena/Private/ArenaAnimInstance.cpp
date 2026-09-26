#include "ArenaAnimInstance.h"
#include "ArenaCharacter.h"
#include "Animation/AnimNodeBase.h"
#include "Animation/AnimSequence.h"
#include "AnimationRuntime.h"
#include "TwoBoneIK.h"

namespace
{
	// Mannequin clip speeds, measured from the foot motion: one walk cycle covers
	// 2.73 s at 230 cm/s, one run cycle 1.9 s at 527 cm/s.
	constexpr float WalkSpeed = 230.f;
	constexpr float RunSpeed = 527.f;

	const TCHAR* const ClipPaths[] =
	{
		TEXT("/Game/Characters/Mannequins/Animations/Manny/MM_Idle.MM_Idle"),
		TEXT("/Game/Characters/Mannequins/Animations/Manny/MM_Walk_Fwd.MM_Walk_Fwd"),
		TEXT("/Game/Characters/Mannequins/Animations/Manny/MM_Run_Fwd.MM_Run_Fwd"),
		TEXT("/Game/Characters/Mannequins/Animations/Manny/MM_Jump.MM_Jump"),
		TEXT("/Game/Characters/Mannequins/Animations/Manny/MM_Fall_Loop.MM_Fall_Loop"),
		TEXT("/Game/Characters/Mannequins/Animations/Manny/MM_Land.MM_Land"),
	};

	float ClipLength(const UAnimSequence* Clip)
	{
		return Clip ? FMath::Max(Clip->GetPlayLength(), 0.01f) : 1.f;
	}

	struct FBones
	{
		FCompactPoseBoneIndex Pelvis, Head, Neck;
		FCompactPoseBoneIndex Spine[5];
		FCompactPoseBoneIndex Thigh[2], Calf[2], Foot[2];
		FCompactPoseBoneIndex UpperArm[2], LowerArm[2], Hand[2];
		FCompactPoseBoneIndex Metacarpal[2], Middle[2], IndexMeta[2], Index[2], PinkyMeta[2], Pinky[2];

		explicit FBones(const FBoneContainer& Container)
			: Pelvis(Find(Container, TEXT("pelvis"))), Head(Find(Container, TEXT("head"))), Neck(Find(Container, TEXT("neck_01")))
			, Spine{ Find(Container, TEXT("spine_01")), Find(Container, TEXT("spine_02")), Find(Container, TEXT("spine_03")), Find(Container, TEXT("spine_04")), Find(Container, TEXT("spine_05")) }
			, Thigh{ Find(Container, TEXT("thigh_l")), Find(Container, TEXT("thigh_r")) }
			, Calf{ Find(Container, TEXT("calf_l")), Find(Container, TEXT("calf_r")) }
			, Foot{ Find(Container, TEXT("foot_l")), Find(Container, TEXT("foot_r")) }
			, UpperArm{ Find(Container, TEXT("upperarm_l")), Find(Container, TEXT("upperarm_r")) }
			, LowerArm{ Find(Container, TEXT("lowerarm_l")), Find(Container, TEXT("lowerarm_r")) }
			, Hand{ Find(Container, TEXT("hand_l")), Find(Container, TEXT("hand_r")) }
			, Metacarpal{ Find(Container, TEXT("middle_metacarpal_l")), Find(Container, TEXT("middle_metacarpal_r")) }
			, Middle{ Find(Container, TEXT("middle_01_l")), Find(Container, TEXT("middle_01_r")) }
			, IndexMeta{ Find(Container, TEXT("index_metacarpal_l")), Find(Container, TEXT("index_metacarpal_r")) }
			, Index{ Find(Container, TEXT("index_01_l")), Find(Container, TEXT("index_01_r")) }
			, PinkyMeta{ Find(Container, TEXT("pinky_metacarpal_l")), Find(Container, TEXT("pinky_metacarpal_r")) }
			, Pinky{ Find(Container, TEXT("pinky_01_l")), Find(Container, TEXT("pinky_01_r")) }
		{
		}

		static FCompactPoseBoneIndex Find(const FBoneContainer& Container, const TCHAR* Name)
		{
			const int32 MeshIndex = Container.GetPoseBoneIndexForBoneName(FName(Name));
			return MeshIndex == INDEX_NONE ? FCompactPoseBoneIndex(INDEX_NONE) : Container.MakeCompactPoseIndex(FMeshPoseBoneIndex(MeshIndex));
		}
	};

	void Mix(FPoseContext& Out, FPoseContext& A, FPoseContext& B, float WeightOfB)
	{
		FAnimationPoseData DataA(A);
		FAnimationPoseData DataB(B);
		FAnimationPoseData DataOut(Out);
		FAnimationRuntime::BlendTwoPosesTogether(DataA, DataB, 1.f - FMath::Clamp(WeightOfB, 0.f, 1.f), DataOut);
	}

	void SetCS(FCSPose<FCompactPose>& Pose, FCompactPoseBoneIndex Bone, const FTransform& Transform)
	{
		const FBoneTransform One(Bone, Transform);
		Pose.SafeSetCSBoneTransforms(MakeArrayView(&One, 1));
	}

	/** Rotates a bone about its own position by Delta (component space); children follow. */
	void RotateCS(FCSPose<FCompactPose>& Pose, FCompactPoseBoneIndex Bone, const FQuat& Delta)
	{
		if (Bone.IsValid())
		{
			FTransform T = Pose.GetComponentSpaceTransform(Bone);
			T.SetRotation((Delta * T.GetRotation()).GetNormalized());
			SetCS(Pose, Bone, T);
		}
	}

	/** A bone's reference-pose location relative to Ancestor, following Chain (child first). */
	FVector RefOffset(const FBoneContainer& Container, std::initializer_list<FCompactPoseBoneIndex> Chain)
	{
		FTransform T = FTransform::Identity;
		for (const FCompactPoseBoneIndex Bone : Chain)
		{
			T = T * Container.GetRefPoseTransform(Bone);
		}
		return T.GetLocation();
	}
}

// ---------------------------------------------------------------------------
// Game thread
// ---------------------------------------------------------------------------

void UArenaAnimInstance::NativeInitializeAnimation()
{
	Super::NativeInitializeAnimation();
	Clips.Reset();
	for (const TCHAR* Path : ClipPaths)
	{
		Clips.Add(LoadObject<UAnimSequence>(nullptr, Path));
	}
}

FAnimInstanceProxy* UArenaAnimInstance::CreateAnimInstanceProxy()
{
	return new FArenaAnimProxy(this);
}

void UArenaAnimInstance::NativeUpdateAnimation(float DeltaSeconds)
{
	Super::NativeUpdateAnimation(DeltaSeconds);

	const AArenaCharacter* Character = Cast<AArenaCharacter>(TryGetPawnOwner());
	if (!Character || Clips.Num() != NumClips)
	{
		return;
	}
	const float Dt = DeltaSeconds;
	const FArenaPoseInput In = Character->GetPoseInput();
	const FVector LocalVelocity = Character->GetActorRotation().UnrotateVector(In.Velocity);
	const float Speed = LocalVelocity.Size2D();

	// Legs turn toward where you're going; past ~100 degrees you backpedal instead.
	float TargetLegYaw = 0.f;
	if (Speed > 40.f)
	{
		const float Angle = FMath::RadiansToDegrees(FMath::Atan2(LocalVelocity.Y, LocalVelocity.X));
		if (bBackward ? FMath::Abs(Angle) < 80.f : FMath::Abs(Angle) > 100.f)
		{
			bBackward = !bBackward;
		}
		TargetLegYaw = FMath::Clamp(bBackward ? FMath::UnwindDegrees(Angle + 180.f) : Angle, -75.f, 75.f);
	}
	if (!In.bOnGround)
	{
		TargetLegYaw *= 0.5f;
	}
	State.LegYaw = FMath::FInterpTo(State.LegYaw, TargetLegYaw, Dt, 10.f);

	// Walk and run share a phase that advances with distance travelled, so feet
	// don't slide at any speed (bhop speeds just play faster).
	const float WalkLength = ClipLength(Clips[Walk]);
	const float RunLength = ClipLength(Clips[Run]);
	State.RunAlpha = FMath::Clamp((Speed - WalkSpeed) / (RunSpeed - WalkSpeed), 0.f, 1.f);
	State.MoveAlpha = FMath::FInterpTo(State.MoveAlpha, FMath::Clamp(Speed / 120.f, 0.f, 1.f), Dt, 12.f);
	const float CycleDistance = FMath::Lerp(WalkSpeed * WalkLength, RunSpeed * RunLength, State.RunAlpha);
	Phase = FMath::Frac(Phase + (bBackward ? -1.f : 1.f) * Speed * Dt / CycleDistance);
	State.WalkTime = Phase * WalkLength;
	State.RunTime = Phase * RunLength;
	State.IdleTime = FMath::Fmod(State.IdleTime + Dt, ClipLength(Clips[Idle]));

	// Air: a jump clip when leaving the ground upward, then the fall loop.
	if (!In.bOnGround)
	{
		if (bWasOnGround)
		{
			AirTime = 0.f;
			bJumpStart = In.Velocity.Z > 100.f;
		}
		AirTime += Dt;
	}
	else if (!bWasOnGround && -LastVelocityZ > 450.f)
	{
		State.LandTime = 0.f;
		LandWeight = FMath::GetMappedRangeValueClamped(FVector2D(450.f, 1200.f), FVector2D(0.35f, 1.f), -LastVelocityZ);
	}
	State.AirAlpha = FMath::FInterpTo(State.AirAlpha, In.bOnGround ? 0.f : 1.f, Dt, In.bOnGround ? 14.f : 10.f);
	State.JumpTime = FMath::Min(AirTime, ClipLength(Clips[Jump]) - 0.01f);
	State.FallAlpha = bJumpStart ? FMath::Clamp((AirTime - 0.3f) / 0.25f, 0.f, 1.f) : 1.f;
	State.FallTime = FMath::Fmod(AirTime, ClipLength(Clips[Fall]));
	State.LandTime = FMath::Min(State.LandTime + Dt, ClipLength(Clips[Land]) - 0.01f);
	State.LandAlpha = LandWeight * (1.f - FMath::Clamp(State.LandTime / 0.4f, 0.f, 1.f)) * (1.f - FMath::Clamp(Speed / 300.f, 0.f, 1.f));

	State.CrouchAlpha = FMath::FInterpTo(State.CrouchAlpha, In.bCrouched ? 1.f : 0.f, Dt, 12.f);
	State.AimPitch = In.AimPitch;
	State.Gun = In.Gun;
	State.LeftGrip = In.LeftGrip;
	State.bHoldingGun = In.bHoldingGun;
	State.bLeftGrip = In.bLeftGrip;

	bWasOnGround = In.bOnGround;
	LastVelocityZ = In.Velocity.Z;
}

// ---------------------------------------------------------------------------
// Worker thread
// ---------------------------------------------------------------------------

void FArenaAnimProxy::PreUpdate(UAnimInstance* InAnimInstance, float DeltaSeconds)
{
	FAnimInstanceProxy::PreUpdate(InAnimInstance, DeltaSeconds);
	const UArenaAnimInstance* Instance = CastChecked<UArenaAnimInstance>(InAnimInstance);
	State = Instance->State;
	Clips.Reset();
	for (const TObjectPtr<UAnimSequence>& Clip : Instance->Clips)
	{
		Clips.Add(Clip.Get());
	}
}

bool FArenaAnimProxy::Evaluate(FPoseContext& Output)
{
	if (Clips.Num() != UArenaAnimInstance::NumClips || Clips.Contains(nullptr))
	{
		Output.ResetToRefPose();
		return true;
	}
	const FArenaAnimState& S = State;

	// --- Clip blend --------------------------------------------------------

	auto Sample = [this, &Output](int32 Clip, float Time, FPoseContext& Out)
	{
		FAnimationPoseData Data(Out);
		Clips[Clip]->GetAnimationPose(Data, FAnimExtractContext(static_cast<double>(Time), false, {}, true));
	};

	FPoseContext Idle(Output), Walk(Output), Run(Output), Jump(Output), Fall(Output), Land(Output);
	Sample(UArenaAnimInstance::Idle, S.IdleTime, Idle);
	Sample(UArenaAnimInstance::Walk, S.WalkTime, Walk);
	Sample(UArenaAnimInstance::Run, S.RunTime, Run);
	Sample(UArenaAnimInstance::Jump, S.JumpTime, Jump);
	Sample(UArenaAnimInstance::Fall, S.FallTime, Fall);
	Sample(UArenaAnimInstance::Land, S.LandTime, Land);

	FPoseContext Loco(Output), Ground(Output), Landed(Output), Air(Output);
	Mix(Loco, Walk, Run, S.RunAlpha);
	Mix(Ground, Idle, Loco, S.MoveAlpha);
	Mix(Landed, Ground, Land, S.LandAlpha);
	Mix(Air, Jump, Fall, S.FallAlpha);
	Mix(Output, Landed, Air, S.AirAlpha);

	// --- Procedural layers (component space) -----------------------------------

	const FBoneContainer& Container = Output.Pose.GetBoneContainer();
	const FBones B(Container);
	if (!B.Pelvis.IsValid() || !B.Spine[4].IsValid())
	{
		return true;
	}

	FComponentSpacePoseContext CSContext(this);
	CSContext.Pose.InitPose(Output.Pose);
	FCSPose<FCompactPose>& CS = CSContext.Pose;

	// The mannequin faces +Y in component space, with +X on its left. Rotating
	// about +X by a positive angle tips +Y (forward) toward +Z (up).
	const FVector Up(0.f, 0.f, 1.f);
	const FVector Lateral(1.f, 0.f, 0.f);
	const FQuat LegRotation(Up, FMath::DegreesToRadians(S.LegYaw));

	// Legs face the movement direction.
	{
		FTransform Pelvis = CS.GetComponentSpaceTransform(B.Pelvis);
		Pelvis.SetRotation(LegRotation * Pelvis.GetRotation());
		Pelvis.SetLocation(LegRotation.RotateVector(Pelvis.GetLocation()));
		SetCS(CS, B.Pelvis, Pelvis);
	}

	// Crouch: drop the hips and keep the feet planted with leg IK.
	const float Crouch = S.CrouchAlpha;
	if (Crouch > 0.01f && B.Thigh[0].IsValid() && B.Foot[1].IsValid())
	{
		FVector FootTarget[2];
		FQuat FootRotation[2];
		for (int32 Side = 0; Side < 2; ++Side)
		{
			const FTransform Foot = CS.GetComponentSpaceTransform(B.Foot[Side]);
			FootTarget[Side] = Foot.GetLocation();
			FootRotation[Side] = Foot.GetRotation();
		}
		FTransform Pelvis = CS.GetComponentSpaceTransform(B.Pelvis);
		Pelvis.AddToTranslation(LegRotation.RotateVector(FVector(0.f, -12.f, -44.f)) * Crouch);
		SetCS(CS, B.Pelvis, Pelvis);

		const FVector LegForward = LegRotation.RotateVector(FVector(0.f, 1.f, 0.f));
		for (int32 Side = 0; Side < 2; ++Side)
		{
			FTransform Thigh = CS.GetComponentSpaceTransform(B.Thigh[Side]);
			FTransform Calf = CS.GetComponentSpaceTransform(B.Calf[Side]);
			FTransform Foot = CS.GetComponentSpaceTransform(B.Foot[Side]);
			const FVector Knee = Calf.GetLocation() + LegForward * 60.f;
			AnimationCore::SolveTwoBoneIK(Thigh, Calf, Foot, Knee, FootTarget[Side], false, 1.0, 1.0);
			Foot.SetRotation(FootRotation[Side]);
			const FBoneTransform Leg[] = { FBoneTransform(B.Thigh[Side], Thigh), FBoneTransform(B.Calf[Side], Calf), FBoneTransform(B.Foot[Side], Foot) };
			CS.SafeSetCSBoneTransforms(Leg);
		}
	}

	// Upper body: undo the leg turn over the lower spine, bend to aim, lean when crouched.
	const float Pitch = FMath::Clamp(S.AimPitch, -80.f, 80.f);
	for (int32 i = 0; i < 5; ++i)
	{
		const float Yaw = i < 3 ? -S.LegYaw / 3.f : 0.f;
		const float Bend = Pitch * 0.12f - 5.f * Crouch;
		RotateCS(CS, B.Spine[i], FQuat(Lateral, FMath::DegreesToRadians(Bend)) * FQuat(Up, FMath::DegreesToRadians(Yaw)));
	}
	RotateCS(CS, B.Neck, FQuat(Lateral, FMath::DegreesToRadians(Pitch * 0.15f + 10.f * Crouch)));
	RotateCS(CS, B.Head, FQuat(Lateral, FMath::DegreesToRadians(Pitch * 0.25f + 15.f * Crouch)));

	// Hands on the gun.
	if (S.bHoldingGun && B.Hand[1].IsValid() && B.Middle[1].IsValid() && B.Index[1].IsValid() && B.Pinky[1].IsValid())
	{
		const FVector GunForward = S.Gun.GetUnitAxis(EAxis::X);
		const FVector GunRight = S.Gun.GetUnitAxis(EAxis::Y);
		const FVector GunUp = S.Gun.GetUnitAxis(EAxis::Z);

		for (int32 Side = 1; Side >= 0; --Side)
		{
			const bool bRight = Side == 1;
			if (!bRight && !S.bLeftGrip)
			{
				continue;
			}
			const FVector Grip = bRight ? S.Gun.GetLocation() : S.LeftGrip;
			// Knuckles run forward and down the grip; the palm faces the grip from the outside.
			const FVector Knuckles = (GunForward - GunUp * 0.35f + (bRight ? FVector::ZeroVector : GunRight * 0.1f)).GetSafeNormal();
			const FVector Palm = bRight ? -GunRight : GunRight;

			// The same directions in the hand bone's own frame, from the reference pose.
			const FVector KnucklesLocal = RefOffset(Container, { B.Middle[Side], B.Metacarpal[Side] }).GetSafeNormal();
			const FVector Across = RefOffset(Container, { B.Index[Side], B.IndexMeta[Side] }) - RefOffset(Container, { B.Pinky[Side], B.PinkyMeta[Side] });
			const FVector PalmLocal = FVector::CrossProduct(KnucklesLocal, Across).GetSafeNormal() * (bRight ? 1.f : -1.f);
			const FQuat HandRotation = FRotationMatrix::MakeFromXY(Knuckles, Palm).ToQuat() * FRotationMatrix::MakeFromXY(KnucklesLocal, PalmLocal).ToQuat().Inverse();
			const FVector Wrist = Grip - Knuckles * 6.f - Palm * 4.f;

			FTransform Upper = CS.GetComponentSpaceTransform(B.UpperArm[Side]);
			FTransform Lower = CS.GetComponentSpaceTransform(B.LowerArm[Side]);
			FTransform Hand = CS.GetComponentSpaceTransform(B.Hand[Side]);
			const FVector Elbow = Upper.GetLocation() + (bRight ? GunRight : -GunRight) * 25.f - Up * 35.f - GunForward * 5.f;
			AnimationCore::SolveTwoBoneIK(Upper, Lower, Hand, Elbow, Wrist, true, 1.0, 1.15);
			Hand.SetRotation(HandRotation);
			const FBoneTransform Arm[] = { FBoneTransform(B.UpperArm[Side], Upper), FBoneTransform(B.LowerArm[Side], Lower), FBoneTransform(B.Hand[Side], Hand) };
			CS.SafeSetCSBoneTransforms(Arm);
		}
	}

	FCSPose<FCompactPose>::ConvertComponentPosesToLocalPoses(MoveTemp(CS), Output.Pose);
	return true;
}
