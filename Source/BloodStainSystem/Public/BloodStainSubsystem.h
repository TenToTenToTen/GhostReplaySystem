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

class AGhostPlayerController;
class ABloodStainManager;
class AReplayActor;
class URecordComponent;
class UReplayTerminatedActorManager;
struct FBloodStainRecordOptions;
struct FGameplayTagContainer;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnBuildRecordingHeader, FName, GroupName);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnBloodStainReadyOnClient, ABloodStainActor*, ReadyActor);

struct FIncomingClientFile
{
	FRecordHeaderData Header;
	TArray<uint8> FileBuffer;
	int64 ExpectedSize;
};

/** @brief Recording group for one or more actors, saved as a single file.
 * 
 *	manages the spawn point, recording options, and active recorders.
 */
USTRUCT()
struct FBloodStainRecordGroup
{
	GENERATED_BODY()

	// Based On World Time
	UPROPERTY()
	float WorldBaseGroupStartTime = 0.f;

	UPROPERTY()
	float WorldBaseGroupEndTime = 0.f;
	
	/** Transform at which this group will be spawned for Replay */
	UPROPERTY()
	FTransform SpawnPointTransform;

	/** Recording options applied to this group */
	UPROPERTY()
	FBloodStainRecordOptions RecordOptions;

	/** Map of actors currently being recorded to their URecordComponent instances */
	UPROPERTY()
	TMap<TObjectPtr<AActor>, TObjectPtr<URecordComponent>> ActiveRecorders;

	/** The Actor represented to specify the SpawnPointTransform position
	 *  If null, it is set to the middle position of the Actors. */
	UPROPERTY()
	TWeakObjectPtr<AActor> RecordingMainActor;
};

/** @brief Playback group: tracks active replay actors for a single replay session.
 */
USTRUCT(BlueprintType)
struct FBloodStainPlaybackGroup
{
	GENERATED_BODY()

	/** Set of currently active replay actors */
	UPROPERTY(BlueprintReadOnly, VisibleAnywhere, Category="BloodStain|Replay")
	TArray<TObjectPtr<AReplayActor>> ActiveReplayers;
};

USTRUCT()
struct FPendingActorData
{
	GENERATED_BODY()
	UPROPERTY()
	TWeakObjectPtr<AActor> Actor = nullptr;
		
	UPROPERTY()
	FInstancedStruct InstancedStruct = FInstancedStruct();
};

USTRUCT()
struct FPendingGroup
{
	GENERATED_BODY()

	UPROPERTY()
	TMap<uint32, FPendingActorData> ActorData;

	UPROPERTY()
	FBloodStainRecordOptions RecordOptions = FBloodStainRecordOptions();

	UPROPERTY()
	TWeakObjectPtr<AActor> RecordingMainActor = nullptr;
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
	 *  @param RequestingController
	 *  @param BloodStainActor The actor containing the replay info.
	 *  @param OutGuid         Returns the unique ID of the new playback session.
	 *  @return True on success, false otherwise.
	 */
	UFUNCTION(BlueprintCallable, Category="BloodStain|Replay")
	bool StartReplayByBloodStain(APlayerController* RequestingController, ABloodStainActor* BloodStainActor, FGuid& OutGuid);

	/**
	 *  @brief Starts a replay directly from a file.
	 *  Loads the replay data from disk (if not cached) and spawns replay actors.
	 *  
	 *  TODO : Make this file I/O asynchronous in order to avoid hitches.
	 *  
	 *  @param RequestingController
	 *  @param FileName          The name of the replay file.
	 *  @param LevelName         The level where the replay was recorded.
	 *  @param PlaybackOptions Playback settings (rate, looping, etc.).
	 *  @param OutGuid           Returns the unique ID of the new playback session.
	 *  @return True on success, false otherwise.
	 */
	UFUNCTION(BlueprintCallable, Category="BloodStain|Replay")
	bool StartReplayFromFile(APlayerController* RequestingController, const FString& FileName, const FString& LevelName, FGuid& OutGuid, FBloodStainPlaybackOptions
	                         PlaybackOptions = FBloodStainPlaybackOptions());

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

	UFUNCTION(BlueprintCallable, Category="BloodStain|Replay", meta=(BlueprintPure = false))
	bool GetPlaybackGroup(const FGuid& InGuid, FBloodStainPlaybackGroup& OutBloodStainPlaybackGroup);
	
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

