/*
* Copyright 2025 TenToTen, All Rights Reserved.
*/


#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "ReplayActor.h"
#include "GhostPlayerController.generated.h"

/**
 * 
 */
UCLASS()
class BLOODSTAINSYSTEM_API AGhostPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	UFUNCTION(Client, Reliable)
	void Client_ReceiveReplayChunk(AReplayActor* TargetReplayActor, int32 ChunkIndex, const TArray<uint8>& DataChunk, bool bIsLastChunk);

	UFUNCTION(Server, Reliable)
	void Server_ReportReplayFileCacheStatus(AReplayActor* TargetReplayActor, bool bClientHasFile);

	/** [Client-side] Start sending local replay file to the server, called on Tick() */
	void StartFileUpload(const FString& FilePath, const FRecordHeaderData& Header);
	
protected:
	virtual void Tick(float DeltaSeconds) override;

private:
	/** [Server RPC] 파일 업로드 시작을 서버에 알립니다. */
	UFUNCTION(Server, Reliable)
	void Server_BeginFileUpload(const FRecordHeaderData& Header, int64 FileSize);

	/** [Server RPC] 파일 데이터 청크를 서버로 전송합니다. */
	UFUNCTION(Server, Reliable)
	void Server_SendFileChunk(const TArray<uint8>& ChunkData);

	/** [Server RPC] 파일 전송 완료를 서버에 알립니다. */
	UFUNCTION(Server, Reliable)
	void Server_EndFileUpload();
	
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
};
