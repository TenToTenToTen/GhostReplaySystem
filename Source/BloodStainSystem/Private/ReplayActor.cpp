/*
* Copyright 2025 TenToTen, All Rights Reserved.
*/


#include "ReplayActor.h"
#include "PlayComponent.h"
#include "BloodStainCompressionUtils.h"
#include "QuantizationHelper.h"
#include "Net/UnrealNetwork.h"
#include "Engine/World.h"
#include "Engine/NetDriver.h"
#include "Engine/NetConnection.h"
#include "Engine/ActorChannel.h"
#include "Serialization/MemoryReader.h"
#include "BloodStainSystem.h"

DECLARE_CYCLE_STAT(TEXT("AReplayActor Tick"), STAT_AReplayActor_Tick, STATGROUP_BloodStain);

AReplayActor::AReplayActor()
{
	TRACE_CPUPROFILER_EVENT_SCOPE_STR("AReplayActor::AReplayActor");
	
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;
	bAlwaysRelevant = true;
	
	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	RootComponent = Root;
	
	PlayComponent = CreateDefaultSubobject<UPlayComponent>(TEXT("PlayComponent"));
	PlayComponent->PrimaryComponentTick.bCanEverTick = true;
}

void AReplayActor::BeginPlay()
{
	TRACE_CPUPROFILER_EVENT_SCOPE_STR("AReplayActor::BeginPlay");
	Super::BeginPlay();
	ENetMode Mode = GetNetMode();
}


void AReplayActor::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

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
		if (PlayComponent && PlayComponent->IsTickable())
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
	
	PlayComponent->PrimaryComponentTick.bCanEverTick = true;
	PlayComponent->Initialize(InPlaybackKey, InHeader, InActorData, InOptions);
	PlayComponent->SetComponentTickEnabled(true);
}

void AReplayActor::Server_InitializeReplayWithPayload(const FGuid& InPlaybackKey,
	const FBloodStainFileHeader& InFileHeader, const FRecordHeaderData& InRecordHeader,
	const TArray<uint8>& InCompressedPayload, const FBloodStainPlaybackOptions& InOptions)
{
	check(HasAuthority());

	Server_CurrentPayload = InCompressedPayload;
	UE_LOG(LogBloodStain, Warning, TEXT("[%s] Initialized. Payload size: %d"), *GetName(), Server_CurrentPayload.Num());
	
	Server_BytesSent = 0;
	Server_AccumulatedTickTime = 0.f;
	Server_CurrentChunkIndex = 0;
	bIsTransferInProgress = true;

	// [Important] Only the orchestrator should handle the transfer and tick playback.
	SetIsOrchestrator(true);
	
	// Notify clients to initialize the replay and send the header, option data

	const int32 TotalSize = Server_CurrentPayload.Num();
	constexpr int32 ChunkSize = 16*1024; // 16KB
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
		Client_ReceivedPayloadBuffer.Num(), Client_FileHeader.UncompressedSize, *UEnum::GetValueAsString(Client_FileHeader.Options.Compression.Method)
	);
	
	Client_PendingChunks.Empty();
	Client_FinalizeAndSpawnVisuals();
}

void AReplayActor::Client_FinalizeAndSpawnVisuals()
{
	TArray<uint8> RawBytes;
	if (!BloodStainCompressionUtils::DecompressBuffer(Client_FileHeader.UncompressedSize, Client_ReceivedPayloadBuffer,RawBytes, Client_FileHeader.Options.Compression))
	{
		UE_LOG(LogBloodStain, Error, TEXT("[BS] Client failed to decompress payload."));
		Destroy();
		return;
	}

	FRecordSaveData AllReplayData;
	FMemoryReader MemoryReader(RawBytes, true);
	BloodStainFileUtils_Internal::DeserializeSaveData(MemoryReader, AllReplayData, Client_FileHeader.Options.Quantization);
	AllReplayData.Header = Client_RecordHeader;

	if (MemoryReader.IsError())
	{
		UE_LOG(LogBloodStain, Error, TEXT("[BS] Client failed to deserialize raw bytes."));
		Destroy();
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
		if (VisualActor && VisualActor->GetPlayComponent() && VisualActor->GetPlayComponent()->IsTickable())
		{
			VisualActor->SetActorTickEnabled(false);
			VisualActor->GetPlayComponent()->UpdatePlaybackToTime(ReplicatedPlaybackTime);
			
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
}	

void AReplayActor::Multicast_ReceivePayloadChunk_Implementation(int32 ChunkIndex, const TArray<uint8>& DataChunk, bool bIsLastChunk)
{
	if (HasAuthority())
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

			Multicast_ReceivePayloadChunk(Server_CurrentChunkIndex, ChunkData, bIsLastChunk);

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
			TimeSourceComponent = Client_SpawnedVisualActors[0]->GetPlayComponent();
		}
	}
	else if (GetNetMode() == NM_DedicatedServer)
	{
		// No need to spawn visual actors on the dedicated server.
		TimeSourceComponent = this->PlayComponent;
	}

	if (TimeSourceComponent && TimeSourceComponent->IsTickable())
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
					if (Visualizer && Visualizer->GetPlayComponent())
					{
						Visualizer->GetPlayComponent()->UpdatePlaybackToTime(ReplicatedPlaybackTime);
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
