#pragma once

#include "CoreMinimal.h"

class UWorld;

/**
 * Waypoint graph for bots, generated at runtime by probing the level geometry.
 * The maps are built in code, so there's no baked navmesh; instead:
 *  - nodes: walkable surfaces sampled on a grid (every level, e.g. floor and bridge),
 *    plus spawn points, item spots and jump pads;
 *  - links: pairs of nodes a player can walk between (checked step by step for
 *    ground, step height, slope and walls; drops are one-way), and jump pad arcs.
 * Built once per map on the server; A* over a few hundred nodes is cheap.
 */
class ARENA_API FArenaBotNav
{
public:
	/** Probes World and builds the graph. Extra points (spawns, items) become nodes too. */
	void Build(UWorld* World, const TArray<FVector>& ExtraFeetPoints);

	bool IsBuilt() const { return Nodes.Num() > 0; }

	/** Node positions (at foot level) from Start to Goal, or empty if unreachable. */
	TArray<FVector> FindPath(const FVector& StartFeet, const FVector& GoalFeet) const;

	int32 NumNodes() const { return Nodes.Num(); }
	int32 NumLinks() const;

private:
	struct FNode
	{
		FVector Location; // Feet position on the surface.
		TArray<int32> Links;
	};

	int32 AddNode(const FVector& Location);
	int32 FindNearestNode(const FVector& Feet) const;
	bool CanWalk(UWorld* World, const FVector& From, const FVector& To) const;
	bool FindGround(UWorld* World, const FVector& Above, float MaxDrop, FVector& OutGround) const;

	TArray<FNode> Nodes;
};
