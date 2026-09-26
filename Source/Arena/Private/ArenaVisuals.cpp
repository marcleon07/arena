#include "ArenaVisuals.h"
#include "ArenaEffect.h"
#include "Components/MeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture.h"
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

	UMaterialInterface* LoadMaterial(const TCHAR* Name)
	{
		static TMap<FName, TWeakObjectPtr<UMaterialInterface>> Cache;
		TWeakObjectPtr<UMaterialInterface>& Entry = Cache.FindOrAdd(Name);
		return LoadCached(Entry, *FString::Printf(TEXT("/Game/Art/Materials/%s.%s"), Name, Name));
	}

	UTexture* LoadTexture(const FString& Name)
	{
		static TMap<FString, TWeakObjectPtr<UTexture>> Cache;
		TWeakObjectPtr<UTexture>& Entry = Cache.FindOrAdd(Name);
		return LoadCached(Entry, *FString::Printf(TEXT("/Game/Art/Textures/%s.%s"), *Name, *Name));
	}

	struct FSurfaceTexture
	{
		const TCHAR* Name;
		/** Centimetres one repeat of the texture covers. */
		float Size;
	};

	const FSurfaceTexture& GetSurfaceTexture(EArenaSurface Surface)
	{
		static const FSurfaceTexture Textures[] =
		{
			{ TEXT("Tiles"), 256.f },
			{ TEXT("Panels"), 400.f },
			{ TEXT("Plate"), 192.f },
			{ TEXT("Concrete"), 320.f },
			{ TEXT("Bricks"), 320.f },
			{ TEXT("Sand"), 512.f },
		};
		return Textures[FMath::Clamp(static_cast<int32>(Surface), 0, static_cast<int32>(UE_ARRAY_COUNT(Textures)) - 1)];
	}

	const FName ColorParam(TEXT("Color"));
	const FName AccentSlot(TEXT("Accent"));
	const FName GlowSlot(TEXT("Glow"));

	UMaterialInstanceDynamic* SlotMID(UMeshComponent* Comp, FName Slot)
	{
		const int32 Index = Comp ? Comp->GetMaterialIndex(Slot) : INDEX_NONE;
		return Index == INDEX_NONE ? nullptr : Comp->CreateAndSetMaterialInstanceDynamic(Index);
	}
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

UStaticMesh* ArenaVisuals::ArtMesh(const TCHAR* Name)
{
	static TMap<FName, TWeakObjectPtr<UStaticMesh>> Cache;
	TWeakObjectPtr<UStaticMesh>& Entry = Cache.FindOrAdd(Name);
	return LoadCached(Entry, *FString::Printf(TEXT("/Game/Art/Meshes/%s.%s"), Name, Name));
}

UStaticMesh* ArenaVisuals::WeaponMesh(EArenaWeapon Weapon)
{
	switch (Weapon)
	{
	case EArenaWeapon::Gauntlet:        return ArtMesh(TEXT("SM_Gauntlet"));
	case EArenaWeapon::Shotgun:         return ArtMesh(TEXT("SM_Shotgun"));
	case EArenaWeapon::GrenadeLauncher: return ArtMesh(TEXT("SM_GrenadeLauncher"));
	case EArenaWeapon::RocketLauncher:  return ArtMesh(TEXT("SM_RocketLauncher"));
	case EArenaWeapon::LightningGun:    return ArtMesh(TEXT("SM_LightningGun"));
	case EArenaWeapon::Railgun:         return ArtMesh(TEXT("SM_Railgun"));
	case EArenaWeapon::PlasmaGun:       return ArtMesh(TEXT("SM_PlasmaGun"));
	default:                            return ArtMesh(TEXT("SM_MachineGun"));
	}
}

UStaticMesh* ArenaVisuals::WeaponSpinMesh(EArenaWeapon Weapon)
{
	switch (Weapon)
	{
	case EArenaWeapon::Gauntlet:     return ArtMesh(TEXT("SM_Gauntlet_Blade"));
	case EArenaWeapon::MachineGun:   return ArtMesh(TEXT("SM_MachineGun_Barrels"));
	case EArenaWeapon::LightningGun: return ArtMesh(TEXT("SM_LightningGun_Coil"));
	default:                         return nullptr;
	}
}

