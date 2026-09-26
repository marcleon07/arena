#include "ArenaBotNav.h"
#include "Arena.h"
#include "ArenaMap.h"
#include "Components/PrimitiveComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"

namespace
{
	constexpr float GridSpacing = 450.f;
	constexpr float LinkRange = 750.f;
	constexpr float StepLength = 50.f;
	constexpr float MaxStepUp = 55.f;     // GoldSrc step is 18 units (46 cm) plus slack for ramps.
	constexpr float MaxDrop = 450.f;      // Safe drops are one-way links.
	constexpr float KneeHeight = 45.f;
	constexpr float HeadHeight = 150.f;
	constexpr float Headroom = 190.f;
	constexpr float PadAvoidRadius = 130.f;

	FCollisionObjectQueryParams StaticOnly()
	{
		return FCollisionObjectQueryParams(ECC_WorldStatic);
	}
}

int32 FArenaBotNav::NumLinks() const
{
	int32 Count = 0;
	for (const FNode& Node : Nodes)
	{
		Count += Node.Links.Num();
	}
	return Count;
}

int32 FArenaBotNav::AddNode(const FVector& Location)
{
	for (int32 i = 0; i < Nodes.Num(); ++i)
	{
		if (FVector::DistSquared(Nodes[i].Location, Location) < FMath::Square(60.f))
		{
			return i;
		}
	}
	FNode& Node = Nodes.AddDefaulted_GetRef();
	Node.Location = Location;
	return Nodes.Num() - 1;
}

bool FArenaBotNav::FindGround(UWorld* World, const FVector& Above, float MaxDropDistance, FVector& OutGround) const
{
	FHitResult Hit;
	const FCollisionQueryParams Params(SCENE_QUERY_STAT(ArenaBotNavGround), false);
	if (!World->LineTraceSingleByObjectType(Hit, Above, Above - FVector(0.f, 0.f, MaxDropDistance), StaticOnly(), Params))
	{
		return false;
	}
	if (Hit.ImpactNormal.Z < 0.7f) // Too steep to stand on (GoldSrc walkable limit).
	{
		return false;
	}
	OutGround = Hit.ImpactPoint;
	return true;
}

void FArenaBotNav::Build(UWorld* World, const TArray<FVector>& ExtraFeetPoints)
{
	Nodes.Reset();
	if (!World)
	{
		return;
	}
	const double StartTime = FPlatformTime::Seconds();
	const FCollisionQueryParams Params(SCENE_QUERY_STAT(ArenaBotNavBuild), false);

	FBox Bounds(ForceInit);
	for (TActorIterator<AArenaBlock> It(World); It; ++It)
	{
		Bounds += It->GetComponentsBoundingBox();
	}
	if (!Bounds.IsValid)
	{
		return;
	}

	// 1. Grid sample every walkable surface, top to bottom, so bridges and the floor
	//    under them both get nodes.
	for (float X = Bounds.Min.X + GridSpacing * 0.5f; X < Bounds.Max.X; X += GridSpacing)
	{
		for (float Y = Bounds.Min.Y + GridSpacing * 0.5f; Y < Bounds.Max.Y; Y += GridSpacing)
		{
			float TopZ = Bounds.Max.Z + 100.f;
			for (int32 Level = 0; Level < 4 && TopZ > Bounds.Min.Z; ++Level)
			{
				FHitResult Hit;
				if (!World->LineTraceSingleByObjectType(Hit, FVector(X, Y, TopZ), FVector(X, Y, Bounds.Min.Z - 100.f), StaticOnly(), Params))
				{
					break;
				}
				const FVector Surface = Hit.ImpactPoint;
				const bool bHeadroom = !World->LineTraceTestByObjectType(Surface + FVector(0.f, 0.f, 10.f), Surface + FVector(0.f, 0.f, Headroom), StaticOnly(), Params);
				if (Hit.ImpactNormal.Z >= 0.7f && bHeadroom)
				{
					AddNode(Surface);
				}
				// Continue below the block that was hit.
				const UPrimitiveComponent* Comp = Hit.GetComponent();
				TopZ = (Comp ? Comp->Bounds.GetBox().Min.Z : Surface.Z) - 5.f;
			}
		}
	}

	// 2. Spawns, items and jump pads, so bots can path straight to them.
	for (const FVector& Point : ExtraFeetPoints)
	{
		FVector Ground;
		if (FindGround(World, Point + FVector(0.f, 0.f, 60.f), 300.f, Ground))
		{
			AddNode(Ground);
		}
	}

	TArray<FVector> PadCenters;
	TArray<TPair<int32, int32>> PadLinks;
	for (TActorIterator<AArenaJumpPad> It(World); It; ++It)
	{
		FVector PadGround, TargetGround;
		if (FindGround(World, It->GetActorLocation() + FVector(0.f, 0.f, 60.f), 300.f, PadGround)
			&& FindGround(World, It->GetTarget() + FVector(0.f, 0.f, 60.f), 300.f, TargetGround))
		{
			PadCenters.Add(PadGround);
			PadLinks.Emplace(AddNode(PadGround), AddNode(TargetGround));
		}
	}

	// 3. Walk links between nearby nodes. Paths that would cross a jump pad are
	//    rejected (the pad would launch the bot), except links ending on the pad itself.
	for (int32 i = 0; i < Nodes.Num(); ++i)
	{
		for (int32 j = 0; j < Nodes.Num(); ++j)
		{
			const FVector& A = Nodes[i].Location;
			const FVector& B = Nodes[j].Location;
			if (i == j || FVector::DistSquared2D(A, B) > FMath::Square(LinkRange) || FMath::Abs(A.Z - B.Z) > MaxDrop)
			{
				continue;
			}
			bool bCrossesPad = false;
			for (const FVector& Pad : PadCenters)
			{
				if (FVector::DistSquared(Pad, A) > FMath::Square(60.f) && FVector::DistSquared(Pad, B) > FMath::Square(60.f)
					&& FMath::PointDistToSegment(Pad, A, B) < PadAvoidRadius)
				{
					bCrossesPad = true;
					break;
				}
			}
			if (!bCrossesPad && CanWalk(World, A, B))
			{
				Nodes[i].Links.Add(j);
			}
		}
	}
	for (const TPair<int32, int32>& Pad : PadLinks)
	{
		Nodes[Pad.Key].Links.AddUnique(Pad.Value);
	}

	UE_LOG(LogArena, Log, TEXT("Bot nav: %d nodes, %d links, %d jump pads (%.0f ms)"),
		Nodes.Num(), NumLinks(), PadLinks.Num(), (FPlatformTime::Seconds() - StartTime) * 1000.0);
}

