/*
* Copyright 2025 TenToTen, All Rights Reserved.
*/


#pragma once

#include "CoreMinimal.h"
#include "GhostData.h"
#include "UObject/Object.h"
#include "Tickable.h"
#include "Containers/CircularQueue.h"
#include "ReplayTerminatedActorManager.generated.h"

struct FRecordActorSaveData;


DECLARE_DELEGATE(FOnRecordGroupRemove);


/**
 * A Manager Class that takes over and maintains data from a RecordComponent
 * when it is stopped due to various reasons (e.g. Actor destruction, manual StopRecord).
 */
UCLASS()
class BLOODSTAINSYSTEM_API UReplayTerminatedActorManager : public UObject, public FTickableGameObject
{
	GENERATED_BODY()

public:
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

	// Cook Data from FrameQueue to GhostSaveData
	TArray<FRecordActorSaveData> CookQueuedFrames(const FName& GroupName);

	// if the group already exists, RecordComponent join the group
	void AddToRecordGroup(const FName& GroupName, URecordComponent* RecordComponent);

	void ClearRecordGroup(const FName& GroupName);

	bool ContainsGroup(const FName& GroupName) const;

private:
	// Remove old frameData from managing record groups
	void CollectRecordGroups(float DeltaTime);

public:
	FOnRecordGroupRemove OnRecordGroupRemoveByCollecting;
	
private:	
	struct FRecordComponentData
	{
		FName ActorName = NAME_None;
		
		float TimeSinceLastRecord = 0.0f;
		float StartTime = 0.f;

		TSharedPtr<TCircularQueue<FRecordFrame>> FrameQueuePtr = nullptr;
		FRecordActorSaveData GhostSaveData = FRecordActorSaveData();
		TArray<FComponentInterval> ComponentIntervals;
	};
	
	struct FRecordGroupData
	{
		FBloodStainRecordOptions RecordOptions;
		TArray<FRecordComponentData> RecordComponentData;
	};
	TMap<FName, FRecordGroupData> RecordGroups;
};
