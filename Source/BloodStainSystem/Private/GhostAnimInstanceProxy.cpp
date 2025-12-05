/*
* Copyright 2025 TenToTen, All Rights Reserved.
*/

#include "GhostAnimInstanceProxy.h"
#include "GhostAnimInstance.h"
#include "Animation/AnimNodeBase.h"

FGhostAnimInstanceProxy::FGhostAnimInstanceProxy(UAnimInstance* InInstance)
	: FAnimInstanceProxy(InInstance)
	, GhostInstance(CastChecked<UGhostAnimInstance>(InInstance))
{
}

bool FGhostAnimInstanceProxy::Evaluate(FPoseContext& Output)
{
	const FBoneContainer& BoneContainer = Output.AnimInstanceProxy->GetRequiredBones();
	const TArray<FTransform>& SrcPose = GhostInstance->GetPose();
	
	const TArray<FBoneIndexType>& RequiredBoneIndices = BoneContainer.GetBoneIndicesArray();

	for (int32 CompactIdx = 0; CompactIdx < RequiredBoneIndices.Num(); ++CompactIdx)
	{
		const int32 SkeletonIndex = RequiredBoneIndices[CompactIdx];
		const FCompactPoseBoneIndex CompactIndex(CompactIdx);

		if (SrcPose.IsValidIndex(SkeletonIndex) && Output.Pose.IsValidIndex(CompactIndex))
		{
			Output.Pose[CompactIndex] = SrcPose[SkeletonIndex];
		}
		// TODO : Need to Fix
		else
		{
			Output.Pose[CompactIndex] = FTransform::Identity;
		}
	}
	return true;
}
