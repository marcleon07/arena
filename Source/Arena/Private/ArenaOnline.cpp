#include "ArenaOnline.h"
#include "Arena.h"
#include "ArenaMap.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/PlayerController.h"
#include "Interfaces/OnlineExternalUIInterface.h"
#include "Kismet/GameplayStatics.h"
#include "Online/OnlineSessionNames.h"
#include "OnlineSessionSettings.h"
#include "OnlineSubsystem.h"

#define LOCTEXT_NAMESPACE "ArenaOnline"

namespace
{
	// Session metadata. ARENAGAME filters our games out of everything else using
	// the shared Spacewar (480) test app on Steam.
	const FName KeyGame(TEXT("ARENAGAME"));
	const FName KeyMap(TEXT("ARENAMAP"));
	const FName KeyHost(TEXT("ARENAHOST"));
	const FName KeyBots(TEXT("ARENABOTS"));
	constexpr int32 ArenaProtocol = 1;
	constexpr int32 MaxPlayers = 16;
}

UArenaOnline* UArenaOnline::Get(const UObject* WorldContext)
{
	const UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	const UGameInstance* GI = World ? World->GetGameInstance() : nullptr;
	return GI ? GI->GetSubsystem<UArenaOnline>() : nullptr;
}

IOnlineSessionPtr UArenaOnline::GetSessions() const
{
	const IOnlineSubsystem* OSS = IOnlineSubsystem::Get();
	return OSS ? OSS->GetSessionInterface() : nullptr;
}

bool UArenaOnline::IsSteam() const
{
	const IOnlineSubsystem* OSS = IOnlineSubsystem::Get();
	return OSS && OSS->GetSubsystemName() == FName(TEXT("STEAM"));
}

FText UArenaOnline::GetServiceName() const
{
	return IsSteam() ? LOCTEXT("Steam", "Steam") : LOCTEXT("Lan", "LAN");
}

void UArenaOnline::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	const IOnlineSubsystem* OSS = IOnlineSubsystem::Get();
	UE_LOG(LogArena, Log, TEXT("Online subsystem: %s"), OSS ? *OSS->GetSubsystemName().ToString() : TEXT("none"));

	if (IOnlineSessionPtr Sessions = GetSessions())
	{
		CreateHandle = Sessions->AddOnCreateSessionCompleteDelegate_Handle(FOnCreateSessionCompleteDelegate::CreateUObject(this, &UArenaOnline::OnCreateComplete));
		DestroyHandle = Sessions->AddOnDestroySessionCompleteDelegate_Handle(FOnDestroySessionCompleteDelegate::CreateUObject(this, &UArenaOnline::OnDestroyComplete));
		FindHandle = Sessions->AddOnFindSessionsCompleteDelegate_Handle(FOnFindSessionsCompleteDelegate::CreateUObject(this, &UArenaOnline::OnFindComplete));
		JoinHandle = Sessions->AddOnJoinSessionCompleteDelegate_Handle(FOnJoinSessionCompleteDelegate::CreateUObject(this, &UArenaOnline::OnJoinComplete));
		// Accepting a Steam invite (or "Join Game" on a friend) lands here, including at startup.
		InviteHandle = Sessions->AddOnSessionUserInviteAcceptedDelegate_Handle(FOnSessionUserInviteAcceptedDelegate::CreateUObject(this, &UArenaOnline::OnInviteAccepted));
	}
}

void UArenaOnline::Deinitialize()
{
	if (IOnlineSessionPtr Sessions = GetSessions())
	{
		Sessions->ClearOnCreateSessionCompleteDelegate_Handle(CreateHandle);
		Sessions->ClearOnDestroySessionCompleteDelegate_Handle(DestroyHandle);
		Sessions->ClearOnFindSessionsCompleteDelegate_Handle(FindHandle);
		Sessions->ClearOnJoinSessionCompleteDelegate_Handle(JoinHandle);
		Sessions->ClearOnSessionUserInviteAcceptedDelegate_Handle(InviteHandle);
	}
	Super::Deinitialize();
}

// ---------------------------------------------------------------------------
// Hosting
// ---------------------------------------------------------------------------

void UArenaOnline::HostGame(const FString& TravelOptions, FName MapId, int32 Bots, bool bFriendsOnly)
{
	PendingTravelOptions = TravelOptions;
	PendingMap = MapId;
	PendingBots = Bots;
	bPendingFriendsOnly = bFriendsOnly;

	IOnlineSessionPtr Sessions = GetSessions();
	if (!Sessions)
	{
		TravelToHostedGame(); // No online subsystem at all: host unlisted (IP join still works).
		return;
	}
	if (Sessions->GetNamedSession(NAME_GameSession))
	{
		bHostAfterDestroy = true;
		Sessions->DestroySession(NAME_GameSession);
		return;
	}
	CreateSession();
}

