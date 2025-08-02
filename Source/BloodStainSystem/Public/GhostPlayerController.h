// Fill out your copyright notice in the Description page of Project Settings.

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
};
