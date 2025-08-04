/*
* Copyright 2025 TenToTen, All Rights Reserved.
*/


#include "ReplayActor.h"
#include "PlayComponent.h"
#include "BloodStainCompressionUtils.h"
#include "BloodStainFileUtils.h"
#include "QuantizationHelper.h"
#include "Net/UnrealNetwork.h"
#include "Engine/World.h"
#include "Engine/NetDriver.h"
#include "Engine/NetConnection.h"
#include "Engine/ActorChannel.h"
#include "Serialization/MemoryReader.h"
#include "BloodStainSystem.h"
#include "GhostPlayerController.h"

DECLARE_CYCLE_STAT(TEXT("AReplayActor Tick"), STAT_AReplayActor_Tick, STATGROUP_BloodStain);

AReplayActor::AReplayActor()
{
	TRACE_CPUPROFILER_EVENT_SCOPE_STR("AReplayActor::AReplayActor");
	
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;
	bAlwaysRelevant = true;
	SetReplicateMovement(false);
	
	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	RootComponent = Root;
	
	PlayComponent = CreateDefaultSubobject<UPlayComponent>(TEXT("PlayComponent"));
	PlayComponent->PrimaryComponentTick.bCanEverTick = true;
}

void AReplayActor::BeginPlay()
{
	TRACE_CPUPROFILER_EVENT_SCOPE_STR("AReplayActor::BeginPlay");
	Super::BeginPlay();
}


void AReplayActor::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	// UE_LOG(LogBloodStain, Log, TEXT("NetMode: %d, FirstPlayerController HasAuthority: %d"),
	//static_cast<int32>(GetNetMode()),
	//GetWorld() && GetWorld()->GetFirstPlayerController() ? (int32)GetWorld()->GetFirstPlayerController()->HasAuthority() : -1);
	if (bIsOrchestrator)
	{
		if (HasAuthority())
		{
			if (bIsTransferInProgress)
			{
				Server_TickTransfer(DeltaTime);
				return;
			}

			Server_TickPlayback(DeltaTime);
		}
	}
	else if (GetNetMode() == NM_Standalone) // Local Mode
	{
		if (PlayComponent && PlayComponent->IsComponentTickEnabled())
		{
			float ElapsedTime = 0.f;
			if (PlayComponent->CalculatePlaybackTime(ElapsedTime))
			{
				PlayComponent->UpdatePlaybackToTime(ElapsedTime);
			}
			else
			{
				Destroy();
			}
		}
	}
}

void AReplayActor::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE_STR("AReplayActor::GetLifetimeReplicatedProps");
	
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME_CONDITION(AReplayActor, ReplicatedPlaybackTime, COND_None);
}

UPlayComponent* AReplayActor::GetPlayComponent() const
{
	return PlayComponent;
}

void AReplayActor::InitializeReplayLocal(const FGuid& InPlaybackKey, const FRecordHeaderData& InHeader,
	const FRecordActorSaveData& InActorData, const FBloodStainPlaybackOptions& InOptions)
{
	TRACE_CPUPROFILER_EVENT_SCOPE_STR("AReplayActor::InitializeReplayLocal");
	PlayComponent->Initialize(InPlaybackKey, InHeader, InActorData, InOptions);
}

