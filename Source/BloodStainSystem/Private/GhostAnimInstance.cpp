/*
* Copyright 2025 TenToTen, All Rights Reserved.
*/

#include "GhostAnimInstance.h"
#include "GhostAnimInstanceProxy.h"

UGhostAnimInstance::UGhostAnimInstance()
{
	// No initialization needed
}

FAnimInstanceProxy* UGhostAnimInstance::CreateAnimInstanceProxy()
{
	return new FGhostAnimInstanceProxy(this);
}

void UGhostAnimInstance::DestroyAnimInstanceProxy(FAnimInstanceProxy* InProxy)
{
	delete InProxy;
}

void UGhostAnimInstance::SetTargetPose(const TArray<FTransform>& InPose)
{
	BonePose = InPose;
}