#include "ArenaHUD.h"
#include "ArenaCharacter.h"
#include "ArenaGameState.h"
#include "ArenaMovementComponent.h"
#include "ArenaPlayerController.h"
#include "ArenaPlayerState.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "GameFramework/GameMode.h"

namespace
{
	const FLinearColor TextColor(0.95f, 0.95f, 0.95f);
	const FLinearColor DimColor(0.6f, 0.6f, 0.6f);
	const FLinearColor Panel(0.f, 0.f, 0.f, 0.55f);

	FLinearColor HealthColor(int32 Value)
	{
		if (Value > 100) return FLinearColor(0.4f, 0.7f, 1.f);
		if (Value > 50)  return TextColor;
		if (Value > 25)  return FLinearColor(1.f, 0.8f, 0.2f);
		return FLinearColor(1.f, 0.2f, 0.2f);
	}
}

void AArenaHUD::DrawHUD()
{
	Super::DrawHUD();
	if (!Canvas)
	{
		return;
	}
	UIScale = Canvas->ClipY / 1080.f;

	if (AArenaGameState::IsMenuWorld(GetWorld()))
	{
		return;
	}

	const AArenaGameState* GS = GetWorld()->GetGameState<AArenaGameState>();
	const AArenaPlayerController* PC = Cast<AArenaPlayerController>(GetOwningPlayerController());
	const AArenaCharacter* Pawn = Cast<AArenaCharacter>(GetOwningPawn());
	const bool bAlive = Pawn && !Pawn->IsDead();
	const bool bMatchOver = GS && GS->GetMatchState() == MatchState::WaitingPostMatch;

	if (bAlive)
	{
		DrawDamageFeedback();
		DrawHitNumbers();
		DrawCrosshair();
		DrawHitMarker();
		DrawSpeedometer(Pawn);
		DrawStatus(Pawn);
		DrawWeapons(Pawn);
	}
	else if (!bMatchOver)
	{
		DrawCentered(TEXT("You are dead. Click to respawn."), Canvas->ClipY * 0.6f, TextColor, GEngine->GetMediumFont(), 1.2f);
	}

	DrawCenterMessage();

	if (GS)
	{
		DrawMatchInfo(GS);
		DrawKillFeed(GS);
		if (bMatchOver || (PC && PC->IsScoreboardHeld()))
		{
			DrawScoreboard(GS);
		}
	}
}

float AArenaHUD::Now() const
{
	return static_cast<float>(GetWorld()->GetTimeSeconds());
}

// ---------------------------------------------------------------------------
// Combat feedback
// ---------------------------------------------------------------------------

void AArenaHUD::OnHitConfirmed(const FVector& VictimLocation, int32 Damage, bool bKilled)
{
	const float T = Now();
	LastHitTime = T;
	bLastHitKilled = bKilled;

	// Fold machinegun streams and multi-hit splash into one rising number.
	if (HitNumbers.Num() > 0)
	{
		FHitNumber& Last = HitNumbers.Last();
		if (T - Last.Time < 0.3f && FVector::Dist(Last.Location, VictimLocation) < 300.f)
		{
			Last.Damage += Damage;
			Last.Location = VictimLocation;
			Last.Time = T;
			return;
		}
	}
	HitNumbers.Add({ VictimLocation, Damage, T });
}

void AArenaHUD::OnDamaged(const FVector& SourceLocation, int32 Damage)
{
	const float T = Now();
	// Stack flashes that land close together (splash + direct, machinegun bursts).
	LastDamageAmount = (T - LastDamageTime < 0.2f ? LastDamageAmount : 0.f) + Damage;
	LastDamageTime = T;
	DamageMarkers.Add({ SourceLocation, static_cast<float>(Damage), T });
}

void AArenaHUD::ShowCenterMessage(const FString& Text, const FLinearColor& Color)
{
	CenterMessage = Text;
	CenterMessageColor = Color;
	CenterMessageTime = Now();
}

void AArenaHUD::DrawHitMarker()
{
	constexpr float Duration = 0.25f;
	const float Age = Now() - LastHitTime;
	if (Age > Duration)
	{
		return;
	}
	const float CX = Canvas->ClipX * 0.5f;
	const float CY = Canvas->ClipY * 0.5f;
	const float Inner = 7.f * UIScale;
	const float Outer = (bLastHitKilled ? 20.f : 15.f) * UIScale;
	FLinearColor Color = bLastHitKilled ? FLinearColor(1.f, 0.15f, 0.1f) : FLinearColor::White;
	Color.A = 1.f - Age / Duration;
	for (const FVector2D Dir : { FVector2D(1, 1), FVector2D(-1, 1), FVector2D(1, -1), FVector2D(-1, -1) })
	{
		DrawLine(CX + Dir.X * Inner, CY + Dir.Y * Inner, CX + Dir.X * Outer, CY + Dir.Y * Outer, Color, 2.f * UIScale);
	}
}