void AReplayActor::Server_InitializeReplayWithPayload(
	APlayerController* RequestingController, const FGuid& InPlaybackKey,
	const FBloodStainFileHeader& InFileHeader, const FRecordHeaderData& InRecordHeader,
	const TArray<uint8>& InCompressedPayload, const FBloodStainPlaybackOptions& InOptions)
{
	check(HasAuthority());

	if (GetOwner() == nullptr)
	{
		UE_LOG(LogBloodStain, Warning, TEXT("AReplayActor spawned without an owner. Setting owner to RequestingController."));
		SetOwner(RequestingController);
	}

	Server_CurrentPayload = InCompressedPayload;
	UE_LOG(LogBloodStain, Warning, TEXT("[%s] Initialized. Payload size: %d"), *GetName(), Server_CurrentPayload.Num());
	
	Server_BytesSent = 0;
	Server_AccumulatedTickTime = 0.f;
	Server_CurrentChunkIndex = 0;
	bIsTransferInProgress = false;

	// [Important] Only the orchestrator should handle the transfer and tick playback.
	SetIsOrchestrator(true);

	ClientTransferRequiredMap.Empty();
	NumClientsResponded = 0;
	
	// Notify clients to initialize the replay and send the header, option data

	const int32 TotalSize = Server_CurrentPayload.Num();
	constexpr int32 ChunkSize = 1024; // 16KB
	const int32 NumChunks = FMath::DivideAndRoundUp(TotalSize, ChunkSize);

	// Notify all Clients to prepare for receiving the payload
	Multicast_InitializeForPayload(InPlaybackKey, InFileHeader, InRecordHeader, InOptions);

	// If it's a listen server, also supposed to render for local client
	if (GetNetMode() == NM_ListenServer || GetNetMode() == NM_DedicatedServer)
	{
		UE_LOG(LogBloodStain, Log, TEXT("Listen Server: Executing local initialization."));
		
		TArray<uint8> LocalPayloadForListenServer  = InCompressedPayload;

		Client_PlaybackKey = InPlaybackKey;
		Client_FileHeader = InFileHeader;
		Client_RecordHeader = InRecordHeader;
		Client_PlaybackOptions = InOptions;
		Client_ReceivedPayloadBuffer = LocalPayloadForListenServer;
		
		Client_FinalizeAndSpawnVisuals();

		// If there is no remote client connected, finalize the transfer immediately.
		UNetDriver* NetDriver = GetNetDriver();
		if (NetDriver == nullptr || NetDriver->ClientConnections.Num() == 0)
		{
			UE_LOG(LogBloodStain, Log, TEXT("No remote clients on Listen Server. Finalizing transfer immediately."));
			bIsTransferInProgress = false;
			Server_CurrentPayload.Empty();
		}
	}
}

void AReplayActor::ProcessReceivedChunk(int32 ChunkIndex, const TArray<uint8>& DataChunk, bool bIsLastChunk)
{
	UE_LOG(LogBloodStain, Log, TEXT("Processing chunk %d, Size: %d, IsLastChunk: %s"), 
		ChunkIndex, DataChunk.Num(), bIsLastChunk ? TEXT("True") : TEXT("False"));
	if (bHasLocalFile)
	{
		return;
	}
	
	if (Client_PendingChunks.Contains(ChunkIndex))
	{
		return;
	}

	Client_PendingChunks.Add(ChunkIndex, DataChunk);

	if (bIsLastChunk)
	{
		Client_ExpectedChunks = ChunkIndex + 1;
		Client_AssembleAndFinalize();
	}
}

void AReplayActor::UpdateClientCacheStatus(APlayerController* ReportingController, bool bClientHasFile)
{
	if (!ReportingController || !ReportingController->GetNetConnection())
	{
		return;
	}

	UNetConnection* Connection = ReportingController->GetNetConnection();
	if (Connection)
	{
		if (!ClientTransferRequiredMap.Contains(Connection))
		{
			ClientTransferRequiredMap.Add(Connection, !bClientHasFile);
			NumClientsResponded++;
			UE_LOG(LogBloodStain, Log, TEXT("Server received cache status from client %s. Needs transfer: %s. Total responded: %d"), *ReportingController->GetName(), !bClientHasFile ? TEXT("Yes") : TEXT("No"), NumClientsResponded);
		}
	}

	UNetDriver* NetDriver = GetNetDriver();
	if (NetDriver)
	{
		if (NumClientsResponded >= NetDriver->ClientConnections.Num())
		{
			UE_LOG(LogBloodStain, Log, TEXT("All remote clients have reported. Checking if transfer is needed."));
			
			bool bAnyClientNeedsTransfer = false;
			for (const auto& Elem : ClientTransferRequiredMap)
			{
				if (Elem.Value == true)
				{
					bAnyClientNeedsTransfer = true;
					break;
				}
			}

			if (bAnyClientNeedsTransfer)
			{
				UE_LOG(LogBloodStain, Log, TEXT("At least one client needs the file. Starting transfer process."));
				bIsTransferInProgress = true;
			}
			else
			{
				UE_LOG(LogBloodStain, Log, TEXT("No clients need the file. Transfer will be skipped."));
				Server_CurrentPayload.Empty(); 
			}
		}
	}
}

