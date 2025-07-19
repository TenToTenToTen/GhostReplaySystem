/*
* Copyright 2025 TenToTen, All Rights Reserved.
*/


#pragma once

#include "CoreMinimal.h"
#include "BloodStainActor.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "GhostData.h"
#include "BloodStainFileOptions.h" 
#include "BloodStainSubsystem.generated.h"

class AReplayActor;
class URecordComponent;
class UReplayTerminatedActorManager;
struct FBloodStainRecordOptions;
struct FGameplayTagContainer;


DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnBuildRecordingHeader, FName, GroupName);

/** @brief Recording group for one or more actors, saved as a single file.
 * 
 *	manages the spawn point, recording options, and active recorders.
 */
USTRUCT()
struct FBloodStainRecordGroup
{
	GENERATED_BODY()

	UPROPERTY()
	float GroupStartTime = 0.f;
	
	/** Transform at which this group will be spawned for Replay */
	UPROPERTY()
	FTransform SpawnPointTransform;

	/** Recording options applied to this group */
	UPROPERTY()
	FBloodStainRecordOptions RecordOptions;

	/** Map of actors currently being recorded to their URecordComponent instances */
	UPROPERTY()
	TMap<TObjectPtr<AActor>, TObjectPtr<URecordComponent>> ActiveRecorders;
};

/** @brief Playback group: tracks active replay actors for a single replay session.
 */
USTRUCT()
struct FBloodStainPlaybackGroup
{
	GENERATED_BODY()

	/** Set of currently active replay actors */
	UPROPERTY()
	TSet<TObjectPtr<AReplayActor>> ActiveReplayers;
};



/**
 * @brief BloodStain recording and playback subsystem.
 *
 * A GameInstanceSubsystem responsible for:
 *  - Real-time recording of actor and component transforms
 *  - Transform quantization and compression based on user settings
 *  - Saving and loading replay data to local files with header/body caching
 *  - Exposing Blueprint-callable APIs for recording and replay control
 */
UCLASS(Config=Game)
class BLOODSTAINSYSTEM_API UBloodStainSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

	UBloodStainSubsystem();
