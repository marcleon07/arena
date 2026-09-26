#include "ArenaCharacter.h"
#include "Arena.h"
#include "ArenaAnimInstance.h"
#include "ArenaAudio.h"
#include "ArenaSettings.h"
#include "ArenaMovementComponent.h"
#include "ArenaPlayerController.h"
#include "ArenaPlayerState.h"
#include "ArenaGameMode.h"
#include "ArenaGameState.h"
#include "ArenaMap.h"
#include "ArenaProjectile.h"
#include "ArenaVisuals.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "EnhancedInputComponent.h"
#include "InputAction.h"
#include "InputActionValue.h"
#include "Engine/CollisionProfile.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshSocket.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Net/UnrealNetwork.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	constexpr float StandingEyeHeight = QU(28.f); // VEC_VIEW
	constexpr float CrouchedEyeHeight = QU(12.f); // VEC_DUCK_VIEW
	constexpr float MouseYawPerCount = 0.022f;    // m_yaw / m_pitch

	const FName MuzzleSocket(TEXT("Muzzle"));
	const FName SpinSocket(TEXT("Spin"));
	const FName LeftHandSocket(TEXT("LeftHand"));

	/** The first-person gun is the third-person model shrunk and held close, inside the capsule, so it never pokes into walls. */
	constexpr float ViewModelScale = 0.36f;

	struct FViewModelInfo
	{
		/** Grip position relative to the camera (X forward, Y right, Z up). */
		FVector Offset;
		/** Recoil: centimetres back and degrees up per shot. */
		float Kick;
		float KickPitch;
		/** Idle and firing spin of the "Spin" part, degrees per second. */
		float IdleSpin;
		float FireSpin;
	};

	const FViewModelInfo& GetViewModel(EArenaWeapon Weapon)
	{
		static const FViewModelInfo Infos[] =
		{
			{ FVector(17.f, 10.f, -10.5f), 1.5f, 3.f,  200.f, 1800.f }, // Gauntlet
			{ FVector(16.f, 11.f, -12.f),  0.5f, 1.2f, 0.f,   1400.f }, // Machinegun
			{ FVector(16.f, 11.f, -12.f),  2.5f, 8.f,  0.f,   0.f },    // Shotgun
			{ FVector(16.f, 11.f, -12.f),  2.f,  6.f,  0.f,   0.f },    // Grenade launcher
			{ FVector(15.f, 11.5f, -13.5f), 2.5f, 6.f, 0.f,   0.f },    // Rocket launcher
			{ FVector(16.f, 11.f, -12.f),  0.2f, 0.6f, 60.f,  500.f },  // Lightning gun
			{ FVector(15.f, 11.f, -12.f),  3.f,  7.f,  0.f,   0.f },    // Railgun
			{ FVector(16.f, 11.f, -12.f),  0.6f, 1.5f, 0.f,   0.f },    // Plasma gun
		};
		static_assert(UE_ARRAY_COUNT(Infos) == ArenaWeaponCount, "One entry per EArenaWeapon");
		return Infos[FMath::Clamp(static_cast<int32>(Weapon), 0, ArenaWeaponCount - 1)];
	}

	/**
	 * Pellet / spread directions from a seed the client sends with the shot,
	 * so the shooter's predicted tracers match what the server traces.
	 */
	void GetShotDirections(const FArenaWeaponInfo& Info, const FVector& Dir, int32 Seed, TArray<FVector>& Out)
	{
		FRandomStream Stream(Seed);
		const float HalfAngle = FMath::DegreesToRadians(Info.SpreadDegrees);
		for (int32 i = 0; i < FMath::Max(1, Info.Pellets); ++i)
		{
			Out.Add(HalfAngle > 0.f ? Stream.VRandCone(Dir, HalfAngle) : Dir);
		}
	}
}

