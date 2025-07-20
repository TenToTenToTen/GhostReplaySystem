/*
* Copyright 2025 TenToTen, All Rights Reserved.
*/


#include "ReplayActor.h"

#include "BloodStainSystem.h"
#include "PlayComponent.h"
#include "Net/UnrealNetwork.h"
#include "Serialization/BufferArchive.h"

AReplayActor::AReplayActor()
{
	PrimaryActorTick.bCanEverTick = true;
	/** Only Spawn on Server, then automatically replciates to the client */
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
	Super::BeginPlay();
	ENetMode Mode = GetNetMode();
	if (Mode == NM_DedicatedServer)
	{
		// SetActorTickEnabled(false);
	}
}

void AReplayActor::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (!PlayComponent || !PlayComponent->IsTickable())
	{
		return;
	}

	if (HasAuthority())
	{
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
		// UE_LOG(LogBloodStain, Warning, TEXT("AReplayActor::Tick - Client Tick %.2f"), ReplicatedPlaybackTime);
	}
	PlayComponent->UpdatePlaybackToTime(ReplicatedPlaybackTime);

	
}

void AReplayActor::InitializeReplayLocal(const FGuid& InPlaybackKey, const FRecordHeaderData& InHeader,
	const FRecordActorSaveData& InActorData, const FBloodStainPlaybackOptions& InOptions)
{
	PlayComponent->PrimaryComponentTick.bCanEverTick = true;
	PlayComponent->Initialize(InPlaybackKey, InHeader, InActorData, InOptions);
	PlayComponent->SetComponentTickEnabled(true);
}

void AReplayActor::Server_InitializeReplay(
	const FGuid& InPlaybackKey, const FRecordHeaderData& InHeader,
	const FRecordActorSaveData& InActorData, const FBloodStainPlaybackOptions& InOptions)
{
	check(HasAuthority());

	Server_PlaybackStartTime = GetWorld()->GetTimeSeconds();
	Server_PlaybackOptions = InOptions;

	if (InActorData.RecordedFrames.Num() > 0)
	{
		Server_PlaybackDuration = InActorData.RecordedFrames.Last().TimeStamp;
	}

	Multicast_InitializeOnClients(InPlaybackKey, InHeader, InOptions);
	Server_BuildDataChunks(InActorData);
	Server_SendChunks();

	/** If it's Listen Server, Initialized itself too */
	if (GetNetMode() != NM_DedicatedServer && PlayComponent)
	{
		FRecordActorSaveData LocalCopy = InActorData;
		PlayComponent->Initialize(InPlaybackKey, InHeader, MoveTemp(LocalCopy), InOptions);
	}
}

void AReplayActor::OnRep_PlaybackTime()
{
	// GEngine->AddOnScreenDebugMessage(
	// 		-1, 0.1f, FColor::Yellow,
	// 		FString::Printf(TEXT("Client OnRep_PlaybackTime: %0.2f"), ReplicatedPlaybackTime)
	// 	);
	// UE_LOG(LogBloodStain, Log, TEXT("AReplayActor::OnRep_PlaybackTime Called: %f"), ReplicatedPlaybackTime);
	if (PlayComponent && PlayComponent->IsTickable())
	{
		// UE_LOG(LogBloodStain, Log, TEXT("AReplayActor::OnRep_PlaybackTime - Replicated Playback Time: %f"), ReplicatedPlaybackTime);
		PlayComponent->UpdatePlaybackToTime(ReplicatedPlaybackTime);
	}
}

void AReplayActor::Server_BuildDataChunks(const FRecordActorSaveData& InActorData)
{
	check(HasAuthority());

	FBufferArchive Archive;
	Archive << const_cast<FRecordActorSaveData&>(InActorData);

	constexpr int32 ChunkSize = 8000; // 8KB
	const int32 TotalSize = Archive.Num();
	const int32 NumChunks = FMath::DivideAndRoundUp(TotalSize, ChunkSize);

	Server_DataChunks.Empty(NumChunks);
	Server_DataChunks.SetNum(NumChunks);

	for (int32 i = 0; i < NumChunks; ++i)
	{
		const int32 Offset = i * ChunkSize;
		const int32 Size = FMath::Min(ChunkSize, TotalSize - Offset); // Maximum size 8KB, or remaining size
		Server_DataChunks[i].Append(Archive.GetData() + Offset, Size);
	}
}

void AReplayActor::Server_SendChunks()
{
	check(HasAuthority());

	const int32 TotalChunks = Server_DataChunks.Num();
	for (int32 i = 0; i < TotalChunks; ++i)
	{
		Multicast_ReceiveDataChunk(i, TotalChunks, Server_DataChunks[i]);
	}

	// Memory cleanup after sending all chunks
	Server_DataChunks.Empty();
}


void AReplayActor::Multicast_InitializeOnClients_Implementation(const FGuid& InPlaybackKey,
                                                                const FRecordHeaderData& InHeader, const FBloodStainPlaybackOptions& InOptions)
{
	if (HasAuthority() && GetNetMode() == NM_DedicatedServer)
	{
		// Client is the only one should enter this function
		return;
	}

	Client_PlaybackKey = InPlaybackKey;
	Client_HeaderData = InHeader;
	Client_PlaybackOptions = InOptions;

	Client_ExpectedChunks = 0;
	Client_ReceivedChunks = 0;
	Client_ReceivedDataBuffer.Empty();
}

void AReplayActor::Multicast_ReceiveDataChunk_Implementation(int32 ChunkIndex, int32 TotalChunks,
	const TArray<uint8>& DataChunk)
{
	if (HasAuthority())
	{
		// Client is the only one should enter this function
		return;
	}

	if (Client_ReceivedChunks == 0)
	{
		Client_ExpectedChunks = TotalChunks;
	}

	// Append the received data chunk to the buffer
	Client_ReceivedDataBuffer.Append(DataChunk);
	Client_ReceivedChunks++;

	if (Client_ReceivedChunks == Client_ExpectedChunks)
	{
		Client_FinalizeDataAndInitialize();
	}
}

void AReplayActor::Client_FinalizeDataAndInitialize()
{
	FMemoryReader Reader(Client_ReceivedDataBuffer, true);
	FRecordActorSaveData DeserializedData;
	Reader << DeserializedData;

	if (Reader.IsError() || !PlayComponent)
	{
		Destroy();
		return;
	}

	UE_LOG(LogBloodStain, Warning, TEXT("Client_FinalizeDataAndInitialize: Received %d chunks, total size: %d bytes"),
		Client_ReceivedChunks, Client_ReceivedDataBuffer.Num());
	PlayComponent->Initialize(Client_PlaybackKey, Client_HeaderData, MoveTemp(DeserializedData), Client_PlaybackOptions);
}



void AReplayActor::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME_CONDITION(AReplayActor, ReplicatedPlaybackTime, COND_None);
}


UPlayComponent* AReplayActor::GetPlayComponent() const
{
	return PlayComponent;
}
