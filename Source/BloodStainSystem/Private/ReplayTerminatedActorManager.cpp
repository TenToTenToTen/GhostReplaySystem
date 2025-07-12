// Fill out your copyright notice in the Description page of Project Settings.


#include "ReplayTerminatedActorManager.h"

#include "BloodStainSystem.h"

void UReplayTerminatedActorManager::Tick(float DeltaTime)
{
	TArray<FName> KeysToRemove;
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
						// 구간 내 데이터인 경우
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
			KeysToRemove.Add(GroupName);
		}
	}

	for (const FName& ToRemove : KeysToRemove)
	{
		RecordGroups.Remove(ToRemove);
	}
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
	RecordComponentData.GhostSaveData = MoveTemp(RecordComponent->GhostSaveData);

	RecordComponentData.ComponentIntervals = MoveTemp(RecordComponent->ComponentIntervals);

	FRecordGroupData& RecordGroup = RecordGroups[GroupName];
	RecordGroup.RecordOptions = RecordComponent->RecordOptions;
	RecordGroup.RecordComponentData.Add(RecordComponentData);
}

void UReplayTerminatedActorManager::ClearRecordGroup(const FName& GroupName)
{
	RecordGroups.Remove(GroupName);
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
	
	for (FRecordComponentData& RecordComponentData : RecordGroupData.RecordComponentData)
	{
		FRecordFrame First;
		if (!RecordComponentData.FrameQueuePtr->Peek(First))
		{
			UE_LOG(LogBloodStain, Warning, TEXT("No frames to save in RecordComponent for %s"), *RecordComponentData.ActorName.ToString());
			continue;
		}
		const int32 FirstIndex = First.FrameIndex;
		const float BaseTime = First.TimeStamp;

		
		// 1. 원본 프레임들 복사 + 시간 정규화
		TArray<FRecordFrame> RawFrames;
		FRecordFrame Tmp;
		while (RecordComponentData.FrameQueuePtr->Dequeue(Tmp))
		{
			Tmp.TimeStamp -= BaseTime;
			RawFrames.Add(MoveTemp(Tmp));
		}

		if (RawFrames.Num() < 2)
		{
			UE_LOG(LogBloodStain, Warning, TEXT("Not enough raw frames to interpolate."));
			continue;
		}

		// 2. 보간 프레임 설정
		const float FrameInterval = RecordGroupData.RecordOptions.SamplingInterval;
		const int32 NumInterpFrames = FMath::FloorToInt(RawFrames.Last().TimeStamp / FrameInterval);

		RecordComponentData.GhostSaveData.RecordedFrames.Empty(NumInterpFrames + 1);

		for (int32 i = 0; i <= NumInterpFrames; ++i)
		{
			float TargetTime = i * FrameInterval;

			// 3. 보간을 위한 A, B 프레임 찾기
			int32 PrevIndex = 0;
			while (PrevIndex + 1 < RawFrames.Num() && RawFrames[PrevIndex + 1].TimeStamp < TargetTime)
			{
				++PrevIndex;
			}

			const FRecordFrame& A = RawFrames[PrevIndex];
			const FRecordFrame& B = RawFrames[PrevIndex + 1];
			float Alpha = (TargetTime - A.TimeStamp) / (B.TimeStamp - A.TimeStamp);

			FRecordFrame NewFrame;
			NewFrame.TimeStamp = TargetTime;
			NewFrame.AddedComponents = A.AddedComponents;
			NewFrame.RemovedComponentNames = A.RemovedComponentNames;

			// 4. ComponentTransform 보간
			for (const auto& Pair : A.ComponentTransforms)
			{
				const FString& Name = Pair.Key;
				const FTransform& TA = Pair.Value;
				const FTransform* TBPtr = B.ComponentTransforms.Find(Name);
				if (!TBPtr) continue;

				const FTransform& TB = *TBPtr;
				FTransform Interp = FTransform(
					FQuat::Slerp(TA.GetRotation(), TB.GetRotation(), Alpha),
					FMath::Lerp(TA.GetLocation(), TB.GetLocation(), Alpha),
					FMath::Lerp(TA.GetScale3D(), TB.GetScale3D(), Alpha)
				);
				NewFrame.ComponentTransforms.Add(Name, Interp);
			}

			// 5. BoneTransform 보간
			for (const auto& BonePairA : A.SkeletalMeshBoneTransforms)
			{
				const FString& CompName = BonePairA.Key;
				const FBoneComponentSpace& BoneA = BonePairA.Value;

				if (const FBoneComponentSpace* BoneB = B.SkeletalMeshBoneTransforms.Find(CompName))
				{
					const int32 BoneCount = FMath::Min(BoneA.BoneTransforms.Num(), BoneB->BoneTransforms.Num());
					FBoneComponentSpace InterpBone;
					InterpBone.BoneTransforms.SetNum(BoneCount);

					for (int32 j = 0; j < BoneCount; ++j)
					{
						const FTransform& TA = BoneA.BoneTransforms[j];
						const FTransform& TB = BoneB->BoneTransforms[j];
						InterpBone.BoneTransforms[j] = FTransform(
							FQuat::Slerp(TA.GetRotation(), TB.GetRotation(), Alpha),
							FMath::Lerp(TA.GetLocation(), TB.GetLocation(), Alpha),
							FMath::Lerp(TA.GetScale3D(), TB.GetScale3D(), Alpha)
						);
					}

					NewFrame.SkeletalMeshBoneTransforms.Add(CompName, InterpBone);
				}
			}

			RecordComponentData.GhostSaveData.RecordedFrames.Add(MoveTemp(NewFrame));
		}

		/* Construct Initial Component Structure based on Total Component event Data */
		BuildInitialComponentStructure(FirstIndex, RecordComponentData.GhostSaveData.RecordedFrames.Num(), RecordComponentData.GhostSaveData, RecordComponentData.ComponentIntervals);

		Result.Add(RecordComponentData.GhostSaveData);
	}
	
	return Result;
}

