#include "ArenaBotController.h"
#include "ArenaBotNav.h"
#include "ArenaCharacter.h"
#include "ArenaGameMode.h"
#include "ArenaGameState.h"
#include "ArenaMap.h"
#include "ArenaPickup.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerState.h"

namespace
{
	struct FBotSkill
	{
		float ReactionTime;   // Seconds from first seeing an enemy to firing.
		float AimErrorDeg;    // Random aim offset, re-rolled a few times a second.
		float TurnRate;       // Degrees per second.
		float FireConeDeg;    // Fires when aim is within this of its (erroneous) target.
	};

	const FBotSkill& GetSkillProfile(int32 Skill)
	{
		static const FBotSkill Skills[] =
		{
			{ 0.65f, 7.0f,  200.f, 10.f },
			{ 0.45f, 4.5f,  320.f,  8.f },
			{ 0.30f, 2.8f,  480.f,  6.f },
			{ 0.20f, 1.6f,  700.f,  5.f },
			{ 0.12f, 0.8f, 1000.f,  4.f },
		};
		return Skills[FMath::Clamp(Skill, 1, 5) - 1];
	}

	float ProjectileSpeed(EArenaWeapon Weapon)
	{
		switch (Weapon)
		{
		case EArenaWeapon::GrenadeLauncher: return QU(700.f);
		case EArenaWeapon::RocketLauncher:  return QU(900.f);
		case EArenaWeapon::PlasmaGun:       return QU(2000.f);
		default:                            return 0.f;
		}
	}

	/** Distance the bot tries to keep from its target with each weapon. */
	float PreferredRange(EArenaWeapon Weapon)
	{
		switch (Weapon)
		{
		case EArenaWeapon::Gauntlet:        return 50.f;
		case EArenaWeapon::Shotgun:         return 350.f;
		case EArenaWeapon::LightningGun:    return 600.f;
		case EArenaWeapon::PlasmaGun:       return 600.f;
		case EArenaWeapon::GrenadeLauncher: return 700.f;
		case EArenaWeapon::RocketLauncher:  return 900.f;
		case EArenaWeapon::Railgun:         return 1800.f;
		default:                            return 1000.f;
		}
	}

	/** How much the bot likes each weapon at this distance (0 = don't use). */
	float WeaponScore(EArenaWeapon Weapon, float Dist)
	{
		switch (Weapon)
		{
		case EArenaWeapon::Gauntlet:        return Dist < 150.f ? 10.f : 0.f;
		case EArenaWeapon::MachineGun:      return 2.f;
		case EArenaWeapon::Shotgun:         return Dist < 500.f ? 7.f : Dist < 900.f ? 4.f : 1.f;
		case EArenaWeapon::GrenadeLauncher: return Dist > 300.f && Dist < 900.f ? 4.f : 1.f;
		case EArenaWeapon::RocketLauncher:  return Dist < 300.f ? 2.f : Dist < 1600.f ? 8.f : 5.f;
		case EArenaWeapon::LightningGun:    return Dist < QU(740.f) ? 8.f : 0.f;
		case EArenaWeapon::Railgun:         return Dist > 1200.f ? 9.f : 5.f;
		case EArenaWeapon::PlasmaGun:       return Dist < 1500.f ? 6.f : 3.f;
		default:                            return 0.f;
		}
	}

	FCollisionObjectQueryParams StaticOnly()
	{
		return FCollisionObjectQueryParams(ECC_WorldStatic);
	}
}

AArenaBotController::AArenaBotController()
{
	bWantsPlayerState = true;
	bSetControlRotationFromPawnOrientation = false;
	PrimaryActorTick.bCanEverTick = true;
}

void AArenaBotController::InitBot(int32 InSkill)
{
	Skill = FMath::Clamp(InSkill, 1, 5);
	if (PlayerState)
	{
		PlayerState->SetIsABot(true);
	}
}