void AReplayActor::Client_AssembleAndFinalize()
{
	if (Client_PendingChunks.IsEmpty())
	{
		UE_LOG(LogBloodStain, Error, TEXT("Client failed to assemble: No chunks received."));
		Destroy();
		return;
	}

	int32 HighestIndex = 0;
	for (const auto& Elem : Client_PendingChunks)
	{
		if (Elem.Key > HighestIndex)
		{
			HighestIndex = Elem.Key;
		}
	}

	int32 TotalPayloadSize = 0;
	for (int32 i = 0; i <= HighestIndex; ++i)
	{
		if (const TArray<uint8>* Chunk = Client_PendingChunks.Find(i))
		{
			TotalPayloadSize += Chunk->Num();
		}
		else
		{
			UE_LOG(LogBloodStain, Error, TEXT("Client failed to assemble: Chunk %d is missing!"), i);
			Destroy();
			return;
		}
	}
    
	Client_ReceivedPayloadBuffer.Empty(TotalPayloadSize);

	for (int32 i = 0; i <= HighestIndex; ++i)
	{
		Client_ReceivedPayloadBuffer.Append(Client_PendingChunks[i]);
	}

	UE_LOG(LogBloodStain, Warning, TEXT("Assembling done. Payload Size: %d, Expected Uncompressed Size: %lld, Compression Method: %s"),
		Client_ReceivedPayloadBuffer.Num(), Client_FileHeader.UncompressedSize, *UEnum::GetValueAsString(Client_FileHeader.Options.CompressionOption)
	);
	
	Client_PendingChunks.Empty();
	Client_FinalizeAndSpawnVisuals();
}

void AReplayActor::Client_FinalizeAndSpawnVisuals()
{
	TArray<uint8> RawBytes;
	if (!BloodStainCompressionUtils::DecompressBuffer(Client_FileHeader.UncompressedSize, Client_ReceivedPayloadBuffer,RawBytes, Client_FileHeader.Options.CompressionOption))
	{
		UE_LOG(LogBloodStain, Error, TEXT("[BS] Client failed to decompress payload."));
		Destroy();
		return;
	}

	FRecordSaveData AllReplayData;
	FMemoryReader MemoryReader(RawBytes, true);
	BloodStainFileUtils_Internal::DeserializeSaveData(MemoryReader, AllReplayData, Client_FileHeader.Options.QuantizationOption);
	AllReplayData.Header = Client_RecordHeader;

	// Save the replay data locally if it doesn't already exist
	if (!bHasLocalFile)
	{
		SaveReplayLocallyIfNotExists(AllReplayData, Client_RecordHeader, Client_FileHeader.Options);
	}

	if (MemoryReader.IsError())
	{
		UE_LOG(LogBloodStain, Error, TEXT("[BS] Client failed to deserialize raw bytes."));
		Destroy();
		return;
	}

	if (IsNetMode(NM_DedicatedServer))
	{
		PlayComponent->SetComponentTickEnabled(true);
		PlayComponent->RecordHeaderData = Client_RecordHeader;
		PlayComponent->PlaybackOptions = Client_PlaybackOptions;
		PlayComponent->SetPlaybackStartTime(GetWorld()->GetTimeSeconds());
		return;
	}
	
	for (const FRecordActorSaveData& Data : AllReplayData.RecordActorDataArray)
	{
		AReplayActor* VisualActor = GetWorld()->SpawnActor<AReplayActor>(AReplayActor::StaticClass(), GetActorTransform());
		if (VisualActor)
		{
			VisualActor->SetReplicates(false); 
			VisualActor->InitializeReplayLocal(Client_PlaybackKey, AllReplayData.Header, Data, Client_PlaybackOptions);
			VisualActor->SetActorHiddenInGame(true);
			Client_SpawnedVisualActors.Add(VisualActor);
		}
	}
}