void UReplayTerminatedActorManager::BuildInitialComponentStructure(int32 FirstFrameIndex, int32 NumSavedFrames, FRecordActorSaveData& GhostSaveData, TArray<FComponentInterval>& ComponentIntervals)
{
	ComponentIntervals.Sort([](auto& A, auto& B) {
		return A.EndFrame < B.EndFrame;
	});

	// StartIdx : first index where EndFrame > FirstFrameIndex 
	int32 StartIdx = Algo::UpperBoundBy(ComponentIntervals, FirstFrameIndex, &FComponentInterval::EndFrame);
	// Sorted[0..StartIdx-1] 은 FirstFrameIndex 이전에 이미 탈착된 구간

	// 3) 그 이후 구간만 순회하며 StartFrame ≤ FirstFrameIndex 필터
	GhostSaveData.InitialComponentStructure.Empty();
	for (int32 i = StartIdx; i < ComponentIntervals.Num(); ++i)
	{
		FComponentInterval& I = ComponentIntervals[i];
		// if (I.StartFrame <= FirstFrameIndex)
		// [StartFrame, EndFrame] 구간을 [0, NumSavedFrames) 구간으로 변환
		{
			GhostSaveData.InitialComponentStructure.Add(I.Meta);
			I.StartFrame = FMath::Max(0, I.StartFrame - FirstFrameIndex);
			if (I.EndFrame == INT32_MAX)
			{
				I.EndFrame = NumSavedFrames;
			}
			else
			{
				I.EndFrame = FMath::Min(I.EndFrame - FirstFrameIndex, NumSavedFrames);
			}
			
			GhostSaveData.ComponentIntervals.Add(I);
			UE_LOG(LogBloodStain, Log, TEXT("BuildInitialComponentStructure: %s added to initial structure"), *I.Meta.ComponentName);
		}
	}
}