AArenaCharacter::AArenaCharacter(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer.SetDefaultSubobjectClass<UArenaMovementComponent>(ACharacter::CharacterMovementComponentName))
{
	PrimaryActorTick.bCanEverTick = true;

	static ConstructorHelpers::FObjectFinder<USkeletalMesh> BodyAsset(TEXT("/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple.SKM_Manny_Simple"));

	// GoldSrc hull: 32x32x72 standing, 32x32x36 ducked.
	GetCapsuleComponent()->InitCapsuleSize(QU(16.f), QU(36.f));
	GetCharacterMovement()->SetCrouchedHalfHeight(QU(18.f));

	bUseControllerRotationYaw = true;
	bUseControllerRotationPitch = false;
	bUseControllerRotationRoll = false;
	JumpMaxHoldTime = 0.f;
	JumpMaxCount = 1;

	// UE5 mannequin, animated natively by UArenaAnimInstance. Hidden from its owner
	// (who sees the first-person gun instead) but still casting a shadow.
	USkeletalMeshComponent* Body = GetMesh();
	Body->SetSkeletalMeshAsset(BodyAsset.Object);
	Body->SetAnimationMode(EAnimationMode::AnimationBlueprint);
	Body->SetAnimInstanceClass(UArenaAnimInstance::StaticClass());
	Body->SetRelativeLocationAndRotation(FVector(0.f, 0.f, -QU(36.f)), FRotator(0.f, -90.f, 0.f));
	Body->SetOwnerNoSee(true);
	Body->bCastHiddenShadow = true;
	Body->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::OnlyTickPoseWhenRendered;
	// Hits are decided by the capsule alone; the body only collides as a ragdoll.
	Body->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);

	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	Camera->SetupAttachment(GetCapsuleComponent());
	Camera->SetRelativeLocation(FVector(0.f, 0.f, StandingEyeHeight));
	Camera->bUsePawnControlRotation = true;
	Camera->SetFieldOfView(100.f);

	auto MakeGunPart = [this](const TCHAR* Name, USceneComponent* Parent, bool bFirstPerson)
	{
		UStaticMeshComponent* Part = CreateDefaultSubobject<UStaticMeshComponent>(Name);
		Part->SetupAttachment(Parent);
		ArenaVisuals::SetupCosmeticMesh(Part, nullptr);
		if (bFirstPerson)
		{
			Part->SetOnlyOwnerSee(true);
			Part->SetCastShadow(false);
		}
		else
		{
			Part->SetOwnerNoSee(true);
		}
		return Part;
	};

	WorldGunMesh = MakeGunPart(TEXT("WorldGun"), Body, false);
	WorldGunSpin = MakeGunPart(TEXT("WorldGunSpin"), WorldGunMesh, false);
	WorldFlash = MakeGunPart(TEXT("WorldFlash"), WorldGunMesh, false);
	WorldFlash->SetCastShadow(false);
	WorldFlash->SetVisibility(false);

	ViewGunMesh = MakeGunPart(TEXT("ViewGun"), Camera, true);
	ViewGunMesh->SetRelativeScale3D(FVector(ViewModelScale));
	ViewGunSpin = MakeGunPart(TEXT("ViewGunSpin"), ViewGunMesh, true);
	ViewFlash = MakeGunPart(TEXT("ViewFlash"), ViewGunMesh, true);
	ViewFlash->SetVisibility(false);

	FlashLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("FlashLight"));
	FlashLight->SetupAttachment(GetCapsuleComponent());
	FlashLight->SetCastShadows(false);
	FlashLight->SetIntensityUnits(ELightUnits::Candelas);
	FlashLight->SetIntensity(15.f);
	FlashLight->SetAttenuationRadius(500.f);
	FlashLight->SetVisibility(false);
}

UArenaMovementComponent* AArenaCharacter::GetArenaMovement() const
{
	return Cast<UArenaMovementComponent>(GetCharacterMovement());
}

void AArenaCharacter::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AArenaCharacter, Health);
	DOREPLIFETIME(AArenaCharacter, Armor);
	DOREPLIFETIME(AArenaCharacter, bDead);
	DOREPLIFETIME_CONDITION(AArenaCharacter, CurrentWeapon, COND_SkipOwner);
	DOREPLIFETIME_CONDITION(AArenaCharacter, Ammo, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(AArenaCharacter, OwnedWeapons, COND_OwnerOnly);
}

void AArenaCharacter::BeginPlay()
{
	Super::BeginPlay();

	if (HasAuthority())
	{
		Ammo.Init(0, ArenaWeaponCount);
		GiveWeapon(EArenaWeapon::Gauntlet, 0);
		GiveWeapon(EArenaWeapon::MachineGun, GetWeaponInfo(EArenaWeapon::MachineGun).StartAmmo);
	}
	UpdateColors();
	UpdateWeaponVisuals();
	UArenaAudio::PlayAt(this, EArenaSound::Spawn, GetActorLocation(), 0.7f);
}

void AArenaCharacter::PossessedBy(AController* NewController)
{
	Super::PossessedBy(NewController);
	UpdateColors();
	UE_LOG(LogArena, Log, TEXT("%s spawned at %s"), GetPlayerState() ? *GetPlayerState()->GetPlayerName() : TEXT("?"), *GetActorLocation().ToCompactString());
}

void AArenaCharacter::OnRep_PlayerState()
{
	Super::OnRep_PlayerState();
	UpdateColors();
}

void AArenaCharacter::UpdateColors()
{
	const AArenaPlayerState* PS = GetPlayerState<AArenaPlayerState>();
	SetBodyColor(PS ? PS->GetPlayerColor() : FLinearColor::Gray);
}

void AArenaCharacter::SetBodyColor(const FLinearColor& Color)
{
	// Armour plates in the player colour; the dark suit gets a rim of it.
	USkeletalMeshComponent* Body = GetMesh();
	for (const FName Slot : { FName(TEXT("M_Torso")), FName(TEXT("M_HeadLegs")) })
	{
		const int32 Index = Body->GetMaterialIndex(Slot);
		UMaterialInstanceDynamic* MID = Index == INDEX_NONE ? nullptr : Body->CreateAndSetMaterialInstanceDynamic(Index);
		if (!MID)
		{
			continue;
		}
		if (Slot == TEXT("M_Torso"))
		{
			MID->SetVectorParameterValue(TEXT("Color"), Color);
		}
		MID->SetVectorParameterValue(TEXT("RimColor"), Color);
	}
}

FVector AArenaCharacter::GetMuzzleLocation() const
{
	const UStaticMeshComponent* Gun = IsLocalPlayerView() ? ViewGunMesh.Get() : WorldGunMesh.Get();
	return Gun->DoesSocketExist(MuzzleSocket) ? Gun->GetSocketLocation(MuzzleSocket) : Gun->GetComponentLocation();
}

