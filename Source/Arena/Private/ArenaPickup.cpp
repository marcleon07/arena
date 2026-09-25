#include "ArenaPickup.h"
#include "ArenaAudio.h"
#include "ArenaCharacter.h"
#include "ArenaVisuals.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
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
	UStaticMesh* Shape = ArenaVisuals::Cube();
	FVector Scale(0.4f);
	FLinearColor Color = FLinearColor::White;
	switch (Type)
	{
	case EArenaPickupType::Health:         Shape = ArenaVisuals::Sphere(); Scale = FVector(0.45f); Color = FLinearColor(0.2f, 0.9f, 0.2f); break;
	case EArenaPickupType::MegaHealth:     Shape = ArenaVisuals::Sphere(); Scale = FVector(0.8f);  Color = FLinearColor(0.2f, 0.4f, 1.0f); break;
	case EArenaPickupType::Armor:          Scale = FVector(0.5f, 0.5f, 0.6f);                      Color = FLinearColor(1.0f, 0.85f, 0.1f); break;
	case EArenaPickupType::HeavyArmor:     Scale = FVector(0.7f, 0.7f, 0.8f);                      Color = FLinearColor(1.0f, 0.1f, 0.1f); break;
	case EArenaPickupType::RocketLauncher: Scale = FVector(0.9f, 0.2f, 0.2f); Color = GetWeaponInfo(EArenaWeapon::RocketLauncher).Color; break;
	case EArenaPickupType::Railgun:        Scale = FVector(1.1f, 0.12f, 0.15f); Color = GetWeaponInfo(EArenaWeapon::Railgun).Color; break;
	case EArenaPickupType::Ammo:           Scale = FVector(0.3f);                                  Color = FLinearColor(0.7f, 0.6f, 0.3f); break;
	}
	Mesh->SetStaticMesh(Shape);
	Mesh->SetRelativeScale3D(Scale);
	ArenaVisuals::SetColor(Mesh, Color);
}

void AArenaPickup::OnRep_Available()
{
	Mesh->SetVisibility(bAvailable);

	// Skip the initial replication when joining a game with items already taken.
	if (!bAvailable && GetGameTimeSinceCreation() > 1.f)
	{
		const bool bWeapon = Type == EArenaPickupType::RocketLauncher || Type == EArenaPickupType::Railgun;
		UArenaAudio::PlayAt(this, bWeapon ? EArenaSound::WeaponPickup : EArenaSound::Pickup, GetActorLocation());
	}
}

void AArenaPickup::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	SpinTime += DeltaSeconds;
	Mesh->SetRelativeRotation(FRotator(0.f, SpinTime * 90.f, 0.f));
	Mesh->SetRelativeLocation(FVector(0.f, 0.f, FMath::Sin(SpinTime * 2.f) * 8.f));
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
	case EArenaPickupType::RocketLauncher: return Character->GiveWeapon(EArenaWeapon::RocketLauncher, GetWeaponInfo(EArenaWeapon::RocketLauncher).StartAmmo);
	case EArenaPickupType::Railgun:        return Character->GiveWeapon(EArenaWeapon::Railgun, GetWeaponInfo(EArenaWeapon::Railgun).StartAmmo);
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
	}
	return false;
}

float AArenaPickup::GetRespawnTime() const
{
	switch (Type)
	{
	case EArenaPickupType::MegaHealth:     return 35.f;
	case EArenaPickupType::Health:         return 35.f;
	case EArenaPickupType::Armor:
	case EArenaPickupType::HeavyArmor:     return 25.f;
	case EArenaPickupType::RocketLauncher:
	case EArenaPickupType::Railgun:        return 5.f;
	default:                           return 40.f;
	}
}
