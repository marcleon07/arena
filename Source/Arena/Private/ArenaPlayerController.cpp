#include "ArenaPlayerController.h"
#include "ArenaGameMode.h"
#include "ArenaAudio.h"
#include "ArenaGameState.h"
#include "ArenaHUD.h"
#include "ArenaSettings.h"
#include "AudioDevice.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Engine/GameViewportClient.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/GameUserSettings.h"
#include "GameFramework/PlayerState.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetSystemLibrary.h"
#include "ArenaMap.h"
#include "ArenaOnline.h"
#include "SArenaMapVote.h"
#include "SArenaMenu.h"
#include "Widgets/SWeakWidget.h"
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
	MenuAction        = MakeAction(this, TEXT("IA_Menu"), EInputActionValueType::Boolean);
	for (int32 i = 0; i < ArenaWeaponCount; ++i)
	{
		WeaponActions.Add(MakeAction(this, *FString::Printf(TEXT("IA_Weapon%d"), i + 1), EInputActionValueType::Boolean));
	}

	ApplyKeyBindings();
}

void AArenaPlayerController::ApplyKeyBindings()
{
	if (!MappingContext)
	{
		return;
	}
	MappingContext->UnmapAll();

	auto Map = [this](UInputAction* Action, const FKey& Key, bool bNegate = false)
	{
		if (!Action || !Key.IsValid())
		{
			return;
		}
		FEnhancedActionKeyMapping& Mapping = MappingContext->MapKey(Action, Key);
		if (bNegate)
		{
			Mapping.Modifiers.Add(NewObject<UInputModifierNegate>(MappingContext));
		}
	};

	// Fixed: mouse look and the menu. Esc ends Play-In-Editor sessions, so F10 opens the menu too.
	Map(LookAction, EKeys::Mouse2D);
	Map(MenuAction, EKeys::Escape);
	Map(MenuAction, EKeys::F10);

	// Rebindable actions from the user's settings.
	const UArenaSettings* Settings = UArenaSettings::Get();
	for (const FArenaBindableAction& Def : UArenaSettings::GetBindableActions())
	{
		const FArenaKeyBinding Binding = Settings->GetBinding(Def.Id);
		for (int32 Slot = 0; Slot < 2; ++Slot)
		{
			const FKey& Key = Binding.GetKey(Slot);
			const FString Id = Def.Id.ToString();
			if (Id == TEXT("MoveForward"))     { Map(MoveForwardAction, Key); }
			else if (Id == TEXT("MoveBack"))   { Map(MoveForwardAction, Key, true); }
			else if (Id == TEXT("MoveLeft"))   { Map(MoveRightAction, Key, true); }
			else if (Id == TEXT("MoveRight"))  { Map(MoveRightAction, Key); }
			else if (Id == TEXT("Jump"))
			{
				// Wheel "presses" have no release, so they use a jump action that never calls StopJumping.
				const bool bWheel = Key == EKeys::MouseScrollUp || Key == EKeys::MouseScrollDown;
				Map(bWheel ? JumpWheelAction : JumpAction, Key);
			}
			else if (Id == TEXT("Crouch"))     { Map(CrouchAction, Key); }
			else if (Id == TEXT("Fire"))       { Map(FireAction, Key); }
			else if (Id == TEXT("NextWeapon")) { Map(NextWeaponAction, Key); }
			else if (Id == TEXT("LastWeapon")) { Map(LastWeaponAction, Key); }
			else if (Id == TEXT("Scoreboard")) { Map(ScoreboardAction, Key); }
			else if (Id.StartsWith(TEXT("Weapon")))
			{
				const int32 Index = FCString::Atoi(*Id.RightChop(6)) - 1;
				if (WeaponActions.IsValidIndex(Index))
				{
					Map(WeaponActions[Index], Key);
				}
			}
		}
	}

	// Re-adding the context makes Enhanced Input rebuild its key mappings.
	if (ULocalPlayer* LP = GetLocalPlayer())
	{
		if (UEnhancedInputLocalPlayerSubsystem* Subsystem = LP->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>())
		{
			Subsystem->RemoveMappingContext(MappingContext);
			Subsystem->AddMappingContext(MappingContext, 0);
		}
	}
}

void AArenaPlayerController::BeginPlay()
{
	Super::BeginPlay();

	if (!IsLocalController())
	{
		return;
	}

	ApplyUserSettings(true);
	if (AArenaGameState::IsMenuWorld(GetWorld()))
	{
		// Main menu: slowly orbit the arena behind the menu.
		bAutoManageActiveCameraTarget = false;
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		MenuCamera = GetWorld()->SpawnActor<ACameraActor>(Params);
		if (MenuCamera)
		{
			MenuCamera->GetCameraComponent()->SetFieldOfView(80.f);
			UpdateMenuCamera(0.f);
		}
		OpenMenu();
	}
	else
	{
		SetInputMode(FInputModeGameOnly());
	}
}

