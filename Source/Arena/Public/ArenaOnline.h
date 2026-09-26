#pragma once

#include "CoreMinimal.h"
#include "Interfaces/OnlineSessionInterface.h"
#include "OnlineSessionSettings.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "ArenaOnline.generated.h"

class FOnlineSessionSearch;

/** One game in the server browser. */
struct FArenaServerEntry
{
	FString HostName;
	FName MapId;
	int32 Players = 0;
	int32 MaxPlayers = 0;
	int32 Bots = 0;
	int32 PingMs = 0;
	int32 ResultIndex = INDEX_NONE;
};

/**
 * Online sessions through Unreal's Online Subsystem: Steam lobbies when Steam is
 * running (listed games, invites, Steam names, Steam networking), otherwise the
 * NULL subsystem, which finds games on the local network.
 */
UCLASS()
class ARENA_API UArenaOnline : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	static UArenaOnline* Get(const UObject* WorldContext);

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/** True when the Steam subsystem is up (Steam client running and logged in). */
	bool IsSteam() const;

	/** "Steam" or "LAN", for UI text. */
	FText GetServiceName() const;

	/** Creates a listed session, then opens the map as a listen server with TravelOptions. */
	void HostGame(const FString& TravelOptions, FName MapId, int32 Bots, bool bFriendsOnly);

	/** Server: keep the listed map and bot count current after a map change. */
	void UpdateListing(FName MapId, int32 Bots);

	void FindGames();
	bool IsSearching() const { return bSearching; }
	const TArray<FArenaServerEntry>& GetSearchResults() const { return Results; }
	void JoinGame(int32 ResultIndex);

	/** Leave or end the current session (call before returning to the menu). */
	void LeaveSession();

	/** Opens the Steam overlay's invite dialog for the current session. */
	bool ShowInviteUI();

	/** Short status for the UI ("Searching Steam...", "Couldn't join", ...). */
	FText GetStatusText() const { return StatusText; }

	/** Fired when a search finishes or its results change. */
	FSimpleMulticastDelegate OnSearchUpdated;

private:
	IOnlineSessionPtr GetSessions() const;
	void CreateSession();
	void OnCreateComplete(FName SessionName, bool bWasSuccessful);
	void OnDestroyComplete(FName SessionName, bool bWasSuccessful);
	void OnFindComplete(bool bWasSuccessful);
	void OnJoinComplete(FName SessionName, EOnJoinSessionCompleteResult::Type Result);
	void OnInviteAccepted(bool bWasSuccessful, int32 ControllerId, FUniqueNetIdPtr UserId, const FOnlineSessionSearchResult& Invite);
	void JoinSearchResult(const FOnlineSessionSearchResult& Result);
	void TravelToHostedGame();

	TSharedPtr<FOnlineSessionSearch> Search;
	TArray<FArenaServerEntry> Results;
	bool bSearching = false;
	FText StatusText;

	// Pending host request, kept while an old session is destroyed / the new one is created.
	FString PendingTravelOptions;
	FName PendingMap;
	int32 PendingBots = 0;
	bool bPendingFriendsOnly = false;
	bool bHostAfterDestroy = false;
	TOptional<FOnlineSessionSearchResult> JoinAfterDestroy;

	FDelegateHandle CreateHandle, DestroyHandle, FindHandle, JoinHandle, InviteHandle;
};
