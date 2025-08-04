/*
* Copyright 2025 TenToTen, All Rights Reserved.
*/


#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "ReplayActor.h"
#include "GhostPlayerController.generated.h"

class ABloodStainActor;
/**
 * 
 */
UCLASS()
class BLOODSTAINSYSTEM_API AGhostPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	AGhostPlayerController();
	
	UFUNCTION(Client, Reliable)
	void Client_ReceiveReplayChunk(AReplayActor* TargetReplayActor, int32 ChunkIndex, const TArray<uint8>& DataChunk, bool bIsLastChunk);

	UFUNCTION(Server, Reliable)
	void Server_ReportReplayFileCacheStatus(AReplayActor* TargetReplayActor, bool bClientHasFile);

	UFUNCTION(Server, Reliable)
	void Server_SpawnBloodStain(const FString& FileName, const FString& LevelName, const FBloodStainPlaybackOptions& PlaybackOptions);

	/** [Client-side] Start sending local replay file to the server, called on Tick() */
	void StartFileUpload(const FString& FilePath, const FRecordHeaderData& Header);
	
protected:
	virtual void Tick(float DeltaSeconds) override;

private:
	/** [Server RPC] Notifies the server that a file upload is about to begin. */
	UFUNCTION(Server, Reliable)
	void Server_BeginFileUpload(const FRecordHeaderData& Header, int64 FileSize);

	/** [Server RPC] Sends a chunk of file data to the server. */
	UFUNCTION(Server, Reliable)
	void Server_SendFileChunk(const TArray<uint8>& ChunkData);

	/** [Server RPC] Notifies the server that the file transfer is complete. */
	UFUNCTION(Server, Reliable)
	void Server_EndFileUpload();
	
	UPROPERTY()
	TSubclassOf<ABloodStainActor> BloodStainActorClass;
	
	const float RateLimitMbps = 0.5f;
	int32 MaxBytesToSendThisTick = 1024 * 16; // 16 KB per tick
	int32 ChunkSize = 1024; // 1 KB per chunk

	FString UploadFilePath;
	FRecordHeaderData UploadHeader;
	TUniquePtr<IFileHandle> UploadFileHandle;
	int64 TotalFileSize = 0;
	int64 BytesSent = 0;
	float AccumulatedTickTime = 0.f;
	bool bIsUploading = false;

	// from DataChannel.h
	static const int32 NetMaxConstructedPartialBunchSizeBytes = 1024 * 64;
};
