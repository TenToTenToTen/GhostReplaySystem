/*
* Copyright 2025 TenToTen, All Rights Reserved.
*/

#include "GhostPlayerController.h"
#include "HAL/PlatformFilemanager.h"
#include "BloodStainSubsystem.h"
#include "BloodStainSystem.h"
#include "Engine/ActorChannel.h"

void AGhostPlayerController::Client_ReceiveReplayChunk_Implementation(AReplayActor* TargetReplayActor, int32 ChunkIndex, const TArray<uint8>& DataChunk, bool bIsLastChunk)
{
	if (TargetReplayActor)
	{
		TargetReplayActor->ProcessReceivedChunk(ChunkIndex, DataChunk, bIsLastChunk);
	}
}



void AGhostPlayerController::Server_ReportReplayFileCacheStatus_Implementation(AReplayActor* TargetReplayActor,
	bool bClientHasFile)
{
	if (TargetReplayActor)
	{
		TargetReplayActor->UpdateClientCacheStatus(this, bClientHasFile);
	}
}

void AGhostPlayerController::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);

    if (bIsUploading)
    {
        if (!UploadFileHandle)
        {
            UE_LOG(LogBloodStain, Error, TEXT("Upload stopped: File handle is invalid."));
            bIsUploading = false;
            return;
        }

    	if (!NetConnection)
    	{
    		return;
    	}

    	UActorChannel* Channel = NetConnection->FindActorChannelRef(this);
    	if (!Channel)
    	{
    		return;
    	}

    	const int32 ReliableBufferLimit = RELIABLE_BUFFER / 2;
    	const int32 MaxChunkSize = NetMaxConstructedPartialBunchSizeBytes / 2;
    	const int32 MaxChunksPerFrame = 4;
    	int32 MaxBytesToSendThisFrame = TotalFileSize; 
    	
		const float TotalTimeSinceLastTransfer = DeltaSeconds + AccumulatedTickTime;
		if (RateLimitMbps > 0)
		{
			const float BytesPerSecond = (RateLimitMbps * 1024 * 1024) / 8.0f;
			MaxBytesToSendThisTick = FMath::Max(1, static_cast<int32>(TotalTimeSinceLastTransfer * BytesPerSecond));
		}
    	
        int32 BytesSentThisFrame = 0;
        int32 ChunksSentThisFrame = 0;

        while (BytesSent < TotalFileSize &&
        	Channel->NumOutRec < ReliableBufferLimit &&
        	 BytesSentThisFrame < MaxBytesToSendThisFrame &&
        	 ChunksSentThisFrame < MaxChunksPerFrame)
        {
            int64 BytesToRead = FMath::Min(static_cast<int64>(ChunkSize), TotalFileSize - BytesSent);

        	const int32 RemainingBytesThisFrame = MaxBytesToSendThisFrame - BytesSentThisFrame;
        	BytesToRead = FMath::Min(BytesToRead, (int64)RemainingBytesThisFrame);

        	if (BytesToRead <= 0)
        	{
        		// No more bytes to read this frame or limited by the tick limit
        		break;
        	}
        	
        	TArray<uint8> ChunkBuffer;
        	ChunkBuffer.SetNumUninitialized(BytesToRead);
        	
        	if (UploadFileHandle->Read(ChunkBuffer.GetData(), BytesToRead))
        	{
        		Server_SendFileChunk(ChunkBuffer);
                
        		BytesSent += BytesToRead;
        		BytesSentThisFrame += BytesToRead;
        		ChunksSentThisFrame++;
        	}
            else
            {
                UE_LOG(LogBloodStain, Error, TEXT("Failed to read chunk from file %s. Aborting upload."), *UploadFilePath);
                bIsUploading = false;
                UploadFileHandle.Reset();
                return;
            }
        }

    	// If we sent any bytes this frame, reset the accumulated tick time
    	if (BytesSentThisFrame > 0)
    	{
    		UE_LOG(LogBloodStain, Log, TEXT("File Upload Tick: Sent %d bytes in %d chunks. Total sent: %lld / %lld. (NumOutRec: %d)"),
				BytesSentThisFrame, ChunksSentThisFrame, BytesSent, TotalFileSize, Channel->NumOutRec);
    		AccumulatedTickTime = 0.f;
    	}
    	else
    	{
    		AccumulatedTickTime = TotalTimeSinceLastTransfer;
    	}
    	
        AccumulatedTickTime = 0.f;

        if (BytesSent >= TotalFileSize)
        {
            UE_LOG(LogBloodStain, Log, TEXT("File upload completed for %s."), *UploadHeader.FileName.ToString());
            bIsUploading = false;
            UploadFileHandle.Reset();
            Server_EndFileUpload();
        }
    }
}

void AGhostPlayerController::StartFileUpload(const FString& FilePath, const FRecordHeaderData& Header)
{
	if (GetLocalRole() != ENetRole::ROLE_AutonomousProxy)
	{
		UE_LOG(LogBloodStain, Warning, TEXT("StartFileUpload can only be called on an owning client."));
		return;
	}

	if (bIsUploading)
	{
		UE_LOG(LogBloodStain, Warning, TEXT("Already uploading a file. New request for %s ignored."), *Header.FileName.ToString());
		return;
	}

	UploadFileHandle.Reset(FPlatformFileManager::Get().GetPlatformFile().OpenRead(*FilePath));
	if (!UploadFileHandle)
	{
		UE_LOG(LogBloodStain, Error, TEXT("Failed to open file for upload: %s"), *FilePath);
		return;
	}

	TotalFileSize = UploadFileHandle->Size();
	BytesSent = 0;
	UploadFilePath = FilePath;
	UploadHeader = Header;

	// Notify the server to begin the upload process
	Server_BeginFileUpload(Header, TotalFileSize);

	// Tick transfer flag to true
	bIsUploading = true;
	UE_LOG(LogBloodStain, Log, TEXT("Starting file upload for %s. Size: %lld bytes."), *Header.FileName.ToString(), TotalFileSize);
}

void AGhostPlayerController::Server_BeginFileUpload_Implementation(const FRecordHeaderData& Header, int64 FileSize)
{
	if (UWorld* World = GetWorld())
	{
		if (UBloodStainSubsystem* Subsystem = World->GetGameInstance()->GetSubsystem<UBloodStainSubsystem>())
		{
			Subsystem->HandleBeginFileUpload(this, Header, FileSize);
		}
	}
}

void AGhostPlayerController::Server_SendFileChunk_Implementation(const TArray<uint8>& ChunkData)
{
	if (UWorld* World = GetWorld())
	{
		if (UBloodStainSubsystem* Subsystem = World->GetGameInstance()->GetSubsystem<UBloodStainSubsystem>())
		{
			Subsystem->HandleReceiveFileChunk(this, ChunkData);
		}
	}
}

void AGhostPlayerController::Server_EndFileUpload_Implementation()
{
	if (UWorld* World = GetWorld())
	{
		if (UBloodStainSubsystem* Subsystem = World->GetGameInstance()->GetSubsystem<UBloodStainSubsystem>())
		{
			Subsystem->HandleEndFileUpload(this);
		}
	}
}