void AArenaHUD::DrawHitNumbers()
{
	constexpr float Duration = 0.9f;
	const float T = Now();
	HitNumbers.RemoveAll([T](const FHitNumber& Number) { return T - Number.Time > Duration; });

	const APlayerController* PC = GetOwningPlayerController();
	if (!PC)
	{
		return;
	}
	FVector ViewLoc;
	FRotator ViewRot;
	PC->GetPlayerViewPoint(ViewLoc, ViewRot);

	UFont* Font = GEngine->GetMediumFont();
	for (const FHitNumber& Number : HitNumbers)
	{
		const float Age = T - Number.Time;
		const FVector World = Number.Location + FVector(0.f, 0.f, 110.f + Age * 90.f);
		if (FVector::DotProduct(World - ViewLoc, ViewRot.Vector()) <= 0.f)
		{
			continue;
		}
		const FVector Screen = Project(World);
		// Quake-style colours: bigger hits get warmer.
		FLinearColor Color = Number.Damage >= 100 ? FLinearColor(1.f, 0.2f, 0.1f)
			: Number.Damage >= 50 ? FLinearColor(1.f, 0.6f, 0.1f)
			: FLinearColor(1.f, 0.95f, 0.4f);
		Color.A = 1.f - FMath::Square(Age / Duration);
		const FString Text = FString::FromInt(Number.Damage);
		const float TextScale = (Number.Damage >= 100 ? 1.5f : 1.1f) * UIScale;
		float W = 0.f, H = 0.f;
		GetTextSize(Text, W, H, Font, TextScale);
		DrawText(Text, Color, Screen.X - W * 0.5f, Screen.Y - H * 0.5f, Font, TextScale);
	}
}

void AArenaHUD::DrawDamageFeedback()
{
	const float T = Now();

	// Red flash, stronger for bigger hits.
	constexpr float FlashTime = 0.4f;
	const float FlashAge = T - LastDamageTime;
	if (FlashAge < FlashTime)
	{
		const float Alpha = FMath::Clamp(LastDamageAmount / 100.f, 0.12f, 0.45f) * (1.f - FlashAge / FlashTime);
		DrawRect(FLinearColor(0.7f, 0.f, 0.f, Alpha), 0.f, 0.f, Canvas->ClipX, Canvas->ClipY);
	}

	// Arcs around the crosshair pointing at where the damage came from.
	constexpr float MarkerTime = 1.2f;
	DamageMarkers.RemoveAll([T](const FDamageMarker& Marker) { return T - Marker.Time > MarkerTime; });

	const APlayerController* PC = GetOwningPlayerController();
	if (!PC || DamageMarkers.Num() == 0)
	{
		return;
	}
	FVector ViewLoc;
	FRotator ViewRot;
	PC->GetPlayerViewPoint(ViewLoc, ViewRot);

	const float CX = Canvas->ClipX * 0.5f;
	const float CY = Canvas->ClipY * 0.5f;
	const float Radius = 120.f * UIScale;
	constexpr int32 Segments = 6;
	constexpr float HalfSpan = 0.32f; // radians
	for (const FDamageMarker& Marker : DamageMarkers)
	{
		const FVector ToSource = Marker.Source - ViewLoc;
		if (ToSource.SizeSquared2D() < FMath::Square(50.f))
		{
			continue; // Self damage at your feet has no useful direction.
		}
		// Yaw grows clockwise seen from above, so +angle is to the right; forward is up on screen.
		const float Angle = FMath::DegreesToRadians(FRotator::NormalizeAxis(ToSource.Rotation().Yaw - ViewRot.Yaw));
		const float Alpha = (1.f - (T - Marker.Time) / MarkerTime) * FMath::Clamp(Marker.Strength / 60.f, 0.45f, 1.f);
		const FLinearColor Color(1.f, 0.1f, 0.05f, Alpha);
		for (int32 i = 0; i < Segments; ++i)
		{
			const float A0 = Angle - HalfSpan + 2.f * HalfSpan * i / Segments;
			const float A1 = Angle - HalfSpan + 2.f * HalfSpan * (i + 1) / Segments;
			DrawLine(CX + FMath::Sin(A0) * Radius, CY - FMath::Cos(A0) * Radius,
			         CX + FMath::Sin(A1) * Radius, CY - FMath::Cos(A1) * Radius, Color, 6.f * UIScale);
		}
	}
}