	/** Set Main Actor for specify the SpawnPointTransform position
	 *  If null, it is set to the middle position of the Actors. */
	UFUNCTION(BlueprintCallable, Category="BloodStain|Record")
	void SetRecordingGroupMainActor(AActor* TargetActor, FName GroupName = NAME_None);

	UFUNCTION()
	void HandleBloodStainReady(ABloodStainActor* ReadyActor);
public:
	/**
	 *	Finds all replay files for a given level and loads their headers into the cache.
	 *  Note: This will clear all previously cached header data before loading. 
	 *
	 *	@param LevelName The name of the level to search for replay files. If empty, uses the current level.
	 */
	UFUNCTION(BlueprintCallable, Category="BloodStain|File")
	int32 LoadAllHeadersInLevel(const FString& LevelName);

	UFUNCTION(BlueprintCallable, Category="BloodStain|File")
	int32 LoadAllHeadersInLevels(const TArray<FString>& LevelNames);
	
	/**
	 *	Finds all replay files and loads their headers into the cache.
	 */
	UFUNCTION(BlueprintCallable, Category="BloodStain|File")
	void LoadAllHeaders();
	
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
	bool IsFileHeaderLoaded(const FString& FileName, const FString& LevelName) const;

	/** Returns if the full body data for a given replay file is currently in the memory cache. */
	UFUNCTION(BlueprintCallable, Category="BloodStain|File")
	bool IsFileBodyLoaded(const FString& FileName, const FString& LevelName) const;
	
	/** Gets a read-only reference to the cached replay headers. */
	UFUNCTION(BlueprintCallable, Category="BloodStain|File")
	TArray<FRecordHeaderData> GetCachedHeaders() const;

	/**
	 * Gets a read-only reference to the cached replay headers filtered by tags
	 * @param FilterTags used to filter cached header data.
	 */
	UFUNCTION(BlueprintCallable, Category="BloodStain|File")
	TArray<FRecordHeaderData> GetCachedHeadersByTags(const FGameplayTagContainer& FilterTags) const ;

	// Clear Body Data (do not clear header)
	UFUNCTION(BlueprintCallable, Category="BloodStain|File")
	void ClearCachedBodyData(const FString& FileName, const FString& LevelName);
	
	// Clear Header & Body
	UFUNCTION(BlueprintCallable, Category="BloodStain|File")
	void ClearCachedData(const FString& FileName, const FString& LevelName);

	UFUNCTION(BlueprintCallable, Category="BloodStain|File")
	void ClearAllCachedBodyData();
	
	UFUNCTION(BlueprintCallable, Category="BloodStain|File")
	void ClearAllCachedData();
	
	UFUNCTION(BlueprintCallable, Category="BloodStain|File")
	bool DeleteFile(const FString& FileName, const FString& LevelName);

public:
	/** @return The complete absolute file path in the project's standard save directory. */
	UFUNCTION(BlueprintCallable, Category="BloodStain|File")
	FString GetFullFilePath(const FString& FileName, const FString& LevelName) const;

	/** @return The relative file path in the project's standard save directory. */
	UFUNCTION(BlueprintCallable, Category="BloodStain|File")
	FString GetRelativeFilePath(const FString& FileName, const FString& LevelName) const;

	/** If no files match, nothing is returned even if the directory exists. */
	UFUNCTION(BlueprintCallable, Category="BloodStain|File")
	TArray<FString> GetSavedLevelNames() const;

	UFUNCTION(BlueprintCallable, Category="BloodStain|File")
	TArray<FString> GetSavedFileNames(const FString& LevelName) const;
	
public:
	/** Spawns a BloodStainActor to the ground using the file name and level name. */
	UFUNCTION(BlueprintCallable, Category="BloodStain|BloodStainActor")
	void SpawnBloodStain(const FString& FileName, const FString& LevelName, const FBloodStainPlaybackOptions& PlaybackOptions);

	/** Scans the current level's save directory and spawns all BloodStainActors for every replay file found */
	UFUNCTION(BlueprintCallable, Category="BloodStain|BloodStainActor")
	TArray<ABloodStainActor*> SpawnAllBloodStainInLevel(const FBloodStainPlaybackOptions& PlaybackOptions);
	
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