void AArenaPlayerController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	HideMapVote();
	CloseMenu();
	Super::EndPlay(EndPlayReason);
}

void AArenaPlayerController::PlayerTick(float DeltaTime)
{
	Super::PlayerTick(DeltaTime);
	if (MenuCamera)
	{
		UpdateMenuCamera(DeltaTime);
	}
}

void AArenaPlayerController::UpdateMenuCamera(float DeltaTime)
{
	const AArenaGameState* GS = GetWorld()->GetGameState<AArenaGameState>();
	const FArenaMapDef& Map = ArenaMap::Get(GS ? GS->MapId : NAME_None);
	MenuCameraAngle += DeltaTime * 4.f;
	const float Rad = FMath::DegreesToRadians(MenuCameraAngle);
	const FVector Location(FMath::Cos(Rad) * Map.MenuOrbitRadius, FMath::Sin(Rad) * Map.MenuOrbitRadius, Map.MenuOrbitHeight);
	const FVector Focus(0.f, 0.f, 300.f);
	MenuCamera->SetActorLocationAndRotation(Location, (Focus - Location).Rotation());
	if (GetViewTarget() != MenuCamera)
	{
		SetViewTarget(MenuCamera);
	}
}

// ---------------------------------------------------------------------------
// Menu
// ---------------------------------------------------------------------------

void AArenaPlayerController::OnMenuPressed()
{
	if (!AArenaGameState::IsMenuWorld(GetWorld()))
	{
		IsMenuOpen() ? CloseMenu() : OpenMenu();
	}
}

void AArenaPlayerController::OpenMenu()
{
	UGameViewportClient* Viewport = GetWorld() ? GetWorld()->GetGameViewport() : nullptr;
	if (IsMenuOpen() || !IsLocalController() || !Viewport)
	{
		return;
	}

	MenuWidget = SNew(SArenaMenu)
		.Owner(this)
		.InGame(!AArenaGameState::IsMenuWorld(GetWorld()));
	MenuContainer = SNew(SWeakWidget).PossiblyNullContent(MenuWidget);
	Viewport->AddViewportWidgetContent(MenuContainer.ToSharedRef(), 100);

	// The match keeps running (it's multiplayer); the menu just takes the input.
	SetUIInputFor(MenuWidget);
}

void AArenaPlayerController::SetUIInputFor(TSharedPtr<SWidget> Widget)
{
	FInputModeUIOnly Mode;
	Mode.SetWidgetToFocus(Widget);
	Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	SetInputMode(Mode);
	SetShowMouseCursor(true);
	FlushPressedKeys();
}

void AArenaPlayerController::CloseMenu()
{
	if (!IsMenuOpen())
	{
		return;
	}
	if (UGameViewportClient* Viewport = GetWorld() ? GetWorld()->GetGameViewport() : nullptr)
	{
		Viewport->RemoveViewportWidgetContent(MenuContainer.ToSharedRef());
	}
	MenuWidget.Reset();
	MenuContainer.Reset();

	// Back to the vote if the match is over, otherwise back to playing.
	if (VoteWidget.IsValid())
	{
		SetUIInputFor(VoteWidget);
		return;
	}
	SetInputMode(FInputModeGameOnly());
	SetShowMouseCursor(false);
}

void AArenaPlayerController::ShowMapVote()
{
	UGameViewportClient* Viewport = GetWorld() ? GetWorld()->GetGameViewport() : nullptr;
	if (VoteWidget.IsValid() || !IsLocalController() || !Viewport)
	{
		return;
	}
	VoteWidget = SNew(SArenaMapVote).Owner(this);
	VoteContainer = SNew(SWeakWidget).PossiblyNullContent(VoteWidget);
	// Below the menu (100) so Esc still opens it on top.
	Viewport->AddViewportWidgetContent(VoteContainer.ToSharedRef(), 50);
	if (!IsMenuOpen())
	{
		SetUIInputFor(VoteWidget);
	}
}

void AArenaPlayerController::HideMapVote()
{
	if (!VoteWidget.IsValid())
	{
		return;
	}
	if (UGameViewportClient* Viewport = GetWorld() ? GetWorld()->GetGameViewport() : nullptr)
	{
		Viewport->RemoveViewportWidgetContent(VoteContainer.ToSharedRef());
	}
	VoteWidget.Reset();
	VoteContainer.Reset();
}

void AArenaPlayerController::ServerVoteMap_Implementation(FName Map)
{
	if (AArenaGameMode* GM = GetWorld()->GetAuthGameMode<AArenaGameMode>())
	{
		GM->CastVote(this, Map);
	}
}