void AArenaHUD::DrawCenterMessage()
{
	constexpr float Duration = 2.5f;
	constexpr float FadeTime = 0.5f;
	const float Age = Now() - CenterMessageTime;
	if (Age > Duration || CenterMessage.IsEmpty())
	{
		return;
	}
	FLinearColor Color = CenterMessageColor;
	Color.A = FMath::Clamp((Duration - Age) / FadeTime, 0.f, 1.f);

	TArray<FString> Lines;
	CenterMessage.ParseIntoArrayLines(Lines);
	float Y = Canvas->ClipY * 0.3f;
	for (int32 i = 0; i < Lines.Num(); ++i)
	{
		DrawCentered(Lines[i], Y, i == 0 ? Color : FLinearColor(TextColor.R, TextColor.G, TextColor.B, Color.A), GEngine->GetMediumFont(), i == 0 ? 1.5f : 1.1f);
		Y += (i == 0 ? 42.f : 30.f) * UIScale;
	}
}

void AArenaHUD::DrawCentered(const FString& Text, float Y, const FLinearColor& Color, UFont* Font, float TextScale)
{
	float W = 0.f, H = 0.f;
	GetTextSize(Text, W, H, Font, TextScale * UIScale);
	DrawText(Text, Color, (Canvas->ClipX - W) * 0.5f, Y, Font, TextScale * UIScale);
}

void AArenaHUD::DrawCrosshair()
{
	const float CX = Canvas->ClipX * 0.5f;
	const float CY = Canvas->ClipY * 0.5f;
	const float Gap = 4.f * UIScale;
	const float Len = 9.f * UIScale;
	const FLinearColor Color(0.2f, 1.f, 0.3f);
	DrawLine(CX - Gap - Len, CY, CX - Gap, CY, Color, 2.f);
	DrawLine(CX + Gap, CY, CX + Gap + Len, CY, Color, 2.f);
	DrawLine(CX, CY - Gap - Len, CX, CY - Gap, Color, 2.f);
	DrawLine(CX, CY + Gap, CX, CY + Gap + Len, Color, 2.f);
}

void AArenaHUD::DrawSpeedometer(const AArenaCharacter* Pawn)
{
	const UArenaMovementComponent* Move = Pawn->GetArenaMovement();
	if (!Move)
	{
		return;
	}
	// 320 u/s is running speed; anything above that is bhop gain.
	const float Speed = Move->GetHorizontalSpeedUPS();
	const FLinearColor Color = Speed > 330.f ? FLinearColor::LerpUsingHSV(TextColor, FLinearColor(0.2f, 1.f, 0.4f), FMath::Clamp((Speed - 330.f) / 400.f, 0.f, 1.f)) : TextColor;
	DrawCentered(FString::Printf(TEXT("%d"), FMath::RoundToInt(Speed)), Canvas->ClipY * 0.5f + 40.f * UIScale, Color, GEngine->GetMediumFont(), 1.2f);
}

void AArenaHUD::DrawStatus(const AArenaCharacter* Pawn)
{
	UFont* Big = GEngine->GetLargeFont();
	UFont* Small = GEngine->GetSmallFont();
	const float X = 40.f * UIScale;
	const float Y = Canvas->ClipY - 110.f * UIScale;

	DrawRect(Panel, X - 12.f * UIScale, Y - 10.f * UIScale, 330.f * UIScale, 90.f * UIScale);
	DrawText(TEXT("HEALTH"), DimColor, X, Y, Small, UIScale);
	DrawText(FString::FromInt(Pawn->GetHealth()), HealthColor(Pawn->GetHealth()), X, Y + 18.f * UIScale, Big, 2.f * UIScale);
	DrawText(TEXT("ARMOR"), DimColor, X + 170.f * UIScale, Y, Small, UIScale);
	DrawText(FString::FromInt(Pawn->GetArmor()), FLinearColor(1.f, 0.85f, 0.3f), X + 170.f * UIScale, Y + 18.f * UIScale, Big, 2.f * UIScale);
}

void AArenaHUD::DrawWeapons(const AArenaCharacter* Pawn)
{
	UFont* Font = GEngine->GetMediumFont();
	const float SlotW = 170.f * UIScale;
	const float SlotH = 36.f * UIScale;
	float X = Canvas->ClipX - (SlotW + 8.f * UIScale) * ArenaWeaponCount - 30.f * UIScale;
	const float Y = Canvas->ClipY - 70.f * UIScale;

	for (int32 i = 0; i < ArenaWeaponCount; ++i)
	{
		const EArenaWeapon Weapon = static_cast<EArenaWeapon>(i);
		const FArenaWeaponInfo& Info = GetWeaponInfo(Weapon);
		const bool bOwned = Pawn->HasWeapon(Weapon);
		const bool bCurrent = Pawn->GetCurrentWeapon() == Weapon;

		DrawRect(bCurrent ? FLinearColor(Info.Color.R, Info.Color.G, Info.Color.B, 0.45f) : Panel, X, Y, SlotW, SlotH);
		const FString Label = FString::Printf(TEXT("%d %s  %d"), i + 1, Info.Name, bOwned ? Pawn->GetAmmo(Weapon) : 0);
		DrawText(Label, bOwned ? TextColor : DimColor * 0.6f, X + 8.f * UIScale, Y + 6.f * UIScale, Font, 0.9f * UIScale);
		X += SlotW + 8.f * UIScale;
	}
}