void AArenaCharacter::AttachGunParts(UStaticMeshComponent* Gun, UStaticMeshComponent* Spin, UStaticMeshComponent* Flash)
{
	const FArenaWeaponInfo& Info = GetWeaponInfo(CurrentWeapon);
	Gun->EmptyOverrideMaterials();
	Gun->SetStaticMesh(ArenaVisuals::WeaponMesh(CurrentWeapon));
	ArenaVisuals::SetPropColors(Gun, Info.Color, Info.Color);

	Spin->EmptyOverrideMaterials();
	Spin->SetStaticMesh(ArenaVisuals::WeaponSpinMesh(CurrentWeapon));
	ArenaVisuals::SetPropColors(Spin, Info.Color, Info.Color);
	Spin->AttachToComponent(Gun, FAttachmentTransformRules::SnapToTargetNotIncludingScale, SpinSocket);

	Flash->SetStaticMesh(ArenaVisuals::ArtMesh(TEXT("SM_MuzzleFlash")));
	ArenaVisuals::SetFX(Flash, Info.Color * 0.6f + FLinearColor(1.f, 0.9f, 0.7f) * 0.4f, 12.f);
	Flash->AttachToComponent(Gun, FAttachmentTransformRules::SnapToTargetNotIncludingScale, MuzzleSocket);
}

void AArenaCharacter::UpdateWeaponVisuals()
{
	AttachGunParts(WorldGunMesh, WorldGunSpin, WorldFlash);
	AttachGunParts(ViewGunMesh, ViewGunSpin, ViewFlash);
	WorldFlash->SetRelativeScale3D(FVector(2.f));
	ViewFlash->SetRelativeScale3D(FVector(2.5f));

	const UStaticMesh* GunModel = WorldGunMesh->GetStaticMesh();
	const UStaticMeshSocket* Grip = GunModel ? GunModel->FindSocket(LeftHandSocket) : nullptr;
	bHasLeftGrip = Grip != nullptr;
	LeftGrip = Grip ? Grip->RelativeLocation : FVector::ZeroVector;

	// Raise the new gun into view, Quake style.
	RaiseAlpha = 0.f;
	SpinSpeed = 0.f;
}

FArenaPoseInput AArenaCharacter::GetPoseInput() const
{
	FArenaPoseInput In;
	if (PoseOverride.bEnabled)
	{
		In.Velocity = GetActorRotation().RotateVector(PoseOverride.LocalVelocity);
		In.bOnGround = !PoseOverride.bInAir;
		In.bCrouched = PoseOverride.bCrouched;
		In.AimPitch = PoseOverride.AimPitch;
	}
	else
	{
		In.Velocity = GetVelocity();
		In.bOnGround = GetCharacterMovement()->IsMovingOnGround();
		In.bCrouched = bIsCrouched;
		In.AimPitch = FRotator::NormalizeAxis(GetBaseAimRotation().Pitch);
	}
	In.Gun = GunFrame;
	In.bHoldingGun = !bDead;
	In.bLeftGrip = bHasLeftGrip;
	In.LeftGrip = GunFrame.TransformPosition(LeftGrip);
	return In;
}

void AArenaCharacter::PlayFireEffects()
{
	if (GetNetMode() == NM_DedicatedServer || bDead)
	{
		return;
	}
	LastShotTime = GetWorld()->GetTimeSeconds();
	BodyKick = 1.f;
	if (CurrentWeapon == EArenaWeapon::Gauntlet)
	{
		return;
	}
	FlashTime = 0.05f;
	const bool bLocalView = IsLocalPlayerView();
	UStaticMeshComponent* Flash = bLocalView ? ViewFlash.Get() : WorldFlash.Get();
	Flash->SetRelativeRotation(FRotator(0.f, 0.f, FMath::FRandRange(0.f, 360.f)));
	Flash->SetRelativeScale3D(FVector((bLocalView ? 2.5f : 2.f) * FMath::FRandRange(0.8f, 1.2f)));
	FlashLight->SetLightColor(GetWeaponInfo(CurrentWeapon).Color);
	// Ahead of the muzzle, so it lights the surroundings rather than blowing out the gun.
	FlashLight->SetWorldLocation(GetMuzzleLocation() + GetBaseAimRotation().Vector() * 80.f);
}

void AArenaCharacter::UpdateWorldGun(float DeltaSeconds)
{
	// The gun hangs from a pivot in the upper chest and turns with the aim; the
	// body's hand IK then grabs it. Body component space: +Y forward, -X right, +Z up.
	const FArenaPoseInput In = GetPoseInput();
	GunCrouch = FMath::FInterpTo(GunCrouch, In.bCrouched ? 1.f : 0.f, DeltaSeconds, 12.f);
	BodyKick = FMath::FInterpTo(BodyKick, 0.f, DeltaSeconds, 10.f);

	const float Pitch = FMath::DegreesToRadians(FMath::Clamp(In.AimPitch, -80.f, 80.f));
	const FVector Forward(0.f, FMath::Cos(Pitch), FMath::Sin(Pitch));
	const FVector Up(0.f, -FMath::Sin(Pitch), FMath::Cos(Pitch));
	const FVector Right(-1.f, 0.f, 0.f);
	const FVector Pivot(0.f, -4.f * GunCrouch, 142.f - 44.f * GunCrouch);
	const FVector Grip = Pivot + Forward * (22.f - BodyKick * 5.f) + Right * 11.f - Up * 20.f;
	const FQuat Rotation = FRotationMatrix::MakeFromXZ(Forward, Up).ToQuat() * FRotator(BodyKick * 8.f, 0.f, 0.f).Quaternion();
	GunFrame = FTransform(Rotation, Grip);
	WorldGunMesh->SetRelativeTransform(GunFrame);
}