public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	/**
	 *  @brief Starts recording a single actor into a recording group.
	 *  
	 *  This function finds or creates a recording group with the specified GroupName and Options,
	 *  then attaches a URecordComponent to the TargetActor to begin capturing data.
	 *  If the actor is already being recorded in the group, the function will fail.
	 *  
	 *  @param TargetActor    The actor to be recorded.
	 *  @param RecordOptions        Configuration for recording (e.g., duration, sampling interval). Applied only if the group is new.
	 *  @return True if recording starts successfully; false if the actor is null, already being recorded,
	 *          or if the RecordComponent fails to be created.
	 */
	UFUNCTION(BlueprintCallable, Category="BloodStain|Record")
	bool StartRecording(AActor* TargetActor, FBloodStainRecordOptions RecordOptions = FBloodStainRecordOptions());

	/**
	 *  @brief Starts recording multiple actors into the same recording group using the same options.
	 *  
	 *  This function iterates through the TargetActors array and calls StartRecording for each one.
	 *  Useful for conveniently starting a recording session with multiple actors.
	 *  
	 *  @param TargetActors   An array of actors to be recorded.
	 *  @param RecordOptions        Recording configuration applied to the group (if new) and all actors.
	 *  @return True if at least one actor in the array started recording successfully; false otherwise.
	 */
	UFUNCTION(BlueprintCallable, Category="BloodStain|Record")
	bool StartRecordingWithActors(TArray<AActor*> TargetActors, FBloodStainRecordOptions RecordOptions = FBloodStainRecordOptions());
	
	/**
	 *  @brief Stops the entire recording session for the specified group then saves the data.
	 *  
	 *  Finalizes the recording session, gathering data from all currently active recorders
	 *  and any previously terminated actors (managed by ReplayTerminatedActorManager) within the group.
	 *  After saving data, All resources associated with the group are cleaned up and removed.
	 *  
	 *  @param GroupName           The name of the recording group to stop. If NAME_None, the default group is used.
	 *  @param bSaveRecordingData  If true, the aggregated data is serialized and saved to a file. If false, all data is discarded.
	 *  @see StopRecordComponent
	 */
	UFUNCTION(BlueprintCallable, Category="BloodStain|Record")
	void StopRecording(FName GroupName = NAME_None, bool bSaveRecordingData = true);
	
	/**
	 *  @brief Stops recording for a single actor within a group, typically when the actor is destroyed.
	 *  
	 *  Unlike StopRecording, this does NOT immediately save a file. Instead, the actor's recorded data is
	 *  handed off to the ReplayTerminatedActorManager to be held until the entire group session is finalized
	 *  via StopRecording.
	 *  
	 *  @param RecordComponent     The component on the actor that should stop recording.
	 *  @param bSaveRecordingData  If true, the actor's data is preserved for the final save file. If false, it's discarded.
	 *  @see StopRecording
	 */
	UFUNCTION(BlueprintCallable, Category="BloodStain|Record")
	void StopRecordComponent(URecordComponent* RecordComponent, bool bSaveRecordingData = true);
	
	/**
	 *  @brief Starts a replay using a BloodStainActor instance in the world.
	 *  A user-friendly wrapper that calls StartReplayFromFile with info from the actor.
	 *  
	 *  @param BloodStainActor The actor containing the replay info.
	 *  @param OutGuid         Returns the unique ID of the new playback session.
	 *  @return True on success, false otherwise.
	 */
	UFUNCTION(BlueprintCallable, Category="BloodStain|Replay")
	bool StartReplayByBloodStain(ABloodStainActor* BloodStainActor, FGuid& OutGuid);

	/**
	 *  @brief Starts a replay directly from a file.
	 *  Loads the replay data from disk (if not cached) and spawns replay actors.
	 *  
	 *  TODO : Make this file I/O asynchronous in order to avoid hitches.
	 *  
	 *  @param FileName          The name of the replay file.
	 *  @param LevelName         The level where the replay was recorded.
	 *  @param InPlaybackOptions Playback settings (rate, looping, etc.).
	 *  @param OutGuid           Returns the unique ID of the new playback session.
	 *  @return True on success, false otherwise.
	 */
	UFUNCTION(BlueprintCallable, Category="BloodStain|Replay")
	bool StartReplayFromFile(const FString& FileName, const FString& LevelName, FGuid& OutGuid, FBloodStainPlaybackOptions InPlaybackOptions = FBloodStainPlaybackOptions());

	UFUNCTION(BlueprintCallable, Category="BloodStain|Replay")
	bool IsPlaying(const FGuid& InPlaybackKey) const;
	
	/**
	 *  @brief Forcefully stops an entire replay session identified by its key.
	 *  
	 *  Immediately destroys all actors within the group and removes the session from management from the subsystem's management.
	 *  
	 *  @param PlaybackKey The unique identifier of the replay session to be stopped.
	 *  @see StopReplayPlayComponent
	 */
	UFUNCTION(BlueprintCallable, Category="BloodStain|Replay")
	void StopReplay(FGuid PlaybackKey);
	
	/**
	 *  @brief Stops and cleans up a single replay actor and its associated PlayComponent.
	 *  Called internally when an actor's playback finishes. If it's the last remaining actor,
	 *  this function will then call StopReplay to terminate the empty session.
	 *  
	 *  @param GhostActor The specific replay actor that should be stopped and destroyed.
	 *  @see StopReplay
	 */
	UFUNCTION(BlueprintCallable, Category="BloodStain|Replay")
	void StopReplayPlayComponent(AReplayActor* GhostActor);
	
	/**
	 *  @brief Notifies the recording system that a mesh component has been attached to a recorded actor.
	 *  This must be called from game logic to ensure components like weapons or equipment are correctly recorded.
	 *  
	 *  @param TargetActor    The actor that is being recorded.
	 *  @param NewComponent   The UMeshComponent that was just attached.
	 */
	UFUNCTION(BlueprintCallable, Category="BloodStain|Record")
	void NotifyComponentAttached(AActor* TargetActor, UMeshComponent* NewComponent);

	/**
	 *  @brief Notifies the recording system that a mesh component has been detached from a recorded actor.
	 *  This must be called from game logic to ensure the component's removal is correctly recorded.
	 *  
	 *  @param TargetActor       The actor that is being recorded.
	 *  @param DetachedComponent The UMeshComponent that was just detached.
	 */
	UFUNCTION(BlueprintCallable, Category="BloodStain|Record")
	void NotifyComponentDetached(AActor* TargetActor, UMeshComponent* DetachedComponent);
	