// Called when the client already has the full payload data
void AReplayActor::Client_FinalizeAndSpawnVisuals(const FRecordSaveData& AllReplayData)
{
	for (const FRecordActorSaveData& Data : AllReplayData.RecordActorDataArray)
	{
		AReplayActor* VisualActor = GetWorld()->SpawnActor<AReplayActor>(AReplayActor::StaticClass(), GetActorTransform());
		if (VisualActor)
		{
			VisualActor->SetReplicates(false); 
			VisualActor->InitializeReplayLocal(Client_PlaybackKey, AllReplayData.Header, Data, Client_PlaybackOptions);
			VisualActor->SetActorHiddenInGame(true);
			Client_SpawnedVisualActors.Add(VisualActor);
		}
	}
}

void AReplayActor::OnRep_PlaybackTime()
{
	TRACE_CPUPROFILER_EVENT_SCOPE_STR("AReplayActor::OnRep_PlaybackTime");

	if (Client_SpawnedVisualActors.Num() == 0)
	{
		return;
	}
	// UE_LOG (LogBloodStain, Log, TEXT("AReplayActor::OnRep_PlaybackTime - Updated Client to Time: %.2f"), ReplicatedPlaybackTime);
	
	for (TObjectPtr<AReplayActor> VisualActor : Client_SpawnedVisualActors)
	{
		if (VisualActor && VisualActor->PlayComponent && VisualActor->PlayComponent->IsComponentTickEnabled())
		{
			VisualActor->SetActorTickEnabled(false);
			VisualActor->PlayComponent->UpdatePlaybackToTime(ReplicatedPlaybackTime);
			
		}
	}
}

void AReplayActor::Multicast_InitializeForPayload_Implementation(const FGuid& InPlaybackKey,
                                                                 const FBloodStainFileHeader& InFileHeader, const FRecordHeaderData& InRecordHeader,
                                                                 const FBloodStainPlaybackOptions& InOptions)
{
	/** Client only : prepare for the replay start, saving metadata from the server */
	if (HasAuthority())
	{
		return;
	}
	
    Client_PlaybackKey = InPlaybackKey;
    Client_FileHeader = InFileHeader;
    Client_RecordHeader = InRecordHeader;
    Client_PlaybackOptions = InOptions;
	
	Client_ReceivedChunks = 0;
	Client_PendingChunks.Empty();
	Client_ReceivedPayloadBuffer.Empty();

	FRecordSaveData TempData;
	bHasLocalFile = BloodStainFileUtils::LoadFromFile(InRecordHeader.FileName.ToString(), InRecordHeader.LevelName.ToString(), TempData);
	// bHasLocalFile = false; // only if you want to test if the file is not present locally, uncomment this line
	UE_LOG(LogBloodStain, Log, TEXT("Client checking for file %s. Found: %d"), *InRecordHeader.FileName.ToString(), bHasLocalFile);
	
	if (bHasLocalFile)
	{
		UE_LOG(LogBloodStain, Log, TEXT("Client has local file: %s. No transfer needed."), *Client_RecordHeader.FileName.ToString());
	
		TArray<uint8> SerializedData;
		FMemoryWriter MemoryWriter(SerializedData, true);
		BloodStainFileUtils_Internal::SerializeSaveData(MemoryWriter, TempData, Client_FileHeader.Options.QuantizationOption);
	
		Client_ReceivedPayloadBuffer = SerializedData;
		Client_FinalizeAndSpawnVisuals(TempData);
	}
	
	if (APlayerController* PC = GetWorld()->GetFirstPlayerController())
	{
		if (PC->IsLocalController())
		{
			if (AGhostPlayerController* GhostPC = Cast<AGhostPlayerController>(PC))
			{
				GhostPC->Server_ReportReplayFileCacheStatus(this, bHasLocalFile);
			}
		}
	}
}	

void AReplayActor::Multicast_ReceivePayloadChunk_Implementation(int32 ChunkIndex, const TArray<uint8>& DataChunk, bool bIsLastChunk)
{
	if (HasAuthority())
	{
		return;
	}

	if (bHasLocalFile)
	{
		UE_LOG(LogBloodStain, Log, TEXT("Client already has local file. Ignoring received chunk %d."), ChunkIndex);
		return;
	}

	if (Client_PendingChunks.Contains(ChunkIndex))
	{
		return;
	}

	Client_PendingChunks.Add(ChunkIndex, DataChunk);

	if (bIsLastChunk)
	{
		// All chunks received, finalize the data and initialize the playback component
		Client_ExpectedChunks = Server_CurrentChunkIndex;
		Client_AssembleAndFinalize();
	}
}

