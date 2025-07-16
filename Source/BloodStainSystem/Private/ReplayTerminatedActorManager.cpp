/*
* Copyright 2025 TenToTen, All Rights Reserved.
*/


#include "ReplayTerminatedActorManager.h"
#include "BloodStainRecordDataUtils.h"
#include "BloodStainSystem.h"
#include "RecordComponent.h"
#include "Engine/World.h"

void UReplayTerminatedActorManager::Tick(float DeltaTime)
{
	CollectRecordGroups(DeltaTime);
}

TStatId UReplayTerminatedActorManager::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UReplayTerminatedActorManager, STATGROUP_Tickables);
}

void UReplayTerminatedActorManager::AddToRecordGroup(const FName& GroupName, URecordComponent* RecordComponent)
{
	if (!RecordGroups.Contains(GroupName))
	{
		RecordGroups.Add(GroupName, FRecordGroupData());
	}
	
	FRecordComponentData RecordComponentData = FRecordComponentData();
	RecordComponentData.StartTime = RecordComponent->StartTime;
	RecordComponentData.ActorName = RecordComponent->GetOwner()->GetFName();
	RecordComponentData.TimeSinceLastRecord = RecordComponent->TimeSinceLastRecord;
	RecordComponentData.FrameQueuePtr = TSharedPtr<TCircularQueue<FRecordFrame>>(RecordComponent->FrameQueuePtr.Release());
	RecordComponentData.GhostSaveData.PrimaryComponentName = MoveTemp(RecordComponent->PrimaryComponentName);

	RecordComponentData.ComponentIntervals = MoveTemp(RecordComponent->ComponentActiveIntervals);

	FRecordGroupData& RecordGroup = RecordGroups[GroupName];
	RecordGroup.RecordOptions = RecordComponent->RecordOptions;
	RecordGroup.RecordComponentData.Add(RecordComponentData);
}

void UReplayTerminatedActorManager::ClearRecordGroup(const FName& GroupName)
{
	RecordGroups.Remove(GroupName);
}

bool UReplayTerminatedActorManager::ContainsGroup(const FName& GroupName) const
{
	return RecordGroups.Contains(GroupName);
}

void UReplayTerminatedActorManager::CollectRecordGroups(float DeltaTime)
{
	TArray<FName> ToRemoveGroupNames;
	for (auto& [GroupName, RecordGroupData] : RecordGroups)
	{
		for (int32 i = RecordGroupData.RecordComponentData.Num() - 1; i >= 0; --i)
		{
			FRecordComponentData& RecordComponentData = RecordGroupData.RecordComponentData[i];
			RecordComponentData.TimeSinceLastRecord += DeltaTime;
			
			if (RecordComponentData.TimeSinceLastRecord >= RecordGroupData.RecordOptions.SamplingInterval)
			{
				FRecordFrame FirstFrame;
				while (RecordComponentData.FrameQueuePtr->Peek(FirstFrame))
				{
					float CurrentTimeStamp = GetWorld()->GetTimeSeconds() - RecordComponentData.StartTime;

					// Time Buffer Out
					if (FirstFrame.TimeStamp + RecordGroupData.RecordOptions.MaxRecordTime < CurrentTimeStamp)
					{
						RecordComponentData.FrameQueuePtr->Dequeue();
					}
					else
					{
						break;
					}
				}

				if (RecordComponentData.FrameQueuePtr->IsEmpty())
				{
					RecordGroupData.RecordComponentData.RemoveAt(i);
					continue;
				}
				
				RecordComponentData.TimeSinceLastRecord = 0.0f;
			}
		}

		if (RecordGroupData.RecordComponentData.Num() == 0)
		{
			ToRemoveGroupNames.Add(GroupName);
		}
	}

	for (const FName& ToRemoveGroupName : ToRemoveGroupNames)
	{
		RecordGroups.Remove(ToRemoveGroupName);
		OnRecordGroupRemoveByCollecting.ExecuteIfBound();
	}
}

TArray<FRecordActorSaveData> UReplayTerminatedActorManager::CookQueuedFrames(const FName& GroupName)
{
	TArray<FRecordActorSaveData> Result = TArray<FRecordActorSaveData>();
	if (!RecordGroups.Contains(GroupName))
	{
		UE_LOG(LogBloodStain, Warning, TEXT("There is No Group for %s"), *GroupName.ToString());
		return Result;
	}

	auto RecordGroupData = RecordGroups[GroupName];

	if (RecordGroupData.RecordComponentData.Num() == 0)
	{
		UE_LOG(LogBloodStain, Warning, TEXT("There is no Record Data in Group for %s"), *GroupName.ToString());
		RecordGroups.Remove(GroupName);
		return Result;
	}
	
	for (FRecordComponentData& RecordComponentData : RecordGroupData.RecordComponentData)
	{
		if (BloodStainRecordDataUtils::CookQueuedFrames(RecordGroupData.RecordOptions.SamplingInterval, RecordComponentData.FrameQueuePtr.Get(), RecordComponentData.GhostSaveData, RecordComponentData.ComponentIntervals))
		{
			Result.Add(RecordComponentData.GhostSaveData);
		}
	}
	
	return Result;
}

