#include "ArenaPickup.h"
#include "ArenaAudio.h"
#include "ArenaCharacter.h"
#include "ArenaVisuals.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"

AArenaPickup::AArenaPickup()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;
	bAlwaysRelevant = true;

	Trigger = CreateDefaultSubobject<USphereComponent>(TEXT("Trigger"));
	Trigger->InitSphereRadius(60.f);
	Trigger->SetCollisionObjectType(ECC_WorldDynamic);
	Trigger->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Trigger->SetCollisionResponseToAllChannels(ECR_Ignore);
	Trigger->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	Trigger->SetGenerateOverlapEvents(true);
	RootComponent = Trigger;

	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	Mesh->SetupAttachment(Trigger);
	ArenaVisuals::SetupCosmeticMesh(Mesh, nullptr);

	// Items float Hover (50 cm) above the floor; the pedestal sits on it.
	Base = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Base"));
	Base->SetupAttachment(Trigger);
	ArenaVisuals::SetupCosmeticMesh(Base, nullptr);
	Base->SetRelativeLocation(FVector(0.f, 0.f, -50.f));
}

void AArenaPickup::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AArenaPickup, Type);
	DOREPLIFETIME(AArenaPickup, bAvailable);
}

void AArenaPickup::InitPickup(EArenaPickupType InType)
{
	Type = InType;
	OnRep_Type();
}

void AArenaPickup::BeginPlay()
{
	Super::BeginPlay();
	OnRep_Type();
	if (HasAuthority())
	{
		Trigger->OnComponentBeginOverlap.AddDynamic(this, &AArenaPickup::OnOverlap);
	}
}

void AArenaPickup::OnRep_Type()
{
	const TCHAR* Model = TEXT("SM_Ammo");
	float Scale = 1.3f;
	FLinearColor Accent = FLinearColor::White;
	FLinearColor Glow = FLinearColor::White;
	switch (Type)
	{
	case EArenaPickupType::Health:     Model = TEXT("SM_Health");     Accent = FLinearColor(0.15f, 0.7f, 0.15f); Glow = FLinearColor(0.8f, 1.f, 0.8f); break;
	case EArenaPickupType::MegaHealth: Model = TEXT("SM_MegaHealth"); Accent = FLinearColor(0.8f, 0.8f, 0.9f);  Glow = FLinearColor(0.2f, 0.4f, 1.f); break;
	case EArenaPickupType::Armor:      Model = TEXT("SM_Armor");      Accent = FLinearColor(0.9f, 0.7f, 0.1f);  Glow = FLinearColor(1.f, 0.85f, 0.2f); break;
	case EArenaPickupType::HeavyArmor: Model = TEXT("SM_Armor");      Accent = FLinearColor(0.8f, 0.08f, 0.06f); Glow = FLinearColor(1.f, 0.2f, 0.1f); Scale = 1.6f; break;
	case EArenaPickupType::Ammo:       Model = TEXT("SM_Ammo");       Accent = FLinearColor(0.7f, 0.55f, 0.25f); Glow = FLinearColor(1.f, 0.8f, 0.3f); break;
	default:
		break;
	}

	UStaticMesh* Shape = ArenaVisuals::ArtMesh(Model);
	EArenaWeapon Weapon;
	if (GetPickupWeapon(Type, Weapon))
	{
		Shape = ArenaVisuals::WeaponMesh(Weapon);
		Accent = Glow = GetWeaponInfo(Weapon).Color;
		Scale = 1.1f;
	}
	Mesh->EmptyOverrideMaterials();
	Mesh->SetStaticMesh(Shape);
	Mesh->SetRelativeScale3D(FVector(Scale));
	ArenaVisuals::SetPropColors(Mesh, Accent, Glow);
	SpinPivot = Shape ? Shape->GetBounds().Origin * Scale : FVector::ZeroVector;

	Base->SetStaticMesh(ArenaVisuals::ArtMesh(TEXT("SM_ItemBase")));
	ArenaVisuals::SetPropColors(Base, Glow, Glow);
	ArenaVisuals::SetGlowParam(Base, TEXT("Emissive"), bAvailable ? 10.f : 0.5f);
}