void ArenaVisuals::SetupCosmeticMesh(UStaticMeshComponent* Comp, UStaticMesh* Mesh)
{
	Comp->SetStaticMesh(Mesh);
	Comp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Comp->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
	Comp->SetCanEverAffectNavigation(false);
	Comp->SetGenerateOverlapEvents(false);
}

void ArenaVisuals::SetPropColors(UMeshComponent* Comp, const FLinearColor& Accent, const FLinearColor& Glow)
{
	if (UMaterialInstanceDynamic* MID = SlotMID(Comp, AccentSlot))
	{
		MID->SetVectorParameterValue(ColorParam, Accent);
	}
	if (UMaterialInstanceDynamic* MID = SlotMID(Comp, GlowSlot))
	{
		MID->SetVectorParameterValue(ColorParam, Glow);
	}
}

void ArenaVisuals::SetGlowParam(UMeshComponent* Comp, FName Param, float Value)
{
	if (UMaterialInstanceDynamic* MID = SlotMID(Comp, GlowSlot))
	{
		MID->SetScalarParameterValue(Param, Value);
	}
}

UMaterialInstanceDynamic* ArenaVisuals::SetFX(UMeshComponent* Comp, const FLinearColor& Color, float Intensity, float FresnelExp)
{
	UMaterialInterface* FX = LoadMaterial(TEXT("M_ArenaFX"));
	if (!Comp || !FX)
	{
		return nullptr;
	}
	UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(FX, Comp);
	MID->SetVectorParameterValue(ColorParam, Color);
	MID->SetScalarParameterValue(TEXT("Intensity"), Intensity);
	MID->SetScalarParameterValue(TEXT("FresnelExp"), FresnelExp);
	for (int32 i = 0; i < FMath::Max(1, Comp->GetNumMaterials()); ++i)
	{
		Comp->SetMaterial(i, MID);
	}
	return MID;
}

UMaterialInterface* ArenaVisuals::SurfaceMaterial(const FArenaSurfaceStyle& Style)
{
	// Blocks keep their material alive; the cache only lets them share it.
	static TMap<uint32, TWeakObjectPtr<UMaterialInstanceDynamic>> Cache;
	const uint32 Key = HashCombine(HashCombine(GetTypeHash(Style.Top), GetTypeHash(Style.Side)), GetTypeHash(Style.Tint));
	TWeakObjectPtr<UMaterialInstanceDynamic>& Entry = Cache.FindOrAdd(Key);
	if (Entry.IsValid())
	{
		return Entry.Get();
	}
	UMaterialInterface* Base = LoadMaterial(TEXT("M_ArenaSurface"));
	if (!Base)
	{
		return nullptr;
	}
	UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(Base, GetTransientPackage());
	const FSurfaceTexture& Top = GetSurfaceTexture(Style.Top);
	const FSurfaceTexture& Side = GetSurfaceTexture(Style.Side);
	MID->SetTextureParameterValue(TEXT("TopAlbedo"), LoadTexture(FString::Printf(TEXT("T_%s_D"), Top.Name)));
	MID->SetTextureParameterValue(TEXT("TopNormal"), LoadTexture(FString::Printf(TEXT("T_%s_N"), Top.Name)));
	MID->SetTextureParameterValue(TEXT("SideAlbedo"), LoadTexture(FString::Printf(TEXT("T_%s_D"), Side.Name)));
	MID->SetTextureParameterValue(TEXT("SideNormal"), LoadTexture(FString::Printf(TEXT("T_%s_N"), Side.Name)));
	MID->SetScalarParameterValue(TEXT("TopScale"), 1.f / Top.Size);
	MID->SetScalarParameterValue(TEXT("SideScale"), 1.f / Side.Size);
	MID->SetVectorParameterValue(TEXT("Tint"), Style.Tint);
	Entry = MID;
	return MID;
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