void AArenaCharacter::UpdateViewModel(float DeltaSeconds)
{
	const FViewModelInfo& View = GetViewModel(CurrentWeapon);
	const FVector Velocity = GetVelocity();
	const bool bOnGround = GetCharacterMovement()->IsMovingOnGround();
	const float Now = GetWorld()->GetTimeSeconds();

	// Walk bob: a figure of eight that grows with speed.
	BobAmount = FMath::FInterpTo(BobAmount, bOnGround ? FMath::Clamp(Velocity.Size2D() / QU(320.f), 0.f, 1.f) : 0.f, DeltaSeconds, 6.f);
	BobPhase += DeltaSeconds * FMath::Lerp(4.f, 11.f, BobAmount);
	FVector Offset = View.Offset;
	Offset.Y += FMath::Sin(BobPhase) * 0.7f * BobAmount;
	Offset.Z -= FMath::Abs(FMath::Cos(BobPhase)) * 0.6f * BobAmount;
	Offset.Z += FMath::Sin(Now * 1.6f) * 0.12f; // Breathing.

	// The gun lags behind vertical speed and dips on landing.
	Offset.Z += FMath::Clamp(-Velocity.Z / 1600.f, -1.f, 1.f);
	LandDip = FMath::FInterpTo(LandDip, 0.f, DeltaSeconds, 6.f);
	Offset.Z -= LandDip;

	Offset.X -= ViewKick * View.Kick;

	RaiseAlpha = FMath::Min(1.f, RaiseAlpha + DeltaSeconds / 0.25f);
	const float Lowered = 1.f - FMath::InterpEaseOut(0.f, 1.f, RaiseAlpha, 2.f);
	Offset.Z -= Lowered * 10.f;

	// Sway: the gun trails the view when you turn.
	const FRotator ViewRotation = GetControlRotation();
	const FRotator Turn = (ViewRotation - LastViewRotation).GetNormalized();
	LastViewRotation = ViewRotation;
	Sway = FMath::RInterpTo(Sway, FRotator::ZeroRotator, DeltaSeconds, 9.f);
	Sway.Pitch = FMath::Clamp(Sway.Pitch - Turn.Pitch * 0.25f, -4.f, 4.f);
	Sway.Yaw = FMath::Clamp(Sway.Yaw - Turn.Yaw * 0.25f, -5.f, 5.f);

	// Lean into strafes.
	const float Strafe = FMath::Clamp(GetActorRotation().UnrotateVector(Velocity).Y / QU(320.f), -1.f, 1.f);
	const FRotator Rotation(Sway.Pitch + ViewKick * View.KickPitch - Lowered * 35.f, Sway.Yaw, Sway.Yaw * 0.5f - Strafe * 3.f);
	ViewGunMesh->SetRelativeLocationAndRotation(Offset, Rotation);
}

void AArenaCharacter::UpdateGunEffects(float DeltaSeconds)
{
	const bool bLocalView = IsLocalPlayerView();
	FlashTime -= DeltaSeconds;
	const bool bFlash = FlashTime > 0.f;
	ViewFlash->SetVisibility(bFlash && bLocalView);
	WorldFlash->SetVisibility(bFlash && !bLocalView);
	FlashLight->SetVisibility(bFlash);

	const FArenaWeaponInfo& Info = GetWeaponInfo(CurrentWeapon);
	const FViewModelInfo& View = GetViewModel(CurrentWeapon);
	const float SinceShot = GetWorld()->GetTimeSeconds() - LastShotTime;
	const float TargetSpin = SinceShot < Info.RefireTime + 0.1f ? View.FireSpin : View.IdleSpin;
	SpinSpeed = FMath::FInterpTo(SpinSpeed, TargetSpin, DeltaSeconds, TargetSpin > SpinSpeed ? 8.f : 1.5f);
	SpinAngle = FMath::Fmod(SpinAngle + SpinSpeed * DeltaSeconds, 360.f);
	(bLocalView ? ViewGunSpin : WorldGunSpin)->SetRelativeRotation(FRotator(0.f, 0.f, SpinAngle));

	// The railgun's core recharges between shots.
	if (CurrentWeapon == EArenaWeapon::Railgun)
	{
		const float Charge = FMath::Clamp(SinceShot / Info.RefireTime, 0.f, 1.f);
		ArenaVisuals::SetGlowParam(bLocalView ? ViewGunMesh.Get() : WorldGunMesh.Get(), TEXT("Emissive"), FMath::Lerp(0.3f, 12.f, Charge * Charge));
	}
}

void AArenaCharacter::UpdateDeathCamera(float DeltaSeconds)
{
	// Pull back from the ragdoll and watch it.
	const FVector Target = GetMesh()->GetBoneLocation(TEXT("pelvis"));
	const FVector From = Camera->GetComponentLocation();
	FVector Away = From - Target;
	Away.Z = 0.f;
	Away = Away.GetSafeNormal2D();
	if (Away.IsNearlyZero())
	{
		Away = -GetActorForwardVector();
	}
	FVector Desired = Target + Away * 260.f + FVector(0.f, 0.f, 140.f);
	FHitResult Hit;
	const FCollisionQueryParams Params(SCENE_QUERY_STAT(ArenaDeathCamera), false, this);
	if (GetWorld()->SweepSingleByChannel(Hit, Target + FVector(0.f, 0.f, 40.f), Desired, FQuat::Identity, ECC_Camera, FCollisionShape::MakeSphere(12.f), Params))
	{
		Desired = Hit.Location;
	}
	const FVector Location = FMath::VInterpTo(From, Desired, DeltaSeconds, 3.f);
	Camera->SetWorldLocationAndRotation(Location, (Target - Location).Rotation());
}

