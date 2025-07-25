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
	FBoneContainer BoneContainer = FBoneContainer();
	BoneContainer = Output.AnimInstanceProxy->GetRequiredBones();
	const TArray<FTransform>& SrcPose = GhostInstance->GetPose();

	if (SrcPose.Num() != BoneContainer.GetNumBones())
	{
		Output.ResetToRefPose();
		return false;
	}

	for (int32 BoneIndex = 0; BoneIndex < BoneContainer.GetNumBones(); ++BoneIndex)
	{
		FCompactPoseBoneIndex CompactIndex = BoneContainer.GetCompactPoseIndexFromSkeletonIndex(BoneIndex);
		if (!CompactIndex.IsValid() || !Output.Pose.IsValidIndex(CompactIndex))
			continue;

		Output.Pose[CompactIndex] = SrcPose[BoneIndex];
	}
	return true;
}
