// GhostAnimInstanceProxy.cpp
#include "GhostAnimInstanceProxy.h"
#include "GhostAnimInstance.h"

FGhostAnimInstanceProxy::FGhostAnimInstanceProxy(UAnimInstance* InInstance)
	: FAnimInstanceProxy(InInstance)
	, GhostInstance(CastChecked<UGhostAnimInstance>(InInstance))
{
}

bool FGhostAnimInstanceProxy::Evaluate(FPoseContext& Output)
{
	const FBoneContainer& BoneContainer = Output.AnimInstanceProxy->GetRequiredBones();
	const TArray<FTransform>& SrcPose = GhostInstance->GetPose();

	if (SrcPose.Num() != BoneContainer.GetNumBones())
	{
		Output.ResetToRefPose();
		return false;
	}

	for (int32 BoneIndex = 0; BoneIndex < BoneContainer.GetNumBones(); ++BoneIndex)
	{
		FCompactPoseBoneIndex CompactIndex(BoneIndex);
		Output.Pose[CompactIndex] = SrcPose[BoneIndex];
	}
	return true;
}
