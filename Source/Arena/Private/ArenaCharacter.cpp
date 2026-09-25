#include "ArenaCharacter.h"
#include "Arena.h"
#include "ArenaAudio.h"
#include "ArenaSettings.h"
#include "ArenaMovementComponent.h"
#include "ArenaPlayerController.h"
#include "ArenaPlayerState.h"
#include "ArenaGameMode.h"
#include "ArenaGameState.h"
#include "ArenaProjectile.h"
#include "ArenaVisuals.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "EnhancedInputComponent.h"
#include "InputAction.h"
#include "InputActionValue.h"
#include "Engine/StaticMesh.h"
#include "Net/UnrealNetwork.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	constexpr float StandingEyeHeight = QU(28.f); // VEC_VIEW
	constexpr float CrouchedEyeHeight = QU(12.f); // VEC_DUCK_VIEW
	constexpr float MouseYawPerCount = 0.022f;    // m_yaw / m_pitch

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

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereMesh(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylinderMesh(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));

	// GoldSrc hull: 32x32x72 standing, 32x32x36 ducked.
	GetCapsuleComponent()->InitCapsuleSize(QU(16.f), QU(36.f));
	GetCharacterMovement()->SetCrouchedHalfHeight(QU(18.f));

	bUseControllerRotationYaw = true;
	bUseControllerRotationPitch = false;
	bUseControllerRotationRoll = false;
	JumpMaxHoldTime = 0.f;
	JumpMaxCount = 1;

	GetMesh()->SetVisibility(false);

	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	Camera->SetupAttachment(GetCapsuleComponent());
	Camera->SetRelativeLocation(FVector(0.f, 0.f, StandingEyeHeight));
	Camera->bUsePawnControlRotation = true;
	Camera->SetFieldOfView(100.f);

	BodyMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Body"));
	BodyMesh->SetupAttachment(GetCapsuleComponent());
	ArenaVisuals::SetupCosmeticMesh(BodyMesh, CylinderMesh.Object);
	BodyMesh->SetRelativeScale3D(FVector(QU(28.f) / 100.f, QU(28.f) / 100.f, QU(56.f) / 100.f));
	BodyMesh->SetRelativeLocation(FVector(0.f, 0.f, -QU(8.f)));
	BodyMesh->SetOwnerNoSee(true);

	HeadMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Head"));
	HeadMesh->SetupAttachment(GetCapsuleComponent());
	ArenaVisuals::SetupCosmeticMesh(HeadMesh, SphereMesh.Object);
	HeadMesh->SetRelativeScale3D(FVector(QU(18.f) / 100.f));
	HeadMesh->SetRelativeLocation(FVector(0.f, 0.f, QU(28.f)));
	HeadMesh->SetOwnerNoSee(true);

	WorldGunMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("WorldGun"));
	WorldGunMesh->SetupAttachment(GetCapsuleComponent());
	ArenaVisuals::SetupCosmeticMesh(WorldGunMesh, CubeMesh.Object);
	WorldGunMesh->SetRelativeLocation(FVector(QU(14.f), QU(10.f), QU(16.f)));
	WorldGunMesh->SetOwnerNoSee(true);

	ViewGunMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ViewGun"));
	ViewGunMesh->SetupAttachment(Camera);
	ArenaVisuals::SetupCosmeticMesh(ViewGunMesh, CubeMesh.Object);
	ViewGunMesh->SetRelativeLocation(FVector(35.f, 14.f, -14.f));
	ViewGunMesh->SetOnlyOwnerSee(true);
	ViewGunMesh->SetCastShadow(false);
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
	const FLinearColor Color = PS ? PS->GetPlayerColor() : FLinearColor::Gray;
	ArenaVisuals::SetColor(BodyMesh, Color);
	ArenaVisuals::SetColor(HeadMesh, Color * 0.6f + FLinearColor::White * 0.4f);
}

FVector AArenaCharacter::GetMuzzleLocation() const
{
	const UStaticMeshComponent* Gun = IsLocallyControlled() ? ViewGunMesh.Get() : WorldGunMesh.Get();
	// Cube is 100 cm, so half-length along X is Scale.X * 50.
	return Gun->GetComponentLocation() + Gun->GetForwardVector() * Gun->GetComponentScale().X * 50.f;
}