AArenaCharacter* AArenaBotController::GetBot() const
{
	return Cast<AArenaCharacter>(GetPawn());
}

const FArenaBotNav* AArenaBotController::GetNav() const
{
	AArenaGameMode* GM = GetWorld()->GetAuthGameMode<AArenaGameMode>();
	return GM ? GM->GetBotNav() : nullptr;
}

void AArenaBotController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);
	SetControlRotation(FRotator(0.f, InPawn->GetActorRotation().Yaw, 0.f));
	Enemy.Reset();
	Path.Reset();
	NextRepathTime = 0.f;
	LastProgressLocation = InPawn->GetActorLocation();
	LastProgressTime = GetWorld()->GetTimeSeconds();
}

void AArenaBotController::OnUnPossess()
{
	Super::OnUnPossess();
	DeathTime = GetWorld()->GetTimeSeconds();
	Enemy.Reset();
}

void AArenaBotController::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	AArenaCharacter* Bot = GetBot();
	const AArenaGameMode* GM = GetWorld()->GetAuthGameMode<AArenaGameMode>();
	if (!Bot || Bot->IsDead() || !GM || !GM->IsMatchInProgress())
	{
		return;
	}

	const float Now = GetWorld()->GetTimeSeconds();
	UpdateEnemy(Bot, Now);

	if (Enemy.IsValid() && EnemyLastSeenTime == Now)
	{
		if (Now >= NextWeaponCheck)
		{
			NextWeaponCheck = Now + 0.5f;
			ChooseWeapon(Bot, FVector::Dist(Bot->GetActorLocation(), Enemy->GetActorLocation()));
		}
		AimAndFire(Bot, DeltaSeconds, Now);
		MoveInCombat(Bot, Now);
	}
	else
	{
		MoveAlongPath(Bot, Now);
		// Look where we're going, or toward where the enemy was last seen.
		const FVector LookAt = Now - EnemyLastSeenTime < 3.f ? EnemyLastSeenLocation
			: (Path.IsValidIndex(PathIndex) ? Path[PathIndex] + FVector(0.f, 0.f, 60.f) : Bot->GetActorLocation() + Bot->GetActorForwardVector() * 500.f);
		FRotator Look = (LookAt - Bot->GetCamera()->GetComponentLocation()).Rotation();
		Look.Pitch = FMath::Clamp(Look.Pitch, -30.f, 30.f);
		TurnToward(Bot, Look, DeltaSeconds);
	}
}

// ---------------------------------------------------------------------------
// Targeting and shooting
// ---------------------------------------------------------------------------

bool AArenaBotController::CanSee(const AArenaCharacter* Bot, const AArenaCharacter* Target) const
{
	const FVector Eye = Bot->GetCamera()->GetComponentLocation();
	const FVector Chest = Target->GetActorLocation() + FVector(0.f, 0.f, 30.f);
	if (FVector::DistSquared(Eye, Chest) > FMath::Square(7000.f))
	{
		return false;
	}
	const FCollisionQueryParams Params(SCENE_QUERY_STAT(ArenaBotSight), false, Bot);
	return !GetWorld()->LineTraceTestByObjectType(Eye, Chest, StaticOnly(), Params);
}