bool FArenaBotNav::CanWalk(UWorld* World, const FVector& From, const FVector& To) const
{
	const FCollisionQueryParams Params(SCENE_QUERY_STAT(ArenaBotNavWalk), false);
	const FVector Delta(To.X - From.X, To.Y - From.Y, 0.f);
	const int32 Steps = FMath::Max(1, FMath::CeilToInt(Delta.Size() / StepLength));

	FVector Prev = From;
	for (int32 Step = 1; Step <= Steps; ++Step)
	{
		const FVector Flat = From + Delta * (static_cast<float>(Step) / Steps);
		FVector Ground;
		if (!FindGround(World, FVector(Flat.X, Flat.Y, Prev.Z + KneeHeight + 15.f), MaxDrop + KneeHeight, Ground))
		{
			return false; // Hole, void or too steep.
		}
		if (Ground.Z - Prev.Z > MaxStepUp)
		{
			return false; // Wall or ledge too tall to step up.
		}
		if (World->LineTraceTestByObjectType(Prev + FVector(0.f, 0.f, KneeHeight), Ground + FVector(0.f, 0.f, KneeHeight), StaticOnly(), Params)
			|| World->LineTraceTestByObjectType(Prev + FVector(0.f, 0.f, HeadHeight), Ground + FVector(0.f, 0.f, HeadHeight), StaticOnly(), Params))
		{
			return false; // Something in the way.
		}
		Prev = Ground;
	}
	return FMath::Abs(Prev.Z - To.Z) < 60.f;
}

int32 FArenaBotNav::FindNearestNode(const FVector& Feet) const
{
	int32 Best = INDEX_NONE;
	float BestScore = TNumericLimits<float>::Max();
	for (int32 i = 0; i < Nodes.Num(); ++i)
	{
		// Height differences count triple: a node on the level above isn't "near".
		const FVector D = Nodes[i].Location - Feet;
		const float Score = FVector(D.X, D.Y, D.Z * 3.f).SizeSquared();
		if (Score < BestScore)
		{
			BestScore = Score;
			Best = i;
		}
	}
	return Best;
}

TArray<FVector> FArenaBotNav::FindPath(const FVector& StartFeet, const FVector& GoalFeet) const
{
	TArray<FVector> Path;
	const int32 Start = FindNearestNode(StartFeet);
	const int32 Goal = FindNearestNode(GoalFeet);
	if (Start == INDEX_NONE || Goal == INDEX_NONE)
	{
		return Path;
	}

	// A* with straight-line distance.
	TArray<float> Cost;
	TArray<int32> Parent;
	TArray<bool> Closed;
	Cost.Init(TNumericLimits<float>::Max(), Nodes.Num());
	Parent.Init(INDEX_NONE, Nodes.Num());
	Closed.Init(false, Nodes.Num());

	struct FOpen
	{
		int32 Node;
		float Estimate;
		bool operator<(const FOpen& Other) const { return Estimate < Other.Estimate; }
	};
	TArray<FOpen> Open;
	Cost[Start] = 0.f;
	Open.HeapPush({ Start, static_cast<float>(FVector::Dist(Nodes[Start].Location, Nodes[Goal].Location)) });

	while (Open.Num() > 0)
	{
		FOpen Current;
		Open.HeapPop(Current);
		if (Closed[Current.Node])
		{
			continue;
		}
		if (Current.Node == Goal)
		{
			break;
		}
		Closed[Current.Node] = true;
		for (const int32 Next : Nodes[Current.Node].Links)
		{
			const float NewCost = Cost[Current.Node] + static_cast<float>(FVector::Dist(Nodes[Current.Node].Location, Nodes[Next].Location));
			if (NewCost < Cost[Next])
			{
				Cost[Next] = NewCost;
				Parent[Next] = Current.Node;
				Open.HeapPush({ Next, NewCost + static_cast<float>(FVector::Dist(Nodes[Next].Location, Nodes[Goal].Location)) });
			}
		}
	}

	if (Start != Goal && Parent[Goal] == INDEX_NONE)
	{
		return Path; // Unreachable.
	}
	for (int32 Node = Goal; Node != INDEX_NONE; Node = Parent[Node])
	{
		Path.Insert(Nodes[Node].Location, 0);
		if (Node == Start)
		{
			break;
		}
	}
	return Path;
}