void AArenaCharacter::UpdateWeaponVisuals()
{
	const FArenaWeaponInfo& Info = GetWeaponInfo(CurrentWeapon);
	FVector Scale;
	switch (CurrentWeapon)
	{
	case EArenaWeapon::Gauntlet:        Scale = FVector(0.18f, 0.14f, 0.14f); break;
	case EArenaWeapon::Shotgun:         Scale = FVector(0.40f, 0.10f, 0.08f); break;
	case EArenaWeapon::GrenadeLauncher: Scale = FVector(0.35f, 0.12f, 0.12f); break;
	case EArenaWeapon::RocketLauncher:  Scale = FVector(0.45f, 0.11f, 0.11f); break;
	case EArenaWeapon::LightningGun:    Scale = FVector(0.40f, 0.08f, 0.10f); break;
	case EArenaWeapon::Railgun:         Scale = FVector(0.60f, 0.06f, 0.08f); break;
	case EArenaWeapon::PlasmaGun:       Scale = FVector(0.38f, 0.10f, 0.10f); break;
	default:                            Scale = FVector(0.35f, 0.07f, 0.09f); break;
	}
	ViewGunMesh->SetRelativeScale3D(Scale);
	WorldGunMesh->SetRelativeScale3D(Scale);
	ArenaVisuals::SetColor(ViewGunMesh, Info.Color);
	ArenaVisuals::SetColor(WorldGunMesh, Info.Color);
}

void AArenaCharacter::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	// Eye height eases between standing and ducked, like GoldSrc's view offset.
	const float TargetEye = bIsCrouched ? CrouchedEyeHeight : StandingEyeHeight;
	FVector CamLoc = Camera->GetRelativeLocation();
	CamLoc.Z = FMath::FInterpTo(CamLoc.Z, TargetEye, DeltaSeconds, 18.f);
	Camera->SetRelativeLocation(CamLoc);

	// First-person recoil.
	ViewKick = FMath::FInterpTo(ViewKick, 0.f, DeltaSeconds, 12.f);
	ViewGunMesh->SetRelativeLocation(FVector(35.f - ViewKick * 8.f, 14.f, -14.f));

	// Third-person gun follows aim pitch (replicated via RemoteViewPitch).
	WorldGunMesh->SetRelativeRotation(FRotator(GetBaseAimRotation().Pitch, 0.f, 0.f));

	UpdateMovementSounds(DeltaSeconds);

	if (IsLocallyControlled())
	{
		Camera->SetFieldOfView(UArenaSettings::Get()->FieldOfView);
	}

	// Health and armor above 100 count down one point per second (Quake 3).
	if (HasAuthority() && !bDead)
	{
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
		UArenaAudio::Play2D(this, EArenaSound::NoAmmo, 0.6f);
		NextFireTime = Now + 0.4f;
		OnNextWeapon();
		return;
	}

	const FArenaWeaponInfo& Info = GetWeaponInfo(CurrentWeapon);
	NextFireTime = Now + Info.RefireTime;
	ViewKick = 1.f;
	// Your own gun is heard without spatialization; others hear it via the server.
	// The lightning gun fires 20 times a second, so keep it quieter.
	const float OwnVolume = CurrentWeapon == EArenaWeapon::LightningGun ? 0.35f : 0.6f;
	UArenaAudio::Play2D(this, Info.FireSound, OwnVolume, FMath::FRandRange(0.97f, 1.03f));

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
	BodyMesh->SetVisibility(false);
	HeadMesh->SetVisibility(false);
	WorldGunMesh->SetVisibility(false);
	ViewGunMesh->SetVisibility(false);
	GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	GetCharacterMovement()->DisableMovement();
	GetCharacterMovement()->StopMovementImmediately();
	// Drop the view to the floor.
	FRotator DeathView = Camera->GetComponentRotation();
	DeathView.Roll = 25.f;
	Camera->bUsePawnControlRotation = false;
	Camera->SetRelativeLocation(FVector(0.f, 0.f, -QU(24.f)));
	Camera->SetWorldRotation(DeathView);
	SetActorTickEnabled(false);
}