void AArenaCharacter::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (bDead)
	{
		UpdateDeathCamera(DeltaSeconds);
		return;
	}

	// Eye height eases between standing and ducked, like GoldSrc's view offset.
	const float TargetEye = bIsCrouched ? CrouchedEyeHeight : StandingEyeHeight;
	FVector CamLoc = Camera->GetRelativeLocation();
	CamLoc.Z = FMath::FInterpTo(CamLoc.Z, TargetEye, DeltaSeconds, 18.f);
	Camera->SetRelativeLocation(CamLoc);

	ViewKick = FMath::FInterpTo(ViewKick, 0.f, DeltaSeconds, 12.f);
	UpdateWorldGun(DeltaSeconds);
	if (IsLocalPlayerView())
	{
		UpdateViewModel(DeltaSeconds);
	}
	UpdateGunEffects(DeltaSeconds);
	UpdateMovementSounds(DeltaSeconds);

	if (IsLocalPlayerView())
	{
		Camera->SetFieldOfView(UArenaSettings::Get()->FieldOfView);
	}

	// Health and armor above 100 count down one point per second (Quake 3).
	if (HasAuthority() && !bDead)
	{
		const AArenaGameState* GS = GetWorld()->GetGameState<AArenaGameState>();
		if (GetActorLocation().Z < ArenaMap::Get(GS ? GS->MapId : NAME_None).KillZ)
		{
			Die(nullptr, EArenaWeapon::Count); // Count = killed by the world.
			return;
		}

		TickDownAccumulator += DeltaSeconds;
		while (TickDownAccumulator >= 1.f)
		{
			TickDownAccumulator -= 1.f;
			if (Health > 100) { --Health; }
			if (Armor > 100) { --Armor; }
		}
	}
}

void AArenaCharacter::UpdateMovementSounds(float DeltaSeconds)
{
	const UCharacterMovementComponent* Move = GetCharacterMovement();
	const bool bOnGround = Move->IsMovingOnGround();
	const FVector Velocity = GetVelocity();

	if (bWasOnGround && !bOnGround && Velocity.Z > QU(100.f))
	{
		UArenaAudio::PlayAt(this, EArenaSound::Jump, GetActorLocation(), 0.35f, FMath::FRandRange(0.95f, 1.05f));
	}
	else if (!bWasOnGround && bOnGround)
	{
		const float FallSpeed = -LastVelocityZ;
		if (FallSpeed > QU(250.f))
		{
			UArenaAudio::PlayAt(this, EArenaSound::Land, GetActorLocation(), FMath::GetMappedRangeValueClamped(FVector2D(QU(250.f), QU(700.f)), FVector2D(0.35f, 1.f), FallSpeed));
			LandDip = FMath::GetMappedRangeValueClamped(FVector2D(QU(250.f), QU(900.f)), FVector2D(0.5f, 2.5f), FallSpeed);
		}
		FootstepTimer = 0.2f;
	}

	// GoldSrc: a footstep every 0.3 s when running, 0.4 s when walking; silent when ducked or slow.
	const float Speed = Velocity.Size2D();
	if (bOnGround && !bIsCrouched && Speed > QU(150.f))
	{
		FootstepTimer -= DeltaSeconds;
		if (FootstepTimer <= 0.f)
		{
			UArenaAudio::PlayAt(this, EArenaSound::Footstep, GetActorLocation() - FVector(0.f, 0.f, QU(30.f)), 0.5f, FMath::FRandRange(0.85f, 1.15f));
			FootstepTimer = Speed > QU(220.f) ? 0.3f : 0.4f;
		}
	}

	bWasOnGround = bOnGround;
	LastVelocityZ = Velocity.Z;
}

void AArenaCharacter::OnRep_Health(int32 OldHealth)
{
	// Ignore the 1 point/s tick-down above 100.
	const float Now = GetWorld()->GetTimeSeconds();
	if (!bDead && Health > 0 && OldHealth - Health >= 2 && Now - LastPainTime > 0.3f)
	{
		LastPainTime = Now;
		UArenaAudio::PlayAt(this, EArenaSound::Pain, GetActorLocation(), 0.8f, FMath::FRandRange(0.9f, 1.1f));
	}
}

// ---------------------------------------------------------------------------
// Input
// ---------------------------------------------------------------------------

void AArenaCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	UEnhancedInputComponent* Input = Cast<UEnhancedInputComponent>(PlayerInputComponent);
	const AArenaPlayerController* PC = Cast<AArenaPlayerController>(GetController());
	if (!Input || !PC || !PC->MappingContext)
	{
		return;
	}

	Input->BindAction(PC->MoveForwardAction, ETriggerEvent::Triggered, this, &AArenaCharacter::OnMoveForward);
	Input->BindAction(PC->MoveRightAction, ETriggerEvent::Triggered, this, &AArenaCharacter::OnMoveRight);
	Input->BindAction(PC->LookAction, ETriggerEvent::Triggered, this, &AArenaCharacter::OnLook);
	Input->BindAction(PC->JumpAction, ETriggerEvent::Started, this, &AArenaCharacter::OnJumpStarted);
	Input->BindAction(PC->JumpAction, ETriggerEvent::Triggered, this, &AArenaCharacter::OnJumpHeld);
	Input->BindAction(PC->JumpAction, ETriggerEvent::Completed, this, &AArenaCharacter::OnJumpReleased);
	Input->BindAction(PC->JumpWheelAction, ETriggerEvent::Started, this, &AArenaCharacter::OnJumpWheel);
	Input->BindAction(PC->CrouchAction, ETriggerEvent::Started, this, &AArenaCharacter::OnCrouchStarted);
	Input->BindAction(PC->CrouchAction, ETriggerEvent::Completed, this, &AArenaCharacter::OnCrouchReleased);
	Input->BindAction(PC->FireAction, ETriggerEvent::Triggered, this, &AArenaCharacter::OnFireHeld);
	Input->BindAction(PC->NextWeaponAction, ETriggerEvent::Started, this, &AArenaCharacter::OnNextWeapon);
	Input->BindAction(PC->LastWeaponAction, ETriggerEvent::Started, this, &AArenaCharacter::OnLastWeapon);
	for (const UInputAction* WeaponAction : PC->WeaponActions)
	{
		Input->BindAction(WeaponAction, ETriggerEvent::Started, this, &AArenaCharacter::OnSelectWeaponAction);
	}
}

