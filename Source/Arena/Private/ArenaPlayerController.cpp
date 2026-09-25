#include "ArenaPlayerController.h"
#include "ArenaGameMode.h"
#include "ArenaAudio.h"
#include "ArenaGameState.h"
#include "ArenaHUD.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/LocalPlayer.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "InputModifiers.h"

namespace
{
	UInputAction* MakeAction(UObject* Outer, const TCHAR* Name, EInputActionValueType Type)
	{
		UInputAction* Action = NewObject<UInputAction>(Outer, Name);
		Action->ValueType = Type;
		return Action;
	}
}

AArenaPlayerController::AArenaPlayerController()
{
	bShowMouseCursor = false;
}

void AArenaPlayerController::PostInitializeComponents()
{
	Super::PostInitializeComponents();
	BuildInput();
}

void AArenaPlayerController::BuildInput()
{
	if (MappingContext)
	{
		return;
	}

	MappingContext = NewObject<UInputMappingContext>(this, TEXT("IMC_Arena"));

	MoveForwardAction = MakeAction(this, TEXT("IA_MoveForward"), EInputActionValueType::Axis1D);
	MoveRightAction   = MakeAction(this, TEXT("IA_MoveRight"), EInputActionValueType::Axis1D);
	LookAction        = MakeAction(this, TEXT("IA_Look"), EInputActionValueType::Axis2D);
	JumpAction        = MakeAction(this, TEXT("IA_Jump"), EInputActionValueType::Boolean);
	JumpWheelAction   = MakeAction(this, TEXT("IA_JumpWheel"), EInputActionValueType::Boolean);
	CrouchAction      = MakeAction(this, TEXT("IA_Crouch"), EInputActionValueType::Boolean);
	FireAction        = MakeAction(this, TEXT("IA_Fire"), EInputActionValueType::Boolean);
	NextWeaponAction  = MakeAction(this, TEXT("IA_NextWeapon"), EInputActionValueType::Boolean);
	LastWeaponAction  = MakeAction(this, TEXT("IA_LastWeapon"), EInputActionValueType::Boolean);
	ScoreboardAction  = MakeAction(this, TEXT("IA_Scoreboard"), EInputActionValueType::Boolean);
	WeaponActions = {
		MakeAction(this, TEXT("IA_Weapon1"), EInputActionValueType::Boolean),
		MakeAction(this, TEXT("IA_Weapon2"), EInputActionValueType::Boolean),
		MakeAction(this, TEXT("IA_Weapon3"), EInputActionValueType::Boolean),
	};

	auto Map = [this](UInputAction* Action, const FKey& Key, bool bNegate = false)
	{
		FEnhancedActionKeyMapping& Mapping = MappingContext->MapKey(Action, Key);
		if (bNegate)
		{
			Mapping.Modifiers.Add(NewObject<UInputModifierNegate>(MappingContext));
		}
	};

	Map(MoveForwardAction, EKeys::W);
	Map(MoveForwardAction, EKeys::S, true);
	Map(MoveRightAction, EKeys::D);
	Map(MoveRightAction, EKeys::A, true);
	Map(LookAction, EKeys::Mouse2D);
	Map(JumpAction, EKeys::SpaceBar);
	// HL1 bhoppers bind jump to the scroll wheel.
	Map(JumpWheelAction, EKeys::MouseScrollDown);
	Map(JumpWheelAction, EKeys::MouseScrollUp);
	Map(CrouchAction, EKeys::LeftControl);
	Map(CrouchAction, EKeys::C);
	Map(FireAction, EKeys::LeftMouseButton);
	Map(NextWeaponAction, EKeys::E);
	Map(LastWeaponAction, EKeys::Q);
	Map(ScoreboardAction, EKeys::Tab);
	Map(WeaponActions[0], EKeys::One);
	Map(WeaponActions[1], EKeys::Two);
	Map(WeaponActions[2], EKeys::Three);
}

void AArenaPlayerController::BeginPlay()
{
	Super::BeginPlay();

	if (IsLocalController())
	{
		SetInputMode(FInputModeGameOnly());
	}
}

void AArenaPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();
	BuildInput();

	if (ULocalPlayer* LP = GetLocalPlayer())
	{
		if (UEnhancedInputLocalPlayerSubsystem* Subsystem = LP->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>())
		{
			Subsystem->AddMappingContext(MappingContext, 0);
		}
	}

	if (UEnhancedInputComponent* Input = Cast<UEnhancedInputComponent>(InputComponent))
	{
		Input->BindAction(FireAction, ETriggerEvent::Started, this, &AArenaPlayerController::OnFirePressed);
		Input->BindAction(ScoreboardAction, ETriggerEvent::Started, this, &AArenaPlayerController::OnScoreboardPressed);
		Input->BindAction(ScoreboardAction, ETriggerEvent::Completed, this, &AArenaPlayerController::OnScoreboardReleased);
	}
}

void AArenaPlayerController::OnUnPossess()
{
	Super::OnUnPossess();
	if (HasAuthority())
	{
		DeathTime = GetWorld()->GetTimeSeconds();
	}
}

void AArenaPlayerController::OnFirePressed()
{
	if (!GetPawn() || GetPawn()->IsPendingKillPending())
	{
		ServerRequestRespawn();
	}
}

void AArenaPlayerController::ServerRequestRespawn_Implementation()
{
	if (AArenaGameMode* GM = GetWorld()->GetAuthGameMode<AArenaGameMode>())
	{
		GM->TryRespawn(this, true);
	}
}

void AArenaPlayerController::AutoHop(int32 bEnabled)
{
	bAutoHop = bEnabled != 0;
	SaveConfig();
	ClientMessage(FString::Printf(TEXT("autohop %d"), bAutoHop ? 1 : 0));
}

void AArenaPlayerController::Sens(float NewSensitivity)
{
	Sensitivity = FMath::Clamp(NewSensitivity, 0.01f, 100.f);
	SaveConfig();
	ClientMessage(FString::Printf(TEXT("sensitivity %.2f"), Sensitivity));
}

void AArenaPlayerController::AirAccel(float Value)
{
	ServerSetAirAccel(Value);
}

void AArenaPlayerController::ServerSetAirAccel_Implementation(float Value)
{
	if (AArenaGameState* GS = GetWorld()->GetGameState<AArenaGameState>())
	{
		GS->AirAccelerate = FMath::Clamp(Value, 0.f, 1000.f);
	}
}

void AArenaPlayerController::ClientHitConfirmed_Implementation(FVector_NetQuantize VictimLocation, int32 Damage, bool bKilled)
{
	// Quake 3 hitsound: higher pitch for light hits, lower for heavy ones.
	const float Pitch = FMath::GetMappedRangeValueClamped(FVector2D(10.f, 100.f), FVector2D(1.25f, 0.8f), static_cast<float>(Damage));
	UArenaAudio::Play2D(this, EArenaSound::Hit, 0.8f, Pitch);
	if (AArenaHUD* ArenaHUD = GetHUD<AArenaHUD>())
	{
		ArenaHUD->OnHitConfirmed(VictimLocation, Damage, bKilled);
	}
}

void AArenaPlayerController::ClientTookDamage_Implementation(FVector_NetQuantize SourceLocation, int32 Damage)
{
	if (AArenaHUD* ArenaHUD = GetHUD<AArenaHUD>())
	{
		ArenaHUD->OnDamaged(SourceLocation, Damage);
	}
}

void AArenaPlayerController::ClientFragMessage_Implementation(const FString& Text, bool bGood)
{
	if (bGood)
	{
		UArenaAudio::Play2D(this, EArenaSound::Kill);
	}
	if (AArenaHUD* ArenaHUD = GetHUD<AArenaHUD>())
	{
		ArenaHUD->ShowCenterMessage(Text, bGood ? FLinearColor(1.f, 0.85f, 0.2f) : FLinearColor(1.f, 0.3f, 0.25f));
	}
}
