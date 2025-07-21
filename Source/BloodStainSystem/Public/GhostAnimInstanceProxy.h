#pragma once
#include "Animation/AnimInstanceProxy.h"

class UGhostAnimInstance;

/**
 * Proxy class for multithreaded animation pose evaluation from replay bone data.
 */
class FGhostAnimInstanceProxy : public FAnimInstanceProxy
{
public:
	FGhostAnimInstanceProxy(UAnimInstance* InInstance);

	virtual bool Evaluate(FPoseContext& Output) override;

private:
	UGhostAnimInstance* GhostInstance;
};