void AArenaPlayerController::HostGame()
{
	const UArenaSettings* Settings = UArenaSettings::Get();
	const FString Options = FString::Printf(TEXT("listen?Arena=%s?FragLimit=%d?TimeLimit=%d?Bots=%d?BotSkill=%d"), *Settings->HostMap.ToString(),
		Settings->HostFragLimit, Settings->HostTimeLimit, Settings->HostBots, Settings->HostBotSkill);
	// Lists the game on Steam (or the LAN) first, then opens the map.
	if (UArenaOnline* Online = UArenaOnline::Get(this))
	{
		Online->HostGame(Options, Settings->HostMap, Settings->HostBots, Settings->bHostFriendsOnly);
		return;
	}
	UGameplayStatics::OpenLevel(this, FName(TEXT("/Engine/Maps/Entry")), true, Options);
}

void AArenaPlayerController::JoinGame(const FString& Address)
{
	ClientTravel(Address, TRAVEL_Absolute);
}

void AArenaPlayerController::Disconnect()
{
	if (UArenaOnline* Online = UArenaOnline::Get(this))
	{
		Online->LeaveSession();
	}
	// Loading the map without "listen" gives a standalone world, i.e. the main menu.
	UGameplayStatics::OpenLevel(this, FName(TEXT("/Engine/Maps/Entry")));
}

void AArenaPlayerController::QuitToDesktop()
{
	UKismetSystemLibrary::QuitGame(this, this, EQuitPreference::Quit, false);
}

void AArenaPlayerController::ApplyUserSettings(bool bIncludeDisplay)
{
	const UArenaSettings* Settings = UArenaSettings::Get();

	FAudioDeviceHandle AudioDevice = GetWorld()->GetAudioDevice();
	if (AudioDevice.IsValid())
	{
		AudioDevice->SetTransientPrimaryVolume(Settings->MasterVolume);
	}

	// FOV is applied by the pawn's camera every frame (see AArenaCharacter::Tick).

	if (!AArenaGameState::IsMenuWorld(GetWorld()) && !Settings->PlayerName.IsEmpty()
		&& PlayerState && PlayerState->GetPlayerName() != Settings->PlayerName)
	{
		ServerChangeName(Settings->PlayerName);
	}

	// Window mode would resize the editor viewport in PIE, so only in real game runs.
	if (bIncludeDisplay && !GIsEditor && GEngine)
	{
		UGameUserSettings* UserSettings = GEngine->GetGameUserSettings();
		const EWindowMode::Type Mode = Settings->bFullscreen ? EWindowMode::WindowedFullscreen : EWindowMode::Windowed;
		if (UserSettings && (UserSettings->GetFullscreenMode() != Mode
			|| UserSettings->IsVSyncEnabled() != Settings->bVSync
			|| !FMath::IsNearlyEqual(UserSettings->GetFrameRateLimit(), Settings->FrameRateLimit)))
		{
			UserSettings->SetFullscreenMode(Mode);
			if (Settings->bFullscreen)
			{
				UserSettings->SetScreenResolution(UserSettings->GetDesktopResolution());
			}
			UserSettings->SetVSyncEnabled(Settings->bVSync);
			UserSettings->SetFrameRateLimit(Settings->FrameRateLimit);
			UserSettings->ApplySettings(false);
		}
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
		Input->BindAction(MenuAction, ETriggerEvent::Started, this, &AArenaPlayerController::OnMenuPressed);
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
	UArenaSettings* Settings = UArenaSettings::Get();
	Settings->bAutoHop = bEnabled != 0;
	Settings->Save();
	ClientMessage(FString::Printf(TEXT("autohop %d"), Settings->bAutoHop ? 1 : 0));
}

void AArenaPlayerController::Sens(float NewSensitivity)
{
	UArenaSettings* Settings = UArenaSettings::Get();
	Settings->Sensitivity = FMath::Clamp(NewSensitivity, 0.01f, 100.f);
	Settings->Save();
	ClientMessage(FString::Printf(TEXT("sensitivity %.2f"), Settings->Sensitivity));
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

void AArenaPlayerController::AddBot(int32 Skill)
{
	AArenaGameMode* GM = GetWorld()->GetAuthGameMode<AArenaGameMode>();
	if (!GM || AArenaGameState::IsMenuWorld(GetWorld()))
	{
		ClientMessage(TEXT("Only the host can add bots, from inside a match."));
		return;
	}
	ClientMessage(GM->AddBot(Skill) ? TEXT("Bot added") : TEXT("Server is full"));
}

void AArenaPlayerController::RemoveBot()
{
	if (AArenaGameMode* GM = GetWorld()->GetAuthGameMode<AArenaGameMode>())
	{
		GM->RemoveBot();
	}
}