void AReplayActor::Server_TickTransfer(float DeltaSeconds)
{
	// If there is no data to send or the transfer is not in progress, early exit.
	if (!bIsTransferInProgress || Server_CurrentPayload.IsEmpty())
	{
		bIsTransferInProgress = false;
		return;
	}

	UNetDriver* NetDriver = GetNetDriver();
	if (!NetDriver)
	{
		bIsTransferInProgress = false;
		return;
	}

	bool bCanSend = true;
#if WITH_SERVER_CODE
	// check the busiest channel among all clients to prevent server from being overloaded
	if (NetDriver->ClientConnections.Num() > 0)
	{
		int32 MaxNumOutRec = 0;
		// check all clients' connections.
		for (UNetConnection* Connection : NetDriver->ClientConnections)
		{
			if (Connection && Connection->GetConnectionState() == EConnectionState::USOCK_Open)
			{
				if (UActorChannel* Channel = Connection->FindActorChannelRef(this))
				{
					MaxNumOutRec = FMath::Max(MaxNumOutRec, Channel->NumOutRec);
				}
			}
		}

		// If the maximum number of outgoing reliable packets (NumOutRec) for any client is too high,
		// we will throttle the transfer to prevent network congestion.
		if (MaxNumOutRec >= (RELIABLE_BUFFER / 2))
		{
			bCanSend = false;
			// UE_LOG(LogBloodStain, Log, TEXT("Server_TickTransfer: Throttling transfer due to high network congestion (MaxNumOutRec: %d)."), MaxNumOutRec);
		}
	}
	else
	{
		bIsTransferInProgress = false;
		Server_CurrentPayload.Empty();
		UE_LOG(LogBloodStain, Log, TEXT("No clients connected. Transfer cancelled."));
		return;
	}
#else
	// Server is the only one implementing the transfer logic, so we skipped this check in client builds.
	
#endif
	// Only proceed with sending data if we are allowed to send
	if (bCanSend)
	{
		int32 MaxBytesToSendThisTick = Server_CurrentPayload.Num();
		const float TotalTimeSinceLastTransfer = DeltaSeconds + Server_AccumulatedTickTime;
		if (RateLimitMbps > 0)
		{
			const float BytesPerSecond = (RateLimitMbps * 1024 * 1024) / 8.0f;
			MaxBytesToSendThisTick = FMath::Max(1, static_cast<int32>(TotalTimeSinceLastTransfer * BytesPerSecond));
		}
		int32 BytesSentThisTick = 0;

		const int32 MaxChunksPerFrame = 4; // Max number of chunks to send per frame (64KB total)
		int32 ChunksSentThisFrame = 0;
		
		constexpr int32 MinChunkSize = 256;
		constexpr int32 MaxChunkSize = 16 * 1024;

		UE_LOG(LogBloodStain, Log, TEXT("Server_BytesSent: %d"), Server_BytesSent);
		while (Server_BytesSent < Server_CurrentPayload.Num() &&
			   BytesSentThisTick < MaxBytesToSendThisTick &&
			   ChunksSentThisFrame < MaxChunksPerFrame)
		{
			const int32 BytesRemaining = Server_CurrentPayload.Num() - Server_BytesSent;
			const int32 BytesLeftInTick = MaxBytesToSendThisTick - BytesSentThisTick;

			int32 ChunkSize = FMath::Min(BytesRemaining, MaxChunkSize);
			ChunkSize = FMath::Min(ChunkSize, BytesLeftInTick);
			
			const bool bIsLastChunk = (Server_BytesSent + ChunkSize) >= Server_CurrentPayload.Num();
			if (!bIsLastChunk && ChunkSize < MinChunkSize && RateLimitMbps > 0)
			{
				Server_AccumulatedTickTime += DeltaSeconds;
				break;
			}
			
			TArray<uint8> ChunkData;
			ChunkData.Append(Server_CurrentPayload.GetData() + Server_BytesSent, ChunkSize);
			
			const int32 ChunkIndex = Server_BytesSent / MaxChunkSize;


			for (UNetConnection* Connection : NetDriver->ClientConnections)
			{
				if (!Connection || Connection->PlayerController == nullptr)
					continue;

				if (bool* bNeedTransfer = ClientTransferRequiredMap.Find(Connection))
				{
					if (*bNeedTransfer)
					{
						if (AGhostPlayerController* TargetPC = Cast<AGhostPlayerController>(Connection->PlayerController))
						{
							TargetPC->Client_ReceiveReplayChunk(this, Server_CurrentChunkIndex, ChunkData, bIsLastChunk);
						}
					}
				}
			}
			
			// Multicast_ReceivePayloadChunk(Server_CurrentChunkIndex, ChunkData, bIsLastChunk);

			Server_BytesSent += ChunkSize;
			BytesSentThisTick += ChunkSize;
			ChunksSentThisFrame++;

			Server_CurrentChunkIndex++;
		}
		
        Server_AccumulatedTickTime = 0.f;
	}
    else
    {
        Server_AccumulatedTickTime += DeltaSeconds;
    }

	if (Server_BytesSent >= Server_CurrentPayload.Num())
	{
		UE_LOG(LogBloodStain, Log, TEXT("Payload transfer completed for actor %s."), *GetName());
		bIsTransferInProgress = false;
		Server_CurrentPayload.Empty();
	}
}