public:
	/**
	 *	Finds all replay files for a given level and loads their headers into the cache.
	 *  Note: This will clear all previously cached header data before loading. 
	 *
	 *	@param LevelName The name of the level to search for replay files. If empty, uses the current level.
	 */
	UFUNCTION(BlueprintCallable, Category="BloodStain|File")
	void LoadAllHeadersInLevel(const FString& LevelName);

	/**
	 *  Loads full replay data (header) for a file, loading it from disk it not already cached
	 *  You may use this to quickly search for the header data before spawning a BloodStainActor.
	 *  @see SpawnBloodStain
	 */
	UFUNCTION(BlueprintCallable, Category="BloodStain|File")
	bool FindOrLoadRecordHeader(const FString& FileName, const FString& LevelName, FRecordHeaderData& OutRecordHeaderData);
	
	/** Loads full replay data (body) for a file, loading it from disk it not already cached */
	UFUNCTION(BlueprintCallable, Category="BloodStain|File")
	bool FindOrLoadRecordBodyData(const FString& FileName, const FString& LevelName, FRecordSaveData& OutData);
	
	/** Returns if the header data for a given replay file is currently in the memory cache. */
	UFUNCTION(BlueprintCallable, Category="BloodStain|File")
	bool IsFileHeaderLoaded(const FString& FileName);

	/** Returns if the full body data for a given replay file is currently in the memory cache. */
	UFUNCTION(BlueprintCallable, Category="BloodStain|File")
	bool IsFileBodyLoaded(const FString& FileName);
	
	/** Gets a read-only reference to the map of cached replay headers. */
	UFUNCTION(BlueprintCallable, Category="BloodStain|File")
	const TMap<FString, FRecordHeaderData>& GetCachedHeaders();

	// TODO UISOO
	UFUNCTION(BlueprintCallable, Category="BloodStain|File")
	TArray<FRecordHeaderData> GetTestCachedHeaders(FGameplayTagContainer GameplayTagContainer);
	
	/** @return The complete absolute file path in the project's standard save directory. */
	UFUNCTION(BlueprintCallable, Category="BloodStain|File")
	FString GetFullFilePath(const FString& FileName, const FString& LevelName) const;
public:
	/** Spawns a BloodStainActor to the ground using the file name and level name. */
	UFUNCTION(BlueprintCallable, Category="BloodStain|BloodStainActor")
	ABloodStainActor* SpawnBloodStain(const FString& FileName, const FString& LevelName);

	/** Scans the current level's save directory and spawns all BloodStainActors for every replay file found */
	UFUNCTION(BlueprintCallable, Category="BloodStain|BloodStainActor")
	TArray<ABloodStainActor*> SpawnAllBloodStainInLevel();
	
public:
	/**
 	 * @brief Sets the default material to be used for "ghost" actors during replay.
 	 * @param InMaterial The material instance to use for replay actors.
 	 */
	UFUNCTION(BlueprintCallable, Category="BloodStain|Replay")
	void SetDefaultMaterial(UMaterialInterface* InMaterial) { GhostMaterial = InMaterial; }

	/** @return The currently set default ghost material. */
	UFUNCTION(BlueprintCallable, Category="BloodStain|Replay")
	UMaterialInterface* GetDefaultMaterial() const { return GhostMaterial; }
	
	UFUNCTION(BlueprintCallable, Category="BloodStain|File")
	void SetFileSaveOptions(const FBloodStainFileOptions& InOptions);
	