void AArenaBotController::UpdateEnemy(AArenaCharacter* Bot, float Now)
{
	if (Enemy.IsValid() && !Enemy->IsDead() && CanSee(Bot, Enemy.Get()))
	{
		EnemyLastSeenTime = Now;
		EnemyLastSeenLocation = Enemy->GetActorLocation();
		return;
	}

	// Current enemy gone or hidden: look for the nearest visible one.
	AArenaCharacter* Best = nullptr;
	float BestDist = TNumericLimits<float>::Max();
	for (TActorIterator<AArenaCharacter> It(GetWorld()); It; ++It)
	{
		AArenaCharacter* Other = *It;
		if (Other == Bot || Other->IsDead())
		{
			continue;
		}
		const float Dist = FVector::DistSquared(Other->GetActorLocation(), Bot->GetActorLocation());
		if (Dist < BestDist && CanSee(Bot, Other))
		{
			Best = Other;
			BestDist = Dist;
		}
	}
	if (Best)
	{
		if (Best != Enemy.Get() || Now - EnemyLastSeenTime > 1.f)
		{
			EnemyAcquiredTime = Now; // Reaction time starts over for a new sighting.
		}
		Enemy = Best;
		EnemyLastSeenTime = Now;
		EnemyLastSeenLocation = Best->GetActorLocation();
	}
	else if (Enemy.IsValid() && Enemy->IsDead())
	{
		Enemy.Reset();
	}
}

void AArenaBotController::ChooseWeapon(AArenaCharacter* Bot, float Distance)
{
	EArenaWeapon Best = Bot->GetCurrentWeapon();
	float BestScore = Bot->CanFire(Best) ? WeaponScore(Best, Distance) + 0.5f : -1.f; // Slight preference to not switch.
	for (int32 i = 0; i < ArenaWeaponCount; ++i)
	{
		const EArenaWeapon Weapon = static_cast<EArenaWeapon>(i);
		const float Score = Bot->CanFire(Weapon) ? WeaponScore(Weapon, Distance) : -1.f;
		if (Score > BestScore)
		{
			Best = Weapon;
			BestScore = Score;
		}
	}
	Bot->EquipWeapon(Best);
}

void AArenaBotController::TurnToward(AArenaCharacter* Bot, const FRotator& Desired, float DeltaSeconds)
{
	const FRotator Current = GetControlRotation();
	const FRotator Delta = (Desired - Current).GetNormalized();
	const float MaxStep = GetSkillProfile(Skill).TurnRate * DeltaSeconds;
	FRotator New = Current;
	New.Yaw += FMath::Clamp(Delta.Yaw, -MaxStep, MaxStep);
	New.Pitch = FMath::Clamp(FRotator::NormalizeAxis(Current.Pitch + FMath::Clamp(Delta.Pitch, -MaxStep, MaxStep)), -89.f, 89.f);
	New.Roll = 0.f;
	SetControlRotation(New);
	Bot->FaceRotation(New, DeltaSeconds);
}

void AArenaBotController::AimAndFire(AArenaCharacter* Bot, float DeltaSeconds, float Now)
{
	const AArenaCharacter* Target = Enemy.Get();
	const EArenaWeapon Weapon = Bot->GetCurrentWeapon();
	const FVector Eye = Bot->GetCamera()->GetComponentLocation();
	FVector AimPoint = Target->GetActorLocation() + FVector(0.f, 0.f, 20.f);
	const float Dist = FVector::Dist(Eye, AimPoint);

	// Lead projectiles; aim rockets at the feet of grounded targets for splash.
	if (const float Speed = ProjectileSpeed(Weapon); Speed > 0.f)
	{
		const FVector Velocity = Target->GetVelocity();
		AimPoint += FVector(Velocity.X, Velocity.Y, Velocity.Z * 0.3f) * (Dist / Speed);
		if (Weapon == EArenaWeapon::RocketLauncher && Skill >= 3 && Target->GetCharacterMovement()->IsMovingOnGround())
		{
			AimPoint.Z = Target->GetActorLocation().Z - Target->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 10.f;
		}
		if (Weapon == EArenaWeapon::GrenadeLauncher)
		{
			AimPoint.Z += Dist * 0.12f; // Rough arc compensation.
		}
	}

	if (Now >= NextAimErrorTime)
	{
		NextAimErrorTime = Now + FMath::FRandRange(0.25f, 0.45f);
		const float Error = GetSkillProfile(Skill).AimErrorDeg;
		AimError = FRotator(FMath::FRandRange(-Error, Error) * 0.6f, FMath::FRandRange(-Error, Error), 0.f);
	}
	const FRotator Desired = (AimPoint - Eye).Rotation() + AimError;
	TurnToward(Bot, Desired, DeltaSeconds);

	// Fire once reacted and roughly on target, and only if the weapon can reach.
	const FArenaWeaponInfo& Info = GetWeaponInfo(Weapon);
	const bool bInRange = Info.bProjectile || Dist < Info.Range;
	const float OffAngle = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector::DotProduct(GetControlRotation().Vector(), Desired.Vector()), -1.f, 1.f)));
	if (bInRange && Now - EnemyAcquiredTime >= GetSkillProfile(Skill).ReactionTime && OffAngle <= GetSkillProfile(Skill).FireConeDeg)
	{
		Bot->PullTrigger();
	}
}

