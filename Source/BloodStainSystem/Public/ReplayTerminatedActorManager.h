/*
* Copyright 2025 TenToTen, All Rights Reserved.
*/


#pragma once

#include "CoreMinimal.h"
#include "RecordComponent.h"
#include "UObject/Object.h"
#include "Tickable.h"
#include "ReplayTerminatedActorManager.generated.h"

DECLARE_DELEGATE(FOnRecordGroupRemove);

struct FRecordActorSaveData;
/**
 * 
 */
UCLASS()
class BLOODSTAINSYSTEM_API UReplayTerminatedActorManager : public UObject, public FTickableGameObject
{
	GENERATED_BODY()

public:
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	
	TArray<FRecordActorSaveData> CookQueuedFrames(const FName& GroupName);

	bool ContainsGroup(const FName& GroupName) const;
	
	void AddToRecordGroup(const FName& GroupName, URecordComponent* RecordComponent);
	void ClearRecordGroup(const FName& GroupName);

	FOnRecordGroupRemove OnRecordGroupRemoveByCollecting;

private:

	void CollectRecordGroups(float DeltaTime);
	
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
