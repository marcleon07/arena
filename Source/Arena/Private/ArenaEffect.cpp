#include "ArenaEffect.h"
#include "ArenaVisuals.h"
#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"

AArenaEffect::AArenaEffect()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = false;

	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	RootComponent = Mesh;
	ArenaVisuals::SetupCosmeticMesh(Mesh, nullptr);
	Mesh->SetCastShadow(false);
}

void AArenaEffect::InitBeam(const FVector& Start, const FVector& End, const FLinearColor& Color, float Width, float Life)
{
	// The cylinder is Z-up and 100 cm long; orient Z along the beam.
	const FVector Delta = End - Start;
	const float Length = Delta.Size();
	Mesh->SetStaticMesh(ArenaVisuals::Cylinder());
	SetActorLocationAndRotation((Start + End) * 0.5f, FRotationMatrix::MakeFromZ(Delta.GetSafeNormal()).Rotator());
	StartScale = FVector(Width / 100.f, Width / 100.f, Length / 100.f);
	EndScale = FVector(Width * 0.2f / 100.f, Width * 0.2f / 100.f, StartScale.Z);
	Duration = FMath::Max(Life, 0.01f);
	SetActorScale3D(StartScale);
	Material = ArenaVisuals::SetFX(Mesh, Color, 4.f, 0.8f);
}

void AArenaEffect::InitBlast(const FVector& Location, const FLinearColor& Color, float Radius, float Life)
{
	Mesh->SetStaticMesh(ArenaVisuals::Sphere());
	SetActorLocation(Location);
	StartScale = FVector(Radius * 0.35f / 50.f);
	EndScale = FVector(Radius / 50.f);
	Duration = FMath::Max(Life, 0.01f);
	SetActorScale3D(StartScale);
	Material = ArenaVisuals::SetFX(Mesh, Color, 6.f, 1.5f);

	if (Radius >= 50.f)
	{
		Light = NewObject<UPointLightComponent>(this);
		Light->SetupAttachment(Mesh);
		Light->SetAbsolute(false, false, true);
		Light->SetCastShadows(false);
		Light->SetLightColor(Color);
		Light->SetIntensityUnits(ELightUnits::Candelas);
		Light->SetAttenuationRadius(Radius * 5.f);
		LightIntensity = Radius * 0.5f;
		Light->SetIntensity(LightIntensity);
		Light->RegisterComponent();
	}
}

void AArenaEffect::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	Age += DeltaSeconds;
	const float Alpha = FMath::Clamp(Age / Duration, 0.f, 1.f);
	SetActorScale3D(FMath::Lerp(StartScale, EndScale, FMath::InterpEaseOut(0.f, 1.f, Alpha, 2.f)));
	const float Fade = FMath::Square(1.f - Alpha);
	if (Material)
	{
		Material->SetScalarParameterValue(TEXT("Opacity"), Fade);
	}
	if (Light)
	{
		Light->SetIntensity(LightIntensity * Fade);
	}
	if (Alpha >= 1.f)
	{
		Destroy();
	}
}
