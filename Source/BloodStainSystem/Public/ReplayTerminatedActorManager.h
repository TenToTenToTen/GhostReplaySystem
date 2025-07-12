// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "RecordComponent.h"
#include "UObject/Object.h"
#include "ReplayTerminatedActorManager.generated.h"

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
	static void BuildInitialComponentStructure(int32 FirstFrameIndex, int32 NumSavedFrames, FRecordActorSaveData& GhostSaveData, TArray<FComponentInterval>& ComponentIntervals);
	
	void AddToRecordGroup(const FName& GroupName, URecordComponent* RecordComponent);
	void ClearRecordGroup(const FName& GroupName);	

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