private:
	/** The core implementation for spawning an ABloodStainActor at a specific transform. */
	ABloodStainActor* SpawnBloodStain_Internal(const FVector& Location, const FRotator& Rotation, const FString& FileName, const FString& LevelName);

	/**
	 * @brief The core implementation for initiating a replay session.
	 * Takes fully loaded replay data and spawns all necessary AReplayActor instances,
	 * attaching and initializing a UPlayComponent to each one to begin playback.
	 */
	bool StartReplay_Internal(const FRecordSaveData& RecordSaveData, const FBloodStainPlaybackOptions& InPlaybackOptions, FGuid& OutGuid);

	/** Internal helper to package actor-specific data into the final save format.
	 *  Aggregates multiple FRecordActorSaveData instances into a single FRecordSaveData.
	 */
	FRecordSaveData ConvertToSaveData(TArray<FRecordActorSaveData>& RecordActorDataArray, const FName& GroupName);

	/** @return true if a recording group is still valid */
	bool IsValidReplayGroup(const FName& GroupName);
	
	/** Iterates through all active recording groups and removes any that are no longer valid. */
	void CleanupInvalidRecordGroups();

	FReplayCustomUserData GetReplayCustomUserData(const FName& GroupName);
	
public:
	/**
	 *  @brief Global options for saving replay files (e.g., quantization, compression).
	 *  Can be set from Blueprints.
	 */
	UPROPERTY(BlueprintReadOnly, VisibleAnywhere, Config, Category="BloodStain|File")
	FBloodStainFileOptions FileSaveOptions;

	UPROPERTY(BlueprintAssignable, Category = "BloodStain|File")
	FOnBuildRecordingHeader OnBuildRecordingHeader;

	UFUNCTION(BlueprintCallable, Category="BloodStain|File")
	void SetReplayCustomUserData(const FReplayCustomUserData& ReplayCustomUserData, FName GroupName = NAME_None);
	
protected:
	/** The ABloodStainActor class to spawn, loaded from a hardcoded path in the constructor. */
	UPROPERTY()
	TSubclassOf<ABloodStainActor> BloodStainActorClass;
	
private:
	/** Manages all currently active recording sessions identified by its key */
	UPROPERTY(Transient)
	TMap<FName, FBloodStainRecordGroup> BloodStainRecordGroups;	
	
	/** Manages all currently active replay sessions identified by its key */
	UPROPERTY(Transient)
	TMap<FGuid, FBloodStainPlaybackGroup> BloodStainPlaybackGroups;

	/** Cached replay data's headers */
	UPROPERTY()
	TMap<FString, FRecordHeaderData> CachedHeaders;
	
	/** Cached full replay datas */
	UPROPERTY()
	TMap<FString, FRecordSaveData> CachedRecordings;

	/** Manages data from actors that were destroyed mid-recording, holding it until the session is saved. */
	UPROPERTY()
	TObjectPtr<UReplayTerminatedActorManager> ReplayTerminatedActorManager;
	
	/** Default material used for "Replaying actors" if recorded material is null or bUseGhostMaterial is true */
	UPROPERTY()
	TObjectPtr<UMaterialInterface> GhostMaterial;

	TMap<FName, FReplayCustomUserData> ReplayCustomUserDataMap;
	
	/** Default group name to use if one is not specified when starting a recording. */
	static FName DefaultGroupName;

	/** Distance to trace downwards to find the ground when spawning a BloodStainActor. */
	static float LineTraceLength;

	// Experimental
public:
	UFUNCTION(BlueprintCallable, Category="BloodStain|Experimental", meta=(ToolTip="This is Experimental Function"))
	void AddToPendingGroup(const FName& GroupName, AActor* Actor);

	UFUNCTION(BlueprintCallable, Category="BloodStain|Experimental", meta=(ToolTip="This is Experimental Function"))
	void AddToPendingGroupWithActors(const FName& GroupName, TArray<AActor*> Actors);

	UFUNCTION(BlueprintCallable, Category="BloodStain|Experimental", meta=(ToolTip="This is Experimental Function"))
	void RemoveFromPendingGroup(const FName& GroupName, AActor* Actor);
	
	UFUNCTION(BlueprintCallable, Category="BloodStain|Experimental", meta=(ToolTip="This is Experimental Function"))
	void RemoveFromPendingGroupWithActors(const FName& GroupName, TArray<AActor*> Actors);

	UFUNCTION(BlueprintCallable, Category="BloodStain|Experimental", meta=(ToolTip="This is Experimental Function"))
	void StartRecordingWithPendingGroup(FBloodStainRecordOptions BloodStainRecordOptions);

private:
	TMap<FName, TSet<TWeakObjectPtr<AActor>>> PendingGroups;
};