// GoldSrc builds wishdir from view yaw only, so looking up/down never slows you.
void AArenaCharacter::OnMoveForward(const FInputActionValue& Value)
{
	AddMovementInput(FRotator(0.f, GetControlRotation().Yaw, 0.f).Vector(), Value.Get<float>());
}

void AArenaCharacter::OnMoveRight(const FInputActionValue& Value)
{
	AddMovementInput(FRotationMatrix(FRotator(0.f, GetControlRotation().Yaw, 0.f)).GetUnitAxis(EAxis::Y), Value.Get<float>());
}

void AArenaCharacter::OnLook(const FInputActionValue& Value)
{
	const UArenaSettings* Settings = UArenaSettings::Get();
	const float Scale = MouseYawPerCount * Settings->Sensitivity;
	const FVector2D Delta = Value.Get<FVector2D>();
	AddControllerYawInput(Delta.X * Scale);
	AddControllerPitchInput(Delta.Y * Scale * (Settings->bInvertMouse ? -1.f : 1.f));
}

void AArenaCharacter::OnJumpStarted()
{
	bJumpHeld = true;
	Jump();
}

void AArenaCharacter::OnJumpHeld()
{
	if (UArenaSettings::Get()->bAutoHop)
	{
		Jump();
	}
}

void AArenaCharacter::OnJumpReleased()
{
	bJumpHeld = false;
	StopJumping();
}

void AArenaCharacter::OnJumpWheel()
{
	// Scroll-wheel jumping: a single-frame press, never followed by StopJumping,
	// so the queued jump survives until the movement tick consumes it.
	Jump();
}

void AArenaCharacter::OnCrouchStarted()
{
	Crouch();
}

void AArenaCharacter::OnCrouchReleased()
{
	UnCrouch();
}

bool AArenaCharacter::CanJumpInternal_Implementation() const
{
	// Allow ducked jumps (Unreal's default refuses to jump while crouched).
	return JumpIsAllowedInternal();
}

// ---------------------------------------------------------------------------
// Weapons
// ---------------------------------------------------------------------------

void AArenaCharacter::OnFireHeld()
{
	TryFire();
}

void AArenaCharacter::OnSelectWeaponAction(const FInputActionInstance& Instance)
{
	if (const AArenaPlayerController* PC = Cast<AArenaPlayerController>(GetController()))
	{
		const int32 Index = PC->WeaponActions.IndexOfByKey(Instance.GetSourceAction());
		if (Index >= 0 && Index < ArenaWeaponCount)
		{
			SelectWeapon(static_cast<EArenaWeapon>(Index));
		}
	}
}

bool AArenaCharacter::CanFire(EArenaWeapon Weapon) const
{
	return HasWeapon(Weapon) && (!GetWeaponInfo(Weapon).UsesAmmo() || GetAmmo(Weapon) > 0);
}

void AArenaCharacter::OnNextWeapon()
{
	// Cycles through weapons you can actually fire, like Quake's weapnext.
	for (int32 Step = 1; Step <= ArenaWeaponCount; ++Step)
	{
		const EArenaWeapon Candidate = static_cast<EArenaWeapon>(((int32)CurrentWeapon + Step) % ArenaWeaponCount);
		if (CanFire(Candidate))
		{
			SelectWeapon(Candidate);
			return;
		}
	}
}

void AArenaCharacter::OnLastWeapon()
{
	SelectWeapon(LastWeapon);
}

void AArenaCharacter::SelectWeapon(EArenaWeapon Weapon)
{
	if (bDead || Weapon == CurrentWeapon || !HasWeapon(Weapon))
	{
		return;
	}
	LastWeapon = CurrentWeapon;
	CurrentWeapon = Weapon;
	UpdateWeaponVisuals();
	// Quake-ish switch time.
	NextFireTime = FMath::Max(NextFireTime, GetWorld()->GetTimeSeconds() + 0.15f);
	if (!HasAuthority())
	{
		ServerSelectWeapon(Weapon);
	}
}

void AArenaCharacter::ServerSelectWeapon_Implementation(EArenaWeapon Weapon)
{
	if (HasWeapon(Weapon))
	{
		LastWeapon = CurrentWeapon;
		CurrentWeapon = Weapon;
		UpdateWeaponVisuals();
	}
}

void AArenaCharacter::OnRep_CurrentWeapon()
{
	UpdateWeaponVisuals();
}