void AArenaHUD::DrawMatchInfo(const AArenaGameState* GS)
{
	UFont* Font = GEngine->GetMediumFont();

	const int32 Seconds = FMath::CeilToInt(GS->GetTimeRemaining());
	if (GS->MatchEndTime > 0.f)
	{
		DrawCentered(FString::Printf(TEXT("%d:%02d"), Seconds / 60, Seconds % 60), 20.f * UIScale, TextColor, Font, 1.3f);
	}

	const TArray<AArenaPlayerState*> Sorted = GS->GetSortedPlayers();
	const AArenaPlayerState* Mine = GetOwningPlayerController() ? GetOwningPlayerController()->GetPlayerState<AArenaPlayerState>() : nullptr;
	if (Mine)
	{
		const int32 Rank = Sorted.IndexOfByKey(Mine) + 1;
		const FString Text = FString::Printf(TEXT("Frags %d   Rank %d/%d   Limit %d"), Mine->Frags, Rank, Sorted.Num(), GS->FragLimit);
		float W = 0.f, H = 0.f;
		GetTextSize(Text, W, H, Font, UIScale);
		DrawText(Text, TextColor, Canvas->ClipX - W - 30.f * UIScale, 20.f * UIScale, Font, UIScale);
	}
}

void AArenaHUD::DrawKillFeed(const AArenaGameState* GS)
{
	UFont* Font = GEngine->GetSmallFont();
	const float Now = GetWorld()->GetTimeSeconds();
	float Y = 20.f * UIScale;
	for (const FArenaKillFeedEntry& Entry : GS->GetKillFeed())
	{
		if (Now - Entry.Time > 6.f)
		{
			continue;
		}
		const FString Line = Entry.Killer.IsEmpty()
			? FString::Printf(TEXT("%s died"), *Entry.Victim)
			: FString::Printf(TEXT("%s  [%s]  %s"), *Entry.Killer, GetWeaponInfo(Entry.Weapon).Name, *Entry.Victim);
		DrawText(Line, TextColor, 30.f * UIScale, Y, Font, 1.1f * UIScale);
		Y += 22.f * UIScale;
	}
}

void AArenaHUD::DrawScoreboard(const AArenaGameState* GS)
{
	UFont* Font = GEngine->GetMediumFont();
	const TArray<AArenaPlayerState*> Sorted = GS->GetSortedPlayers();
	const float W = 620.f * UIScale;
	const float RowH = 34.f * UIScale;
	const float X = (Canvas->ClipX - W) * 0.5f;
	float Y = Canvas->ClipY * 0.2f;

	DrawRect(Panel, X, Y, W, RowH * (Sorted.Num() + 2) + 20.f * UIScale);

	const FString Title = GS->WinnerName.IsEmpty() ? FString(TEXT("DEATHMATCH")) : FString::Printf(TEXT("%s WINS"), *GS->WinnerName);
	DrawCentered(Title, Y + 8.f * UIScale, FLinearColor(1.f, 0.8f, 0.2f), Font, 1.3f);
	Y += RowH + 10.f * UIScale;

	DrawText(TEXT("NAME"), DimColor, X + 50.f * UIScale, Y, Font, UIScale);
	DrawText(TEXT("FRAGS"), DimColor, X + 360.f * UIScale, Y, Font, UIScale);
	DrawText(TEXT("DEATHS"), DimColor, X + 450.f * UIScale, Y, Font, UIScale);
	DrawText(TEXT("PING"), DimColor, X + 550.f * UIScale, Y, Font, UIScale);
	Y += RowH;

	for (const AArenaPlayerState* PS : Sorted)
	{
		DrawRect(PS->GetPlayerColor(), X + 16.f * UIScale, Y + 6.f * UIScale, 20.f * UIScale, 20.f * UIScale);
		DrawText(PS->GetPlayerName(), TextColor, X + 50.f * UIScale, Y, Font, UIScale);
		DrawText(FString::FromInt(PS->Frags), TextColor, X + 360.f * UIScale, Y, Font, UIScale);
		DrawText(FString::FromInt(PS->Deaths), TextColor, X + 450.f * UIScale, Y, Font, UIScale);
		DrawText(FString::FromInt(FMath::RoundToInt(PS->GetPingInMilliseconds())), TextColor, X + 550.f * UIScale, Y, Font, UIScale);
		Y += RowH;
	}
}
