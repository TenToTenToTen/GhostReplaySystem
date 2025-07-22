/*
* Copyright 2025 TenToTen, All Rights Reserved.
*/


#include "ReplayActor.h"
#include "PlayComponent.h"
#include "BloodStainCompressionUtils.h"
#include "QuantizationHelper.h"
#include "Net/UnrealNetwork.h"
#include "BloodStainSystem.h"

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
	ENetMode Mode = GetNetMode();
	if (Mode == NM_DedicatedServer)
	{
		// SetActorTickEnabled(false);
	}
}

void AReplayActor::Tick(float DeltaTime)
{
	TRACE_CPUPROFILER_EVENT_SCOPE_STR("AReplayActor::Tick");
	
	Super::Tick(DeltaTime);

	if (!PlayComponent || !PlayComponent->IsTickable())
	{
		return;
	}

	if (HasAuthority())
	{
		TRACE_CPUPROFILER_EVENT_SCOPE_STR("AReplayActor::Tick (Authority)");
		
		float ElapsedTime = 0.f;
		if (!PlayComponent->CalculatePlaybackTime(ElapsedTime))
		{
			PlayComponent->FinishReplay();
			return;
		}

		const FRecordActorSaveData& ReplayData = PlayComponent->GetReplayData();
		const bool bShouldBeHidden = ReplayData.RecordedFrames.IsEmpty() || 
								 ElapsedTime < ReplayData.RecordedFrames[0].TimeStamp || 
								 ElapsedTime > ReplayData.RecordedFrames.Last().TimeStamp;
		SetActorHiddenInGame(bShouldBeHidden);

		if (bShouldBeHidden)
		{
			return;
		}
	
		
		ReplicatedPlaybackTime = ElapsedTime;
		// UE_LOG(LogBloodStain, Log, TEXT("AReplayActor::Tick - Server Tick %.2f"), ReplicatedPlaybackTime);
	}
	else
	{
		TRACE_CPUPROFILER_EVENT_SCOPE_STR("AReplayActor::Tick (Client)");
		// UE_LOG(LogBloodStain, Warning, TEXT("AReplayActor::Tick - Client Tick %.2f"), ReplicatedPlaybackTime);
	}
	PlayComponent->UpdatePlaybackToTime(ReplicatedPlaybackTime);	
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
	const FRecordActorSaveData& InActorData, const FBloodStainPlaybackOptions& InOptions) const
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

	// Notify clients to initialize the replay and send the header, option data

	constexpr int32 ChunkSize = 16*1024; // 16KB
	const int32 TotalSize = InCompressedPayload.Num();
	const int32 NumChunks = FMath::DivideAndRoundUp(TotalSize, ChunkSize);

	// Notify all Clients to prepare for receiving the payload
	Multicast_InitializeForPayload(InPlaybackKey, InFileHeader, InRecordHeader, InOptions, NumChunks);
	
	for (int32 i = 0; i < NumChunks; ++i)
	{
		const int32 Offset = i * ChunkSize;
		const int32 Size = FMath::Min(ChunkSize, TotalSize - Offset); // Maximum size 16KB, or remaining size

		TArray<uint8> ChunkData;
		ChunkData.Append(InCompressedPayload.GetData() + Offset, Size);
		
		Multicast_ReceivePayloadChunk(i, ChunkData);
	}

	// If it's a listen server, also supposed to render for local client
	if (GetNetMode() == NM_ListenServer)
	{
		UE_LOG(LogBloodStain, Log, TEXT("Listen Server: Executing local initialization."));

		Client_PlaybackKey = InPlaybackKey;
		Client_FileHeader = InFileHeader;
		Client_RecordHeader = InRecordHeader;
		Client_PlaybackOptions = InOptions;
		Client_ExpectedChunks = NumChunks;
		Client_ReceivedChunks = NumChunks;
		Client_ReceivedPayloadBuffer = InCompressedPayload;
		
		Client_FinalizeAndSpawnVisuals();
	}
}

void AReplayActor::Client_AssembleAndFinalize()
{
	int32 TotalPayloadSize = 0;
	for (int32 i = 0; i < Client_ExpectedChunks; ++i)
	{
		if (Client_PendingChunks.Contains(i))
		{
			TotalPayloadSize += Client_PendingChunks[i].Num();
		}
		else
		{
			// If any chunk is missing, we cannot finalize
			UE_LOG(LogBloodStain, Error, TEXT("Client failed to assemble: Chunk %d is missing!"), i);
			Destroy();
			return;
		}
	}
    
	Client_ReceivedPayloadBuffer.Empty(TotalPayloadSize);

	for (int32 i = 0; i < Client_ExpectedChunks; ++i)
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

	// Hide the orchestrator actor itself and disable its tick.
	SetActorHiddenInGame(true);
	SetActorTickEnabled(false);

	for (const FRecordActorSaveData& Data : AllReplayData.RecordActorDataArray)
	{
		AReplayActor* VisualActor = GetWorld()->SpawnActor<AReplayActor>(AReplayActor::StaticClass(), GetActorTransform());
		if (VisualActor)
		{
			VisualActor->SetReplicates(false); 
			VisualActor->InitializeReplayLocal(Client_PlaybackKey, AllReplayData.Header, Data, Client_PlaybackOptions);
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

	for (TObjectPtr<AReplayActor> VisualActor : Client_SpawnedVisualActors)
	{
		if (PlayComponent && PlayComponent->IsTickable())
		{
			PlayComponent->UpdatePlaybackToTime(ReplicatedPlaybackTime);
		}
	}
}

void AReplayActor::Multicast_InitializeForPayload_Implementation(const FGuid& InPlaybackKey,
	const FBloodStainFileHeader& InFileHeader, const FRecordHeaderData& InRecordHeader,
	const FBloodStainPlaybackOptions& InOptions, int32 InTotalChunks)
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
	
	Client_ExpectedChunks = InTotalChunks; 
	Client_ReceivedChunks = 0;
	Client_PendingChunks.Empty();
	Client_ReceivedPayloadBuffer.Empty();
}	

void AReplayActor::Multicast_ReceivePayloadChunk_Implementation(int32 ChunkIndex, const TArray<uint8>& DataChunk)
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
	Client_ReceivedChunks++;

	UE_LOG(LogBloodStain, Log, TEXT("Client received chunk %d/%d"), Client_ReceivedChunks, Client_ExpectedChunks);

	if (Client_ExpectedChunks == 0 || Client_ReceivedChunks < Client_ExpectedChunks)
	{
		return;
	}
	
	if (Client_ReceivedChunks >= Client_ExpectedChunks)
	{
		// All chunks received, finalize the data and initialize the playback component
		Client_AssembleAndFinalize();
	}
}