void AArenaCharacter::TryFire()
{
	if (bDead)
	{
		return;
	}
	const float Now = GetWorld()->GetTimeSeconds();
	if (Now < NextFireTime)
	{
		return;
	}
	if (!CanFire(CurrentWeapon))
	{
		if (IsLocalPlayerView())
		{
			UArenaAudio::Play2D(this, EArenaSound::NoAmmo, 0.6f);
		}
		NextFireTime = Now + 0.4f;
		OnNextWeapon();
		return;
	}

	const FArenaWeaponInfo& Info = GetWeaponInfo(CurrentWeapon);
	NextFireTime = Now + Info.RefireTime;
	ViewKick = 1.f;
	PlayFireEffects();
	// Your own gun is heard without spatialization; others hear it via the server.
	// The lightning gun fires 20 times a second, so keep it quieter.
	if (IsLocalPlayerView())
	{
		const float OwnVolume = CurrentWeapon == EArenaWeapon::LightningGun ? 0.35f : 0.6f;
		UArenaAudio::Play2D(this, Info.FireSound, OwnVolume, FMath::FRandRange(0.97f, 1.03f));
	}

	const FVector Origin = Camera->GetComponentLocation();
	const FVector Dir = GetControlRotation().Vector();
	const int32 Seed = FMath::Rand();

	// Predict hitscan tracers locally so the shooter sees them without latency.
	if (!HasAuthority() && !Info.bProjectile && Info.TracerWidth > 0.f)
	{
		TArray<FVector> Dirs;
		GetShotDirections(Info, Dir, Seed, Dirs);
		TArray<FVector_NetQuantize> Ends;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(ArenaTracerPredict), true, this);
		for (const FVector& ShotDir : Dirs)
		{
			FHitResult Hit;
			const FVector End = Origin + ShotDir * Info.Range;
			const bool bHit = GetWorld()->LineTraceSingleByObjectType(Hit, Origin, End, FCollisionObjectQueryParams(ECC_WorldStatic), Params);
			Ends.Add(bHit ? Hit.ImpactPoint : End);
		}
		AArenaGameState::SpawnShotVisual(GetWorld(), this, CurrentWeapon, Ends);
	}

	ServerFire(Origin, Dir, Seed);
}

void AArenaCharacter::ServerFire_Implementation(FVector_NetQuantize Origin, FVector_NetQuantizeNormal Dir, int32 Seed)
{
	const float Now = GetWorld()->GetTimeSeconds();
	if (bDead || Now < ServerNextFireTime || !CanFire(CurrentWeapon))
	{
		return;
	}

	// Allow some slack for jitter, but never faster than 80% of the refire time.
	const FArenaWeaponInfo& Info = GetWeaponInfo(CurrentWeapon);
	ServerNextFireTime = Now + Info.RefireTime * 0.8f;
	if (Info.UsesAmmo())
	{
		--Ammo[(int32)CurrentWeapon];
	}

	// Trust the client's view, within reason.
	const FVector ServerEye = Camera->GetComponentLocation();
	const FVector ShotOrigin = FVector::DistSquared(Origin, ServerEye) < FMath::Square(250.f) ? FVector(Origin) : ServerEye;
	const FVector ShotDir = Dir.GetSafeNormal();

	if (Info.bProjectile)
	{
		FireProjectile(ShotOrigin, ShotDir, CurrentWeapon);
	}
	else
	{
		FireHitscan(ShotOrigin, ShotDir, CurrentWeapon, Seed);
	}
}

void AArenaCharacter::FireHitscan(const FVector& Origin, const FVector& Dir, EArenaWeapon Weapon, int32 Seed)
{
	const FArenaWeaponInfo& Info = GetWeaponInfo(Weapon);
	TArray<FVector> Dirs;
	GetShotDirections(Info, Dir, Seed, Dirs);

	FCollisionObjectQueryParams Objects;
	Objects.AddObjectTypesToQuery(ECC_WorldStatic);
	Objects.AddObjectTypesToQuery(ECC_Pawn);

	// Pellets are summed per victim so a shotgun blast is one hit: one knockback,
	// one hit sound and one damage number.
	TMap<AArenaCharacter*, float> DamageByVictim;
	TArray<FVector_NetQuantize> Ends;
	for (const FVector& ShotDir : Dirs)
	{
		const FVector End = Origin + ShotDir * Info.Range;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(ArenaHitscan), true, this);
		FVector TraceStart = Origin;
		FVector VisualEnd = End;
		// The railgun passes through players (MaxPierce), like Quake 3.
		for (int32 i = 0; i < Info.MaxPierce; ++i)
		{
			FHitResult Hit;
			if (!GetWorld()->LineTraceSingleByObjectType(Hit, TraceStart, End, Objects, Params))
			{
				VisualEnd = End;
				break;
			}
			VisualEnd = Hit.ImpactPoint;

			AArenaCharacter* Victim = Cast<AArenaCharacter>(Hit.GetActor());
			if (!Victim)
			{
				break;
			}
			DamageByVictim.FindOrAdd(Victim) += Info.Damage;
			Params.AddIgnoredActor(Victim);
			TraceStart = Hit.ImpactPoint;
			if (Info.MaxPierce > 1)
			{
				VisualEnd = End;
			}
		}
		Ends.Add(VisualEnd);
	}

	for (const TPair<AArenaCharacter*, float>& Pair : DamageByVictim)
	{
		Pair.Key->ApplyArenaDamage(Pair.Value, GetController(), ArenaKnockback(Dir, Pair.Value), Weapon, Origin);
	}

	if (AArenaGameState* GS = GetWorld()->GetGameState<AArenaGameState>())
	{
		GS->MulticastShot(this, Weapon, Ends);
	}
}

void AArenaCharacter::FireProjectile(const FVector& Origin, const FVector& Dir, EArenaWeapon Weapon)
{
	// Quake 3 lobs grenades slightly upward (dir.z += 0.2).
	const FVector LaunchDir = Weapon == EArenaWeapon::GrenadeLauncher ? (Dir + FVector(0.f, 0.f, 0.2f)).GetSafeNormal() : Dir;

	FActorSpawnParameters Params;
	Params.Owner = this;
	Params.Instigator = this;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	GetWorld()->SpawnActor<AArenaProjectile>(AArenaProjectile::ClassForWeapon(Weapon), Origin + LaunchDir * QU(10.f), LaunchDir.Rotation(), Params);
}

// ---------------------------------------------------------------------------
// Damage, pickups, death
// ---------------------------------------------------------------------------