void AArenaPickup::OnRep_Available()
{
	Mesh->SetVisibility(bAvailable);
	ArenaVisuals::SetGlowParam(Base, TEXT("Emissive"), bAvailable ? 10.f : 0.5f);

	// Skip the initial replication when joining a game with items already taken.
	if (!bAvailable && GetGameTimeSinceCreation() > 1.f)
	{
		EArenaWeapon Weapon;
		const bool bWeapon = GetPickupWeapon(Type, Weapon);
		UArenaAudio::PlayAt(this, bWeapon ? EArenaSound::WeaponPickup : EArenaSound::Pickup, GetActorLocation());
	}
}

void AArenaPickup::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	SpinTime += DeltaSeconds;
	const FRotator Spin(0.f, SpinTime * 90.f, 0.f);
	Mesh->SetRelativeRotation(Spin);
	Mesh->SetRelativeLocation(FVector(0.f, 0.f, FMath::Sin(SpinTime * 2.f) * 8.f) - Spin.RotateVector(SpinPivot));
}

void AArenaPickup::OnOverlap(UPrimitiveComponent* OverlappedComp, AActor* Other, UPrimitiveComponent* OtherComp, int32 BodyIndex, bool bFromSweep, const FHitResult& Sweep)
{
	AArenaCharacter* Character = Cast<AArenaCharacter>(Other);
	if (!bAvailable || !Character || Character->IsDead() || !TryGive(Character))
	{
		return;
	}
	bAvailable = false;
	OnRep_Available();
	GetWorldTimerManager().SetTimer(RespawnTimer, this, &AArenaPickup::Respawn, GetRespawnTime());
}

void AArenaPickup::Respawn()
{
	bAvailable = true;
	OnRep_Available();

	// Someone may be standing on it already.
	TArray<AActor*> Overlapping;
	Trigger->GetOverlappingActors(Overlapping, AArenaCharacter::StaticClass());
	for (AActor* Actor : Overlapping)
	{
		OnOverlap(Trigger, Actor, nullptr, 0, false, FHitResult());
		if (!bAvailable)
		{
			break;
		}
	}
}

bool AArenaPickup::TryGive(AArenaCharacter* Character) const
{
	switch (Type)
	{
	case EArenaPickupType::Health:         return Character->GiveHealth(25, 100);
	case EArenaPickupType::MegaHealth:     return Character->GiveHealth(100, AArenaCharacter::MaxHealth);
	case EArenaPickupType::Armor:          return Character->GiveArmor(50);
	case EArenaPickupType::HeavyArmor:     return Character->GiveArmor(100);
	case EArenaPickupType::Ammo:
	{
		bool bGave = false;
		for (int32 i = 0; i < ArenaWeaponCount; ++i)
		{
			const EArenaWeapon Weapon = static_cast<EArenaWeapon>(i);
			if (Character->HasWeapon(Weapon))
			{
				bGave |= Character->GiveWeapon(Weapon, GetWeaponInfo(Weapon).PickupAmmo);
			}
		}
		return bGave;
	}
	default:
	{
		EArenaWeapon Weapon;
		return GetPickupWeapon(Type, Weapon) && Character->GiveWeapon(Weapon, GetWeaponInfo(Weapon).StartAmmo);
	}
	}
}

float AArenaPickup::GetRespawnTime() const
{
	switch (Type)
	{
	case EArenaPickupType::MegaHealth:     return 35.f;
	case EArenaPickupType::Health:         return 35.f;
	case EArenaPickupType::Armor:
	case EArenaPickupType::HeavyArmor:     return 25.f;
	case EArenaPickupType::Ammo:           return 40.f;
	default:                               return 5.f; // Weapons
	}
}