void AReplayActor::Server_TickPlayback(float DeltaSeconds)
{
	// Send data Completed, now we are in the playback phase.
	UPlayComponent* TimeSourceComponent = nullptr;
	
	if (GetNetMode() == NM_ListenServer)
	{
		// In Listen Server we calculate playback time from the first spawned visual actor.
		if (Client_SpawnedVisualActors.IsValidIndex(0) && Client_SpawnedVisualActors[0])
		{
			TimeSourceComponent = Client_SpawnedVisualActors[0]->PlayComponent;
		}
	}
	else if (GetNetMode() == NM_DedicatedServer)
	{
		// No need to spawn visual actors on the dedicated server.
		TimeSourceComponent = this->PlayComponent;
	}

	if (TimeSourceComponent && TimeSourceComponent->IsComponentTickEnabled())
	{
		float ElapsedTime = 0.f;
		if (TimeSourceComponent->CalculatePlaybackTime(ElapsedTime))
		{
			// Orchestrator AReplayActor is the only one updates Replicated PlaybackTime.
			ReplicatedPlaybackTime = ElapsedTime;
			if (GetNetMode() == NM_ListenServer || GetNetMode() == NM_DedicatedServer)
			{
				for (AReplayActor* Visualizer : Client_SpawnedVisualActors)
				{
					if (Visualizer && Visualizer->PlayComponent)
					{
						Visualizer->PlayComponent->UpdatePlaybackToTime(ReplicatedPlaybackTime);
					}
				}
			}
		}
		else
		{
			SetActorTickEnabled(false);
		}
	}
}

void AReplayActor::SaveReplayLocallyIfNotExists(const FRecordSaveData& SaveData, const FRecordHeaderData& Header, const FBloodStainFileOptions& Options)
{
	const FString FileName = Header.FileName.ToString();
	const FString LevelName = Header.LevelName.ToString();

	if (!BloodStainFileUtils::FileExists(FileName, LevelName))
	{
		const bool bSaved = BloodStainFileUtils::SaveToFile(SaveData, LevelName, FileName, Options);
		if (bSaved)
		{
			UE_LOG(LogBloodStain, Log, TEXT("[Client] Replay saved locally: %s / %s"), *LevelName, *FileName);
		}
		else
		{
			UE_LOG(LogBloodStain, Warning, TEXT("[Client] Failed to save replay locally: %s / %s"), *LevelName, *FileName);
		}
	}
	else
	{
		UE_LOG(LogBloodStain, Log, TEXT("[Client] Replay file already exists locally: %s / %s"), *LevelName, *FileName);
	}
}