// ---------------------------------------------------------------------------
// Movement
// ---------------------------------------------------------------------------

bool AArenaBotController::HasGroundAhead(const AArenaCharacter* Bot, const FVector& Direction, float Distance) const
{
	// At running speed ground friction needs about a quarter of the speed (in cm) to
	// stop, so look further ahead the faster we're going.
	if (Distance < 0.f)
	{
		Distance = 150.f + Bot->GetVelocity().Size2D() * 0.4f;
	}
	const float HalfHeight = Bot->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	const FVector Ahead = Bot->GetActorLocation() + Direction.GetSafeNormal2D() * Distance;
	const FCollisionQueryParams Params(SCENE_QUERY_STAT(ArenaBotEdge), false, Bot);
	return GetWorld()->LineTraceTestByObjectType(Ahead, Ahead - FVector(0.f, 0.f, HalfHeight + 500.f), StaticOnly(), Params);
}

void AArenaBotController::Steer(AArenaCharacter* Bot, FVector Direction)
{
	Direction.Z = 0.f;
	// No steering in the air: pulling toward the last node would cut jump pad arcs short.
	if (Direction.IsNearlyZero() || Bot->GetCharacterMovement()->IsFalling())
	{
		return;
	}
	Bot->AddMovementInput(Direction.GetSafeNormal(), 1.f);
}

void AArenaBotController::MoveInCombat(AArenaCharacter* Bot, float Now)
{
	const FVector ToEnemy = Enemy->GetActorLocation() - Bot->GetActorLocation();
	const float Dist = ToEnemy.Size2D();
	const float Preferred = PreferredRange(Bot->GetCurrentWeapon());
	const FVector Forward = ToEnemy.GetSafeNormal2D();
	const FVector Right(-Forward.Y, Forward.X, 0.f);

	const FVector Velocity2D(Bot->GetVelocity().X, Bot->GetVelocity().Y, 0.f);

	// Sliding toward an edge: brake before anything else.
	if (Velocity2D.SizeSquared() > FMath::Square(200.f) && !HasGroundAhead(Bot, Velocity2D))
	{
		Steer(Bot, -Velocity2D);
		return;
	}

	if (Now >= NextStrafeSwitch)
	{
		NextStrafeSwitch = Now + FMath::FRandRange(0.6f, 1.6f);
		StrafeSign = FMath::RandBool() ? 1.f : -1.f;
		// Better bots hop while dodging, but only with plenty of ground to land on.
		if (Skill >= 3 && FMath::FRand() < 0.35f && Bot->GetCharacterMovement()->IsMovingOnGround()
			&& HasGroundAhead(Bot, Velocity2D.IsNearlyZero() ? Bot->GetActorForwardVector() : Velocity2D, 150.f + Velocity2D.Size() * 0.8f))
		{
			Bot->Jump();
		}
	}

	// Keep the preferred range, or keep heading for items if under-armed or hurt.
	FVector Base = Forward * (Dist > Preferred * 1.3f ? 1.f : Dist < Preferred * 0.7f ? -1.f : 0.f);
	float StrafeAmount = 1.f;
	if (NeedsItems(Bot))
	{
		Base = UpdatePath(Bot, Now).GetSafeNormal2D();
		StrafeAmount = 0.5f;
	}
	// Try the full move, then pure strafe either way; never walk off an edge.
	const FVector Candidates[] =
	{
		Base + Right * StrafeSign * StrafeAmount,
		Right * StrafeSign,
		Base - Right * StrafeSign * StrafeAmount,
		-Right * StrafeSign,
	};
	for (const FVector& Candidate : Candidates)
	{
		if (!Candidate.IsNearlyZero() && HasGroundAhead(Bot, Candidate))
		{
			Steer(Bot, Candidate);
			return;
		}
	}
}

