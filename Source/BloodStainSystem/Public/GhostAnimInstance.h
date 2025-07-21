/*
* Copyright 2025 TenToTen, All Rights Reserved.
*/

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "GhostAnimInstance.generated.h"

class FGhostAnimInstanceProxy;

/**
 * GhostAnimInstance is used to playback bone transforms from replay data.
 * It uses a custom AnimInstanceProxy to evaluate pose in multithreaded context.
 */
UCLASS(Transient, NotBlueprintable)
class BLOODSTAINSYSTEM_API UGhostAnimInstance : public UAnimInstance
{
	GENERATED_BODY()

public:
	UGhostAnimInstance();

	/** Apply external pose for current frame */
	void SetTargetPose(const TArray<FTransform>& InPose);

	/** Get read-only current bone pose */
	const TArray<FTransform>& GetPose() const { return BonePose; }

protected:
	virtual FAnimInstanceProxy* CreateAnimInstanceProxy() override;
	virtual void DestroyAnimInstanceProxy(FAnimInstanceProxy* InProxy) override;

private:
	/** Raw bone-space transforms passed from replay system */
	TArray<FTransform> BonePose;

	friend class FGhostAnimInstanceProxy;
};