void UArenaOnline::CreateSession()
{
	IOnlineSessionPtr Sessions = GetSessions();
	const bool bLan = !IsSteam();

	FOnlineSessionSettings Settings;
	Settings.NumPublicConnections = MaxPlayers;
	Settings.bIsLANMatch = bLan;
	Settings.bShouldAdvertise = bLan || !bPendingFriendsOnly;
	Settings.bAllowJoinInProgress = true;
	Settings.bAllowInvites = true;
	Settings.bUsesPresence = true;
	Settings.bUseLobbiesIfAvailable = true;
	Settings.bAllowJoinViaPresence = true;
	Settings.bAllowJoinViaPresenceFriendsOnly = bPendingFriendsOnly;

	const ULocalPlayer* LP = GetGameInstance()->GetFirstGamePlayer();
	const FString HostName = LP ? LP->GetNickname() : FString(TEXT("Arena"));
	Settings.Set(KeyGame, ArenaProtocol, EOnlineDataAdvertisementType::ViaOnlineServiceAndPing);
	Settings.Set(KeyMap, PendingMap.ToString(), EOnlineDataAdvertisementType::ViaOnlineServiceAndPing);
	Settings.Set(KeyHost, HostName, EOnlineDataAdvertisementType::ViaOnlineServiceAndPing);
	Settings.Set(KeyBots, PendingBots, EOnlineDataAdvertisementType::ViaOnlineServiceAndPing);

	StatusText = FText::Format(LOCTEXT("Creating", "Creating {0} game..."), GetServiceName());
	if (!Sessions->CreateSession(0, NAME_GameSession, Settings))
	{
		OnCreateComplete(NAME_GameSession, false);
	}
}

void UArenaOnline::OnCreateComplete(FName SessionName, bool bWasSuccessful)
{
	UE_LOG(LogArena, Log, TEXT("Create session: %s"), bWasSuccessful ? TEXT("ok") : TEXT("failed"));
	StatusText = bWasSuccessful ? FText::GetEmpty() : LOCTEXT("CreateFailed", "Couldn't list the game online; hosting anyway (join by IP).");
	TravelToHostedGame();
}

void UArenaOnline::TravelToHostedGame()
{
	UGameplayStatics::OpenLevel(GetGameInstance(), FName(TEXT("/Engine/Maps/Entry")), true, PendingTravelOptions);
}

void UArenaOnline::UpdateListing(FName MapId, int32 Bots)
{
	IOnlineSessionPtr Sessions = GetSessions();
	FOnlineSessionSettings* Current = Sessions ? Sessions->GetSessionSettings(NAME_GameSession) : nullptr;
	if (!Current)
	{
		return;
	}
	FOnlineSessionSettings Updated = *Current;
	Updated.Set(KeyMap, MapId.ToString(), EOnlineDataAdvertisementType::ViaOnlineServiceAndPing);
	Updated.Set(KeyBots, Bots, EOnlineDataAdvertisementType::ViaOnlineServiceAndPing);
	Sessions->UpdateSession(NAME_GameSession, Updated, true);
}

void UArenaOnline::LeaveSession()
{
	bHostAfterDestroy = false;
	JoinAfterDestroy.Reset();
	IOnlineSessionPtr Sessions = GetSessions();
	if (Sessions && Sessions->GetNamedSession(NAME_GameSession))
	{
		Sessions->DestroySession(NAME_GameSession);
	}
}

void UArenaOnline::OnDestroyComplete(FName SessionName, bool bWasSuccessful)
{
	if (bHostAfterDestroy)
	{
		bHostAfterDestroy = false;
		CreateSession();
	}
	else if (JoinAfterDestroy.IsSet())
	{
		const FOnlineSessionSearchResult Result = JoinAfterDestroy.GetValue();
		JoinAfterDestroy.Reset();
		JoinSearchResult(Result);
	}
}

bool UArenaOnline::ShowInviteUI()
{
	const IOnlineSubsystem* OSS = IOnlineSubsystem::Get();
	const IOnlineExternalUIPtr ExternalUI = OSS ? OSS->GetExternalUIInterface() : nullptr;
	return IsSteam() && ExternalUI && GetSessions() && GetSessions()->GetNamedSession(NAME_GameSession)
		&& ExternalUI->ShowInviteUI(0, NAME_GameSession);
}

// ---------------------------------------------------------------------------
// Finding and joining
// ---------------------------------------------------------------------------

void UArenaOnline::FindGames()
{
	IOnlineSessionPtr Sessions = GetSessions();
	if (!Sessions || bSearching)
	{
		return;
	}
	Search = MakeShared<FOnlineSessionSearch>();
	Search->MaxSearchResults = 200;
	Search->bIsLanQuery = !IsSteam();
	// UE 5.4's Steam plugin reads SEARCH_PRESENCE to choose a lobby search (it rejects SEARCH_LOBBIES).
	Search->QuerySettings.Set(SEARCH_PRESENCE, true, EOnlineComparisonOp::Equals);
	Search->QuerySettings.Set(KeyGame, ArenaProtocol, EOnlineComparisonOp::Equals);

	bSearching = true;
	Results.Reset();
	StatusText = FText::Format(LOCTEXT("Searching", "Searching {0}..."), GetServiceName());
	OnSearchUpdated.Broadcast();
	if (!Sessions->FindSessions(0, Search.ToSharedRef()))
	{
		OnFindComplete(false);
	}
}

