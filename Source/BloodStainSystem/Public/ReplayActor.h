/*
* Copyright 2025 TenToTen, All Rights Reserved.
*/


#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GhostData.h"
#include "OptionTypes.h"
#include "ReplayActor.generated.h"

class UPlayComponent;

UCLASS()
class BLOODSTAINSYSTEM_API AReplayActor : public AActor
{
	GENERATED_BODY()
	
public:	
	AReplayActor();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;
	void InitializeReplayLocal(const FGuid& InPlaybackKey, const FRecordHeaderData& InHeader, const FRecordActorSaveData& InActorData, const FBloodStainPlaybackOptions& InOptions);
	
	/** Server only : Getting to send header and per actor replay data */
	void Server_InitializeReplay(
		const FGuid& InPlaybackKey,
		const FRecordHeaderData& InHeaderData,
		const FRecordActorSaveData& InActorData,
		const FBloodStainPlaybackOptions& InPlaybackOptions
	);

	UPROPERTY(ReplicatedUsing=OnRep_PlaybackTime)
	float ReplicatedPlaybackTime = 0.f;

	UFUNCTION()
	void OnRep_PlaybackTime();

	void Server_BuildDataChunks(const FRecordActorSaveData& InActorData);
	void Server_SendChunks();

	UFUNCTION(NetMulticast, Reliable)
	void Multicast_InitializeOnClients(const FGuid& InPlaybackKey, const FRecordHeaderData& InHeader, const FBloodStainPlaybackOptions& InOptions);

	UFUNCTION(NetMulticast, Reliable)
	void Multicast_ReceiveDataChunk(int32 ChunkIndex, int32 TotalChunks, const TArray<uint8>& DataChunk);

	void Client_FinalizeDataAndInitialize();
	
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UPlayComponent* GetPlayComponent() const;

	UFUNCTION(NetMulticast, Reliable)
	void Multicast_SendOriginalSize(int32 InSize);
	
protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Replay")
	TObjectPtr<UPlayComponent> PlayComponent;
	
private:
	/** Server only */
	float Server_PlaybackStartTime = 0.f;
	float Server_PlaybackDuration = 0.f;
	FBloodStainPlaybackOptions Server_PlaybackOptions;
	TArray<TArray<uint8>> Server_DataChunks; /** Server only : Serialized chunk data */

	TArray<uint8> CompressedBuffer;
	int32 Server_OriginalSize = 0;

	/** Client Only */
	FGuid Client_PlaybackKey;
	FRecordHeaderData Client_HeaderData;
	FBloodStainPlaybackOptions Client_PlaybackOptions;
	
	TArray<uint8> Client_ReceivedDataBuffer;
	int32 Client_ExpectedChunks = 0;
	int32 Client_ReceivedChunks = 0;

	
	int32 Client_OriginalSize = 0;
};