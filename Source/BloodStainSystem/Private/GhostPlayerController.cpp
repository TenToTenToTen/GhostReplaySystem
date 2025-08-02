// Fill out your copyright notice in the Description page of Project Settings.


#include "GhostPlayerController.h"

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