void AArenaCharacter::ApplyArenaDamage(float Damage, AController* InstigatorController, const FVector& Knockback, EArenaWeapon Weapon, const FVector& SourceLocation)
{
	if (!HasAuthority() || bDead)
	{
		return;
	}
	const AArenaGameMode* GM = GetWorld()->GetAuthGameMode<AArenaGameMode>();
	if (GM && !GM->IsMatchInProgress())
	{
		return;
	}

	// Knockback uses full damage; self damage is then halved (Quake 3 G_Damage).
	AddKnockback(Knockback);

	const bool bSelf = InstigatorController && InstigatorController == GetController();
	const int32 Points = FMath::CeilToInt(bSelf ? Damage * 0.5f : Damage);
	const int32 Absorbed = FMath::Min(FMath::CeilToInt(Points * 0.66f), Armor);
	const int32 OldHealth = Health;
	Armor -= Absorbed;
	Health -= Points - Absorbed;
	OnRep_Health(OldHealth); // Pain sound on a listen server; clients get it via replication.

	if (AArenaPlayerController* Attacker = Cast<AArenaPlayerController>(InstigatorController); Attacker && !bSelf)
	{
		Attacker->ClientHitConfirmed(GetActorLocation(), Points, Health <= 0);
	}
	if (AArenaPlayerController* VictimPC = Cast<AArenaPlayerController>(GetController()))
	{
		VictimPC->ClientTookDamage(SourceLocation, Points);
	}

	if (Health <= 0)
	{
		Die(InstigatorController, Weapon);
	}
}

void AArenaCharacter::AddKnockback(const FVector& Impulse)
{
	if (Impulse.IsNearlyZero())
	{
		return;
	}
	LaunchCharacter(Impulse, false, false);
	if (HasAuthority() && !IsLocallyControlled())
	{
		// Mirror on the owning client so its prediction matches the server.
		ClientAddKnockback(Impulse);
	}
}

void AArenaCharacter::ClientAddKnockback_Implementation(FVector_NetQuantize Impulse)
{
	LaunchCharacter(Impulse, false, false);
}

bool AArenaCharacter::GiveHealth(int32 Amount, int32 Cap)
{
	if (bDead || Health >= Cap)
	{
		return false;
	}
	Health = FMath::Min(Health + Amount, Cap);
	return true;
}

bool AArenaCharacter::GiveArmor(int32 Amount)
{
	if (bDead || Armor >= MaxArmor)
	{
		return false;
	}
	Armor = FMath::Min(Armor + Amount, MaxArmor);
	return true;
}

bool AArenaCharacter::GiveWeapon(EArenaWeapon Weapon, int32 AmmoAmount)
{
	const int32 Index = (int32)Weapon;
	if (bDead || !Ammo.IsValidIndex(Index))
	{
		return false;
	}
	const FArenaWeaponInfo& Info = GetWeaponInfo(Weapon);
	const bool bNew = !HasWeapon(Weapon);
	if (!Info.UsesAmmo())
	{
		OwnedWeapons |= (1 << Index);
		return bNew;
	}
	if (!bNew && Ammo[Index] >= Info.MaxAmmo)
	{
		return false;
	}
	OwnedWeapons |= (1 << Index);
	Ammo[Index] = FMath::Min(Ammo[Index] + AmmoAmount, Info.MaxAmmo);
	return true;
}

void AArenaCharacter::Die(AController* Killer, EArenaWeapon Weapon)
{
	AController* Victim = GetController();
	bDead = true;
	OnRep_Dead();

	if (AArenaGameState* GS = GetWorld()->GetGameState<AArenaGameState>())
	{
		GS->MulticastExplosion(GetActorLocation(), FLinearColor(0.6f, 0.02f, 0.02f), QU(60.f), EArenaSound::Death);
	}
	if (AArenaGameMode* GM = GetWorld()->GetAuthGameMode<AArenaGameMode>())
	{
		GM->OnPlayerKilled(Killer, Victim, Weapon);
	}

	DetachFromControllerPendingDestroy();
	if (APlayerController* PC = Cast<APlayerController>(Victim))
	{
		PC->SetViewTarget(this); // Watch from the corpse until respawn.
	}
	SetLifeSpan(5.f);
}

void AArenaCharacter::OnRep_Dead()
{
	if (!bDead)
	{
		return;
	}
	for (UStaticMeshComponent* Part : { WorldGunMesh.Get(), WorldGunSpin.Get(), WorldFlash.Get(), ViewGunMesh.Get(), ViewGunSpin.Get(), ViewFlash.Get() })
	{
		Part->SetVisibility(false);
	}
	FlashLight->SetVisibility(false);
	GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	const FVector DeathVelocity = GetVelocity();
	GetCharacterMovement()->DisableMovement();
	GetCharacterMovement()->StopMovementImmediately();

	// Ragdoll, thrown along with whatever killed us (rockets send bodies flying).
	USkeletalMeshComponent* Body = GetMesh();
	if (GetNetMode() != NM_DedicatedServer && Body->GetPhysicsAsset())
	{
		Body->SetOwnerNoSee(false);
		Body->SetCollisionProfileName(TEXT("Ragdoll"));
		Body->SetAllBodiesSimulatePhysics(true);
		Body->SetSimulatePhysics(true);
		Body->WakeAllRigidBodies();
		Body->SetAllPhysicsLinearVelocity(DeathVelocity.GetClampedToMaxSize(1800.f) + FVector(0.f, 0.f, 150.f));
	}
	else
	{
		Body->SetVisibility(false);
	}

	// The death camera (UpdateDeathCamera) takes over from the view.
	Camera->bUsePawnControlRotation = false;
}
