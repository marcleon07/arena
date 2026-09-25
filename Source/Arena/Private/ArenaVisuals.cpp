#include "ArenaVisuals.h"
#include "ArenaEffect.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"

namespace
{
	template <typename T>
	T* LoadCached(TWeakObjectPtr<T>& Cache, const TCHAR* Path)
	{
		if (!Cache.IsValid())
		{
			Cache = LoadObject<T>(nullptr, Path);
		}
		return Cache.Get();
	}

	UMaterialInterface* BaseMaterial()
	{
		static TWeakObjectPtr<UMaterialInterface> Cache;
		return LoadCached(Cache, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	}

	const FName ColorParam(TEXT("Color"));
}

UStaticMesh* ArenaVisuals::Cube()
{
	static TWeakObjectPtr<UStaticMesh> Cache;
	return LoadCached(Cache, TEXT("/Engine/BasicShapes/Cube.Cube"));
}

UStaticMesh* ArenaVisuals::Sphere()
{
	static TWeakObjectPtr<UStaticMesh> Cache;
	return LoadCached(Cache, TEXT("/Engine/BasicShapes/Sphere.Sphere"));
}

UStaticMesh* ArenaVisuals::Cylinder()
{
	static TWeakObjectPtr<UStaticMesh> Cache;
	return LoadCached(Cache, TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
}

void ArenaVisuals::SetupCosmeticMesh(UStaticMeshComponent* Comp, UStaticMesh* Mesh)
{
	Comp->SetStaticMesh(Mesh);
	Comp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Comp->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
	Comp->SetCanEverAffectNavigation(false);
	Comp->SetGenerateOverlapEvents(false);
}

void ArenaVisuals::SetColor(UStaticMeshComponent* Comp, const FLinearColor& Color)
{
	if (!Comp)
	{
		return;
	}
	if (UMaterialInstanceDynamic* MID = Comp->CreateDynamicMaterialInstance(0, BaseMaterial()))
	{
		MID->SetVectorParameterValue(ColorParam, Color);
	}
}

void ArenaVisuals::SpawnBeam(UWorld* World, const FVector& Start, const FVector& End, const FLinearColor& Color, float Width, float Life)
{
	if (!World || World->GetNetMode() == NM_DedicatedServer)
	{
		return;
	}
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	if (AArenaEffect* Effect = World->SpawnActor<AArenaEffect>(Start, FRotator::ZeroRotator, Params))
	{
		Effect->InitBeam(Start, End, Color, Width, Life);
	}
}

void ArenaVisuals::SpawnBlast(UWorld* World, const FVector& Location, const FLinearColor& Color, float Radius, float Life)
{
	if (!World || World->GetNetMode() == NM_DedicatedServer)
	{
		return;
	}
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	if (AArenaEffect* Effect = World->SpawnActor<AArenaEffect>(Location, FRotator::ZeroRotator, Params))
	{
		Effect->InitBlast(Location, Color, Radius, Life);
	}
}