float AArenaBotController::ScorePickup(const AArenaCharacter* Bot, const AArenaPickup* Pickup) const
{
	const int32 Health = Bot->GetHealth();
	const int32 Armor = Bot->GetArmor();
	switch (Pickup->GetPickupType())
	{
	case EArenaPickupType::Health:     return Health < 100 ? (100 - Health) / 25.f : 0.f;
	case EArenaPickupType::MegaHealth: return Health < 150 ? 3.f : 0.5f;
	case EArenaPickupType::Armor:      return Armor < 100 ? 1.5f : 0.2f;
	case EArenaPickupType::HeavyArmor: return Armor < 150 ? 2.5f : 0.3f;
	case EArenaPickupType::Ammo:
		for (int32 i = 0; i < ArenaWeaponCount; ++i)
		{
			const EArenaWeapon Weapon = static_cast<EArenaWeapon>(i);
			const FArenaWeaponInfo& Info = GetWeaponInfo(Weapon);
			if (Bot->HasWeapon(Weapon) && Info.UsesAmmo() && Bot->GetAmmo(Weapon) < Info.MaxAmmo / 2)
			{
				return 1.f;
			}
		}
		return 0.f;
	default:
	{
		EArenaWeapon Weapon;
		if (!GetPickupWeapon(Pickup->GetPickupType(), Weapon))
		{
			return 0.f;
		}
		if (!Bot->HasWeapon(Weapon))
		{
			const bool bPower = Weapon == EArenaWeapon::RocketLauncher || Weapon == EArenaWeapon::Railgun || Weapon == EArenaWeapon::LightningGun;
			return bPower ? 3.f : 2.f;
		}
		return Bot->GetAmmo(Weapon) < GetWeaponInfo(Weapon).MaxAmmo / 2 ? 0.8f : 0.f;
	}
	}
}

void AArenaBotController::PickGoal(AArenaCharacter* Bot)
{
	const FArenaBotNav* Nav = GetNav();
	Path.Reset();
	PathIndex = 0;
	if (!Nav || !Nav->IsBuilt())
	{
		return;
	}

	const float HalfHeight = Bot->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	const FVector Feet = Bot->GetActorLocation() - FVector(0.f, 0.f, HalfHeight);
	const float Now = GetWorld()->GetTimeSeconds();

	// Hunt a recently seen enemy if healthy; otherwise the most useful item nearby.
	FVector Goal = FVector::ZeroVector;
	bool bHasGoal = false;
	if (Now - EnemyLastSeenTime < 4.f && !NeedsItems(Bot))
	{
		Goal = EnemyLastSeenLocation - FVector(0.f, 0.f, HalfHeight);
		bHasGoal = true;
	}
	else
	{
		float BestScore = 0.f;
		for (TActorIterator<AArenaPickup> It(GetWorld()); It; ++It)
		{
			if (!It->IsAvailable())
			{
				continue;
			}
			const float Score = ScorePickup(Bot, *It) / (FVector::Dist(It->GetActorLocation(), Feet) + 600.f);
			if (Score > BestScore)
			{
				BestScore = Score;
				Goal = It->GetActorLocation() - FVector(0.f, 0.f, 50.f);
				bHasGoal = true;
			}
		}
	}

	if (bHasGoal)
	{
		Path = Nav->FindPath(Feet, Goal);
	}
	// Nothing worth getting (or unreachable): roam to a random spawn point.
	const AArenaGameState* GS = GetWorld()->GetGameState<AArenaGameState>();
	const TArray<FTransform>& Spawns = ArenaMap::Get(GS ? GS->MapId : NAME_None).Spawns;
	for (int32 Attempt = 0; Path.Num() == 0 && Attempt < 3 && Spawns.Num() > 0; ++Attempt)
	{
		const FVector Roam = Spawns[FMath::RandRange(0, Spawns.Num() - 1)].GetLocation() - FVector(0.f, 0.f, HalfHeight + 10.f);
		Path = Nav->FindPath(Feet, Roam);
	}
	// Skip the first node if we're already standing on it.
	if (Path.Num() > 1 && FVector::Dist2D(Path[0], Feet) < 150.f)
	{
		PathIndex = 1;
	}
}

