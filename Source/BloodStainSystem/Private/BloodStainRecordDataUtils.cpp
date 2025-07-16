/*
* Copyright 2025 TenToTen, All Rights Reserved.
*/


#include "BloodStainRecordDataUtils.h"
#include "BloodStainSystem.h"
#include "GhostData.h"

bool BloodStainRecordDataUtils::CookQueuedFrames(float SamplingInterval, TCircularQueue<FRecordFrame>* FrameQueuePtr, FRecordActorSaveData& OutGhostSaveData, TArray<FComponentActiveInterval>& OutComponentIntervals)
{
	FRecordFrame First;
	if (!FrameQueuePtr->Peek(First))
	{
		UE_LOG(LogBloodStain, Warning, TEXT("No frames to save"));
		return false;
	}

	const int32 FirstIndex = First.FrameIndex;
	const float BaseTime = First.TimeStamp;

	// Copy original frame datas and do normalize timestamps [0, duration)
	TArray<FRecordFrame> RawFrames;
	FRecordFrame Tmp;
	while (FrameQueuePtr->Dequeue(Tmp))
	{
		Tmp.TimeStamp -= BaseTime;
		RawFrames.Add(MoveTemp(Tmp));
	}

	if (RawFrames.Num() < 2)
	{
		UE_LOG(LogBloodStain, Warning, TEXT("Not enough raw frames to interpolate."));
		return false;
	}

	// Set up the interpolation
	const float FrameInterval = SamplingInterval;
	const int32 NumInterpFrames = FMath::FloorToInt(RawFrames.Last().TimeStamp / FrameInterval);

	OutGhostSaveData.RecordedFrames.Empty(NumInterpFrames + 1);

	for (int32 i = 0; i <= NumInterpFrames; ++i)
	{
		float TargetTime = i * FrameInterval;

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

		// Interpolate two frames' transforms of components 
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

		// Interpolate two frames' transforms of bone transforms
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

		OutGhostSaveData.RecordedFrames.Add(MoveTemp(NewFrame));
	}

	/* Construct Initial Component Structure based on Total Component event Data */
	BloodStainRecordDataUtils::BuildInitialComponentStructure(FirstIndex, OutGhostSaveData, OutComponentIntervals);
	
	return true;
}

void BloodStainRecordDataUtils::BuildInitialComponentStructure(int32 FirstFrameIndex, FRecordActorSaveData& OutGhostSaveData, TArray<FComponentActiveInterval>& OutComponentIntervals)
{
	int32 NumSavedFrames = OutGhostSaveData.RecordedFrames.Num();
	OutComponentIntervals.Sort([](auto& A, auto& B) {
		return A.EndFrame < B.EndFrame;
	});

	// StartIdx : first index where EndFrame > FirstFrameIndex 
	const int32 StartIdx = Algo::UpperBoundBy(OutComponentIntervals, FirstFrameIndex, &FComponentActiveInterval::EndFrame);

	for (int32 i = StartIdx; i < OutComponentIntervals.Num(); ++i)
	{
		FComponentActiveInterval& Interval = OutComponentIntervals[i];

		Interval.StartFrame = FMath::Max(0, Interval.StartFrame - FirstFrameIndex);
		if (Interval.EndFrame == INT32_MAX)
		{
			Interval.EndFrame = NumSavedFrames;
		}
		else
		{
			Interval.EndFrame = FMath::Min(Interval.EndFrame - FirstFrameIndex, NumSavedFrames);
		}

		OutGhostSaveData.ComponentIntervals.Add(Interval);
		UE_LOG(LogBloodStain, Log, TEXT("BuildInitialComponentStructure: %s added to initial structure"),
		       *Interval.Meta.ComponentName);
		
	}
}