	/** If GroupName is Name_None, Set GroupName to this */
	UFUNCTION(BlueprintCallable, Category="BloodStain|File")
	void SetDefaultGroupName(const FName& InDefaultGroupName) { DefaultGroupName = InDefaultGroupName; }
	
	UFUNCTION(BlueprintCallable, Category="BloodStain|File")
	void SetReplayUserGroupData(const FInstancedStruct& ReplayCustomUserData, FName GroupName = NAME_None);

	template <typename T>
	bool SetReplayUserGroupData(const T& InUserData, FName GroupName = NAME_None);

	FInstancedStruct GetReplayUserHeaderData(const FName& GroupName);

	void ClearReplayUserHeaderData(const FName& GroupName);
	
private:
	/** The core implementation for spawning an ABloodStainActor at a specific transform. */
	ABloodStainActor* SpawnBloodStain_Internal(const FVector& Location, const FRotator& Rotation, const FString& FileName, const FString& LevelName);
	
	/**
	 * @brief The core implementation for initiating a replay session in single-player mode.
	 * Takes fully loaded replay data and spawns all necessary AReplayActor instances,
	 * attaching and initializing a UPlayComponent to each one to begin playback.
	 */
	bool StartReplay_Standalone(const FRecordSaveData& RecordSaveData, const FBloodStainPlaybackOptions& PlaybackOptions, FGuid& OutGuid);

	/**
 	 * @brief Starts a replay session in networked mode.
 	 *
 	 * This function is intended for networked replay scenarios.
 	 * In network mode, each ReplayActor is responsible for deserializing, dequantizing, and decompressing its own data.
 	 */
	bool StartReplay_Networked(APlayerController* RequestingController, const FString& FileName, const FString& LevelName, const FBloodStainFileHeader
	                           & FileHeader, const FRecordHeaderData& RecordHeader, const TArray<uint8>& CompressedPayload, const
	                           FBloodStainPlaybackOptions& PlaybackOptions, FGuid& OutGuid);
	
	/** Internal helper to package actor-specific data into the final save format.
	 *  Aggregates multiple FRecordActorSaveData instances into a single FRecordSaveData.
	 */
	FRecordSaveData ConvertToSaveData(float EndTime, const FName& GroupName, const FName& FileName, const FName& LevelName, TArray<FRecordActorSaveData>& RecordActorDataArray);

	/** @return true if a recording group is still valid */
	bool IsValidReplayGroup(const FName& GroupName);
	
	/** Iterates through all active recording groups and removes any that are no longer valid. */
	void CleanupInvalidRecordGroups();

public:
	/** Send Replay file to the server from the client-side */
	void StopRecordingAndUploadToServer(FName GroupName);
	
	void HandleBeginFileUpload(AGhostPlayerController* Uploader, const FRecordHeaderData& Header, int64 FileSize);

	void HandleReceiveFileChunk(AGhostPlayerController* Uploader, const TArray<uint8>& ChunkData);

	void HandleEndFileUpload(AGhostPlayerController* Uploader);
private:
	TMap<TWeakObjectPtr<APlayerController>, FIncomingClientFile> IncomingFileTransfers;
	
public:
	/**
	 *  @brief Global options for saving replay files (e.g., quantization, compression).
	 *  Can be set from Blueprints.
	 */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Config, Category="BloodStain|File")
	FBloodStainFileOptions FileSaveOptions;

	UPROPERTY(BlueprintAssignable, Category = "BloodStain|File")
	FOnBuildRecordingHeader OnCompleteBuildRecordingHeader;

	/** Distance to trace downwards to find the ground when spawning a BloodStainActor. */
	static float LineTraceLength;

	UPROPERTY(BlueprintAssignable, Category = "BloodStain|Events")
	FOnBloodStainReadyOnClient OnBloodStainReady;
	
	TArray<TObjectPtr<ABloodStainActor>> BloodStainActors;
	
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

	/**
	 * Key is "LevelName/FileName" 
	 * Cached replay data's headers */
	UPROPERTY()
	TMap<FString, FRecordHeaderData> CachedHeaders;
	
	/**
	* Key is "LevelName/FileName"
	 * Cached full replay data */
	UPROPERTY()
	TMap<FString, FRecordSaveData> CachedRecordings;

	/** Manages data from actors that were destroyed mid-recording, holding it until the session is saved. */
	UPROPERTY()
	TObjectPtr<UReplayTerminatedActorManager> ReplayTerminatedActorManager;
	
	/** Default material used for "Replaying actors" if recorded material is null or bUseGhostMaterial is true */
	UPROPERTY()
	TObjectPtr<UMaterialInterface> GhostMaterial;
	
	/** Key is GroupName */
	TMap<FName, FInstancedStruct> ReplayUserHeaderDataMap;
	
	/** Default group name to use if one is not specified when starting a recording. */
	FName DefaultGroupName = TEXT("BloodStainReplay");
	
	// Experimental
	// Pending to Record & Start In an instant
