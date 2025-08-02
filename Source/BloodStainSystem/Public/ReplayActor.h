/*
* Copyright 2025 TenToTen, All Rights Reserved.
*/


#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "BloodStainFileOptions.h"
#include "GhostData.h"
#include "OptionTypes.h"
#include "ReplayActor.generated.h"

class UPlayComponent;

/**
 * @brief An actor responsible for replaying recorded data. Acts as an 'Orchestrator' in a network environment.
 *
 * In a network game, this actor is spawned on the server and is responsible for:
 * 1. Sending the entire compressed replay data payload to all clients.
 * 2. Replicating the current playback time.
 * 
 * On clients, this actor receives the data, de-serializes it, and then spawns local-only 'Visual Actors' to display the replay.
 */

UCLASS()
class BLOODSTAINSYSTEM_API AReplayActor : public AActor
{
	GENERATED_BODY()

public:
	AReplayActor();

	/** Returns the PlayComponent SubObject. */
	UPlayComponent* GetPlayComponent() const;

	void InitializeReplayLocal(const FGuid& InPlaybackKey, const FRecordHeaderData& InHeader,
	                           const FRecordActorSaveData& InActorData,
	                           const FBloodStainPlaybackOptions& InOptions);

	/** [SERVER-ONLY] : Initializes the replay by sending a compressed payload to all clients.
	 * Called by the server's BloodStainSubsystem when starting a replay.
	 */
	void Server_InitializeReplayWithPayload(
		APlayerController* RequestingController,
		const FGuid& InPlaybackKey,
		const FBloodStainFileHeader& InFileHeader,
		const FRecordHeaderData& InRecordHeader,
		const TArray<uint8>& InCompressedPayload, const FBloodStainPlaybackOptions& InOptions
	);

	void ProcessReceivedChunk(int32 ChunkIndex, const TArray<uint8>& DataChunk, bool bIsLastChunk);
	
	void UpdateClientCacheStatus(APlayerController* ReportingController, bool bClientHasFile);

	void SetIsOrchestrator(bool bValue) { bIsOrchestrator = bValue; }

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BloodStain|Replay|Network")
	float RateLimitMbps = 0.5f;


	/** [CLIENT-ONLY] : Indicates if the client has the local file available for replay.
	 * Initialized as true by default, because the server will always have the file already.
	 */
	bool bHasLocalFile = true;

protected:
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** The component that handles the actual playback logic and visual updates. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "BloodStain|Replay")
	TObjectPtr<UPlayComponent> PlayComponent;

	/** The current playback time, replicated from the server to all clients. */
	UPROPERTY(ReplicatedUsing=OnRep_PlaybackTime)
	float ReplicatedPlaybackTime = 0.f;


	/** [CLIENT-ONLY] Assembles all received data chunks into a single payload buffer and then finalizes. */
	void Client_AssembleAndFinalize();

	/** [CLIENT-ONLY] Decompresses the final payload and spawns the visual actors for the replay. */
	void Client_FinalizeAndSpawnVisuals();
	void Client_FinalizeAndSpawnVisuals(const FRecordSaveData& AllReplayData);

	bool bIsOrchestrator = false;

private:
	
	/** Network Callback Functions and RPC implementations */

	/** [CLIENT-ONLY] Replication notification callback for ReplicatedPlaybackTime. */
	UFUNCTION()
	void OnRep_PlaybackTime();

	/** [RPC] Notifies all clients to prepare for receiving replay data. */
	UFUNCTION(NetMulticast, Reliable)
	void Multicast_InitializeForPayload(
		const FGuid& InPlaybackKey,
		const FBloodStainFileHeader& InFileHeader,
		const FRecordHeaderData& InRecordHeader,
		const FBloodStainPlaybackOptions& InOptions
	);

	/** [RPC] Sends a single chunk of the compressed replay data to all clients. */
	UFUNCTION(NetMulticast, Reliable)
	void Multicast_ReceivePayloadChunk(int32 ChunkIndex, const TArray<uint8>& DataChunk, bool bIsLastChunk);

	/** [SERVER-ONLY] Saves send status for each client */
	TMap<TWeakObjectPtr<class UNetConnection>, bool> ClientTransferRequiredMap;

	/** [SERVER-ONLY] */
	int32 NumClientsResponded = 0;
	
	/**
	 * @brief [CLIENT-ONLY]
	 * State variables used on the client during the data reception and processing phase.
	 * These are populated by the Multicast_InitializeForPayload RPC and are used to
	 * assemble the final data payload from incoming chunks.
	 */
	FGuid Client_PlaybackKey;
	FBloodStainFileHeader Client_FileHeader;
	FRecordHeaderData Client_RecordHeader;
	FBloodStainPlaybackOptions Client_PlaybackOptions;
	TArray<uint8> Client_ReceivedPayloadBuffer;
	TMap<int32, TArray<uint8>> Client_PendingChunks;
	int32 Client_ReceivedChunks = 0;
	int32 Client_ExpectedChunks = 0;

	/**
	 * @brief [CLIENT-ONLY]
	 * List of visual-only actors spawned locally by this orchestrator after
	 * the data has been fully received and processed.
	 */
	UPROPERTY()
	TArray<TObjectPtr<AReplayActor>> Client_SpawnedVisualActors;
	
	/** [SERVER-ONLY] Payload data that is being sent */
	TArray<uint8> Server_CurrentPayload;

	/** [SERVER-ONLY] Total byte that has been sent so far */
	int32 Server_BytesSent = 0;

	/** [SERVER-ONLY] Indicates if it's sending data */
	bool bIsTransferInProgress = false;

	/** [SERVER-ONLY] Accumulated tick time waiting for data sent */
	float Server_AccumulatedTickTime = 0.f;

	int32 Server_CurrentChunkIndex = 0;
	
	/** [SERVER-ONLY] Process sending data in every tick */
	void Server_TickTransfer(float DeltaSeconds);

	/** [SERVER-ONLY] Process Playing Replay data every tick */
	void Server_TickPlayback(float DeltaSeconds);

	/** [Client-ONLY] Saves the replay data locally if it doesn't already exist. */
	void SaveReplayLocallyIfNotExists(const FRecordSaveData& SaveData, const FRecordHeaderData& Header,
	                                  const FBloodStainFileOptions& Options);
};