void UArenaOnline::OnFindComplete(bool bWasSuccessful)
{
	bSearching = false;
	Results.Reset();
	if (Search.IsValid())
	{
		for (int32 i = 0; i < Search->SearchResults.Num(); ++i)
		{
			const FOnlineSessionSearchResult& Result = Search->SearchResults[i];
			int32 Protocol = 0;
			if (!Result.Session.SessionSettings.Get(KeyGame, Protocol) || Protocol != ArenaProtocol)
			{
				continue; // Someone else's Spacewar lobby, or an incompatible version.
			}
			FArenaServerEntry Entry;
			FString MapString;
			Result.Session.SessionSettings.Get(KeyMap, MapString);
			Result.Session.SessionSettings.Get(KeyHost, Entry.HostName);
			Result.Session.SessionSettings.Get(KeyBots, Entry.Bots);
			if (Entry.HostName.IsEmpty())
			{
				Entry.HostName = Result.Session.OwningUserName;
			}
			Entry.MapId = ArenaMap::Get(FName(*MapString)).Id;
			Entry.MaxPlayers = Result.Session.SessionSettings.NumPublicConnections;
			Entry.Players = FMath::Max(1, Entry.MaxPlayers - Result.Session.NumOpenPublicConnections);
			Entry.PingMs = Result.PingInMs;
			Entry.ResultIndex = i;
			Results.Add(Entry);
		}
	}
	StatusText = !bWasSuccessful ? FText::Format(LOCTEXT("SearchFailed", "{0} search failed."), GetServiceName())
		: Results.Num() == 0 ? FText::Format(LOCTEXT("NoGames", "No games found on {0}. Host one!"), GetServiceName())
		: FText::Format(LOCTEXT("Found", "{0} {0}|plural(one=game,other=games) found on {1}."), Results.Num(), GetServiceName());
	UE_LOG(LogArena, Log, TEXT("Find sessions: %s, %d Arena games (%d total results)"), bWasSuccessful ? TEXT("ok") : TEXT("failed"),
		Results.Num(), Search.IsValid() ? Search->SearchResults.Num() : 0);
	OnSearchUpdated.Broadcast();
}

void UArenaOnline::JoinGame(int32 ResultIndex)
{
	if (Search.IsValid() && Search->SearchResults.IsValidIndex(ResultIndex))
	{
		JoinSearchResult(Search->SearchResults[ResultIndex]);
	}
}

void UArenaOnline::JoinSearchResult(const FOnlineSessionSearchResult& Result)
{
	IOnlineSessionPtr Sessions = GetSessions();
	if (!Sessions)
	{
		return;
	}
	// One session at a time: leave the current one first.
	if (Sessions->GetNamedSession(NAME_GameSession))
	{
		JoinAfterDestroy = Result;
		Sessions->DestroySession(NAME_GameSession);
		return;
	}
	StatusText = LOCTEXT("Joining", "Joining...");
	OnSearchUpdated.Broadcast();
	if (!Sessions->JoinSession(0, NAME_GameSession, Result))
	{
		OnJoinComplete(NAME_GameSession, EOnJoinSessionCompleteResult::UnknownError);
	}
}

void UArenaOnline::OnJoinComplete(FName SessionName, EOnJoinSessionCompleteResult::Type Result)
{
	FString Url;
	IOnlineSessionPtr Sessions = GetSessions();
	if (Result != EOnJoinSessionCompleteResult::Success || !Sessions || !Sessions->GetResolvedConnectString(SessionName, Url))
	{
		StatusText = Result == EOnJoinSessionCompleteResult::SessionIsFull ? LOCTEXT("Full", "That game is full.")
			: LOCTEXT("JoinFailed", "Couldn't join that game.");
		UE_LOG(LogArena, Warning, TEXT("Join session failed (%d)"), static_cast<int32>(Result));
		OnSearchUpdated.Broadcast();
		return;
	}
	UE_LOG(LogArena, Log, TEXT("Joining session at %s"), *Url);
	if (APlayerController* PC = GetGameInstance()->GetFirstLocalPlayerController())
	{
		PC->ClientTravel(Url, TRAVEL_Absolute);
	}
}

void UArenaOnline::OnInviteAccepted(bool bWasSuccessful, int32 ControllerId, FUniqueNetIdPtr UserId, const FOnlineSessionSearchResult& Invite)
{
	UE_LOG(LogArena, Log, TEXT("Invite accepted: %s"), bWasSuccessful ? TEXT("joining") : TEXT("invalid"));
	if (bWasSuccessful && Invite.IsValid())
	{
		JoinSearchResult(Invite);
	}
}

#undef LOCTEXT_NAMESPACE