public:
	UFUNCTION(BlueprintCallable, Category="BloodStain|Pending|Experimental", meta=(ToolTip="This is Experimental Function"))
	void AddToPendingGroup(AActor* Actor, FName GroupName = NAME_None);

	UFUNCTION(BlueprintCallable, Category="BloodStain|Pending|Experimental", meta=(ToolTip="This is Experimental Function"))
	void AddToPendingGroupWithActors(TArray<AActor*> Actors, FName GroupName = NAME_None);

	UFUNCTION(BlueprintCallable, Category="BloodStain|Pending|Experimental", meta=(ToolTip="This is Experimental Function"))
	void RemoveFromPendingGroup(AActor* Actor, FName GroupName = NAME_None);
	
	UFUNCTION(BlueprintCallable, Category="BloodStain|Pending|Experimental", meta=(ToolTip="This is Experimental Function"))
	void RemoveFromPendingGroupWithActors(TArray<AActor*> Actors, FName GroupName = NAME_None);

	UFUNCTION(BlueprintCallable, Category="BloodStain|Pending|Experimental", meta=(ToolTip="This is Experimental Function"))
	void StartRecordingWithPendingGroup(FBloodStainRecordOptions RecordOptions);

	UFUNCTION(BlueprintCallable, Category="BloodStain|Pending|Experimental", meta=(ToolTip="This is Experimental Function"))
	void SetPendingGroupMainActor(AActor* TargetActor, FName GroupName = NAME_None);

	UFUNCTION(BlueprintCallable, Category="BloodStain|Pending|Experimental", meta=(ToolTip="This is Experimental Function"))
	void SetPendingActorUserData(FName GroupName, AActor* Actor, const struct FInstancedStruct& InInstancedStruct);

	template <typename T>
	bool SetPendingActorUserData(FName GroupName, AActor* Actor, const T& InUserData); 

private:
	TMap<FName, FPendingGroup> PendingGroups;
};

template <typename T>
bool UBloodStainSubsystem::SetReplayUserGroupData(const T& InUserData, FName GroupName)
{
	static_assert(std::is_same_v<decltype(T::StaticStruct()), UScriptStruct*>, "T must be a USTRUCT with StaticStruct()");

	const UScriptStruct* ScriptStruct = T::StaticStruct();
	check(ScriptStruct);

	if (ScriptStruct == nullptr)
	{
		return false;
	}
	
	FInstancedStruct InstancedStruct = FInstancedStruct::Make(InUserData);

	if (!InstancedStruct.IsValid())
	{
		UE_LOG(LogTemp, Warning, TEXT("[URecordComponent::AcceptBuffer()] Invalid InstancedStruct passed."));
		InstancedStruct.Reset();
		return false;
	}
	
	ReplayUserHeaderDataMap.Add(GroupName, InstancedStruct);
	
	return true;
}

template <typename T>
bool UBloodStainSubsystem::SetPendingActorUserData(const FName GroupName, AActor* Actor, const T& InUserData)
{
	if (PendingGroups.Contains(GroupName))
	{
		FPendingGroup& PendingGroup = PendingGroups[GroupName];

		if (PendingGroup.ActorData.Contains(Actor->GetUniqueID()))
		{
			FPendingActorData& PendingActorData = PendingGroup.ActorData[Actor->GetUniqueID()];
			const FInstancedStruct InstancedStruct = FInstancedStruct::Make(InUserData);

			if (InstancedStruct.IsValid())
			{
				PendingActorData.InstancedStruct = InstancedStruct;
				return true;
			}
		}
	}

	return false;
}