#include "ArenaCharacter.h"
#include "Arena.h"
#include "ArenaMovementComponent.h"
#include "ArenaPlayerController.h"
#include "ArenaPlayerState.h"
#include "ArenaGameMode.h"
#include "ArenaGameState.h"
#include "ArenaRocket.h"
#include "ArenaVisuals.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "EnhancedInputComponent.h"
#include "InputActionValue.h"
#include "Engine/StaticMesh.h"
#include "Net/UnrealNetwork.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	constexpr float StandingEyeHeight = QU(28.f); // VEC_VIEW
	constexpr float CrouchedEyeHeight = QU(12.f); // VEC_DUCK_VIEW
	constexpr float MouseYawPerCount = 0.022f;    // m_yaw / m_pitch
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
		GiveWeapon(EArenaWeapon::MachineGun, GetWeaponInfo(EArenaWeapon::MachineGun).StartAmmo);
	}
	UpdateColors();
	UpdateWeaponVisuals();
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
	case EArenaWeapon::RocketLauncher: Scale = FVector(0.45f, 0.11f, 0.11f); break;
	case EArenaWeapon::Railgun:        Scale = FVector(0.60f, 0.06f, 0.08f); break;
	default:                           Scale = FVector(0.35f, 0.07f, 0.09f); break;
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
	if (PC->WeaponActions.Num() >= 3)
	{
		Input->BindAction(PC->WeaponActions[0], ETriggerEvent::Started, this, &AArenaCharacter::OnWeapon1);
		Input->BindAction(PC->WeaponActions[1], ETriggerEvent::Started, this, &AArenaCharacter::OnWeapon2);
		Input->BindAction(PC->WeaponActions[2], ETriggerEvent::Started, this, &AArenaCharacter::OnWeapon3);
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
	const AArenaPlayerController* PC = Cast<AArenaPlayerController>(GetController());
	const float Scale = MouseYawPerCount * (PC ? PC->Sensitivity : 2.5f);
	const FVector2D Delta = Value.Get<FVector2D>();
	AddControllerYawInput(Delta.X * Scale);
	AddControllerPitchInput(Delta.Y * Scale);
}

void AArenaCharacter::OnJumpStarted()
{
	bJumpHeld = true;
	Jump();
}

void AArenaCharacter::OnJumpHeld()
{
	const AArenaPlayerController* PC = Cast<AArenaPlayerController>(GetController());
	if (PC && PC->bAutoHop)
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

void AArenaCharacter::OnNextWeapon()
{
	for (int32 Step = 1; Step <= ArenaWeaponCount; ++Step)
	{
		const EArenaWeapon Candidate = static_cast<EArenaWeapon>(((int32)CurrentWeapon + Step) % ArenaWeaponCount);
		if (HasWeapon(Candidate))
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
	if (GetAmmo(CurrentWeapon) <= 0)
	{
		OnNextWeapon();
		return;
	}

	const FArenaWeaponInfo& Info = GetWeaponInfo(CurrentWeapon);
	NextFireTime = Now + Info.RefireTime;
	ViewKick = 1.f;

	const FVector Origin = Camera->GetComponentLocation();
	const FVector Dir = GetControlRotation().Vector();

	// Predict hitscan tracers locally so the shooter sees them without latency.
	if (!HasAuthority() && CurrentWeapon != EArenaWeapon::RocketLauncher)
	{
		FHitResult Hit;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(ArenaTracerPredict), true, this);
		const FVector End = Origin + Dir * 50000.f;
		const bool bHit = GetWorld()->LineTraceSingleByObjectType(Hit, Origin, End, FCollisionObjectQueryParams(ECC_WorldStatic), Params);
		AArenaGameState::SpawnShotVisual(GetWorld(), this, CurrentWeapon, bHit ? Hit.ImpactPoint : End);
	}

	ServerFire(Origin, Dir);
}

void AArenaCharacter::ServerFire_Implementation(FVector_NetQuantize Origin, FVector_NetQuantizeNormal Dir)
{
	const float Now = GetWorld()->GetTimeSeconds();
	if (bDead || Now < ServerNextFireTime || GetAmmo(CurrentWeapon) <= 0)
	{
		return;
	}

	// Allow some slack for jitter, but never faster than 80% of the refire time.
	const FArenaWeaponInfo& Info = GetWeaponInfo(CurrentWeapon);
	ServerNextFireTime = Now + Info.RefireTime * 0.8f;
	--Ammo[(int32)CurrentWeapon];

	// Trust the client's view, within reason.
	const FVector ServerEye = Camera->GetComponentLocation();
	const FVector ShotOrigin = FVector::DistSquared(Origin, ServerEye) < FMath::Square(250.f) ? FVector(Origin) : ServerEye;
	const FVector ShotDir = Dir.GetSafeNormal();

	if (CurrentWeapon == EArenaWeapon::RocketLauncher)
	{
		FireRocket(ShotOrigin, ShotDir);
	}
	else
	{
		FireHitscan(ShotOrigin, ShotDir, CurrentWeapon);
	}
}

void AArenaCharacter::FireHitscan(const FVector& Origin, const FVector& Dir, EArenaWeapon Weapon)
{
	const FArenaWeaponInfo& Info = GetWeaponInfo(Weapon);
	const bool bRail = Weapon == EArenaWeapon::Railgun;
	const FVector ShotDir = bRail ? Dir : FMath::VRandCone(Dir, FMath::DegreesToRadians(1.2f));
	const FVector End = Origin + ShotDir * 50000.f;

	FCollisionQueryParams Params(SCENE_QUERY_STAT(ArenaHitscan), true, this);
	FCollisionObjectQueryParams Objects;
	Objects.AddObjectTypesToQuery(ECC_WorldStatic);
	Objects.AddObjectTypesToQuery(ECC_Pawn);

	// The railgun passes through players (up to 4), like Quake 3.
	FVector TraceStart = Origin;
	FVector VisualEnd = End;
	const int32 MaxHits = bRail ? 4 : 1;
	for (int32 i = 0; i < MaxHits; ++i)
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
		Victim->ApplyArenaDamage(Info.Damage, GetController(), ArenaKnockback(ShotDir, Info.Damage), Weapon);
		Params.AddIgnoredActor(Victim);
		TraceStart = Hit.ImpactPoint;
		VisualEnd = End;
	}

	if (AArenaGameState* GS = GetWorld()->GetGameState<AArenaGameState>())
	{
		GS->MulticastShot(this, Weapon, VisualEnd);
	}
}

void AArenaCharacter::FireRocket(const FVector& Origin, const FVector& Dir)
{
	FActorSpawnParameters Params;
	Params.Owner = this;
	Params.Instigator = this;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	GetWorld()->SpawnActor<AArenaRocket>(Origin + Dir * QU(10.f), Dir.Rotation(), Params);
}

// ---------------------------------------------------------------------------
// Damage, pickups, death
// ---------------------------------------------------------------------------

void AArenaCharacter::ApplyArenaDamage(float Damage, AController* InstigatorController, const FVector& Knockback, EArenaWeapon Weapon)
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
	Armor -= Absorbed;
	Health -= Points - Absorbed;

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
		GS->MulticastExplosion(GetActorLocation(), FLinearColor(0.6f, 0.02f, 0.02f), QU(60.f));
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