bool AArenaBotController::NeedsItems(const AArenaCharacter* Bot) const
{
	if (Bot->GetHealth() < 60)
	{
		return true;
	}
	for (int32 i = static_cast<int32>(EArenaWeapon::Shotgun); i < ArenaWeaponCount; ++i)
	{
		if (Bot->CanFire(static_cast<EArenaWeapon>(i)))
		{
			return false;
		}
	}
	return true;
}

FVector AArenaBotController::UpdatePath(AArenaCharacter* Bot, float Now)
{
	if (Now >= NextRepathTime || !Path.IsValidIndex(PathIndex))
	{
		PickGoal(Bot);
		NextRepathTime = Now + 4.f;
	}
	if (!Path.IsValidIndex(PathIndex))
	{
		return FVector::ZeroVector;
	}

	const float HalfHeight = Bot->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	const FVector Feet = Bot->GetActorLocation() - FVector(0.f, 0.f, HalfHeight);

	// Reached the node, or already past it (closer to the next node than this one
	// is, e.g. after a jump pad or a knockback): move on.
	while (Path.IsValidIndex(PathIndex))
	{
		const FVector ToNode = Path[PathIndex] - Feet;
		const bool bReached = ToNode.Size2D() < 90.f && FMath::Abs(ToNode.Z) < 150.f;
		const bool bPast = Path.IsValidIndex(PathIndex + 1)
			&& FVector::Dist(Feet, Path[PathIndex + 1]) < FVector::Dist(Path[PathIndex], Path[PathIndex + 1]);
		if (!bReached && !bPast)
		{
			break;
		}
		++PathIndex;
	}
	if (!Path.IsValidIndex(PathIndex))
	{
		NextRepathTime = Now; // Arrived: pick a new goal next tick.
		return FVector::ZeroVector;
	}

	// Unstick: if we haven't moved in a while, jump and repath.
	if (FVector::DistSquared(Bot->GetActorLocation(), LastProgressLocation) > FMath::Square(80.f))
	{
		LastProgressLocation = Bot->GetActorLocation();
		LastProgressTime = Now;
	}
	else if (Now - LastProgressTime > 1.2f)
	{
		Bot->Jump();
		NextRepathTime = Now + 0.3f;
		LastProgressTime = Now;
	}

	return Path[PathIndex] - Feet;
}

void AArenaBotController::MoveAlongPath(AArenaCharacter* Bot, float Now)
{
	const FVector ToNode = UpdatePath(Bot, Now);
	if (ToNode.IsNearlyZero())
	{
		return;
	}
	Steer(Bot, ToNode);

	// Skilled bots hop along long straight stretches.
	if (Skill >= 4 && ToNode.Size2D() > 600.f && Bot->GetCharacterMovement()->IsMovingOnGround() && FMath::Abs(ToNode.Z) < 50.f)
	{
		Bot->Jump();
	}
}
