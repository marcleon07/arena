#pragma once

#include "CoreMinimal.h"

class UStaticMesh;
class UStaticMeshComponent;
class UWorld;

/**
 * The project ships no content assets: everything is built from the engine's
 * BasicShapes meshes, tinted through BasicShapeMaterial's "Color" parameter.
 */
namespace ArenaVisuals
{
	ARENA_API UStaticMesh* Cube();     // 100 cm, pivot at center
	ARENA_API UStaticMesh* Sphere();   // 100 cm diameter
	ARENA_API UStaticMesh* Cylinder(); // 100 cm diameter, 100 cm tall, Z-up

	/** Creates a component-less mesh setup: no collision, no nav, given mesh. */
	ARENA_API void SetupCosmeticMesh(UStaticMeshComponent* Comp, UStaticMesh* Mesh);

	ARENA_API void SetColor(UStaticMeshComponent* Comp, const FLinearColor& Color);

	/** Spawns a short-lived beam (rail trail / tracer) on this machine only. */
	ARENA_API void SpawnBeam(UWorld* World, const FVector& Start, const FVector& End, const FLinearColor& Color, float Width, float Life);

	/** Spawns a short-lived expanding sphere on this machine only. */
	ARENA_API void SpawnBlast(UWorld* World, const FVector& Location, const FLinearColor& Color, float Radius, float Life);
}
