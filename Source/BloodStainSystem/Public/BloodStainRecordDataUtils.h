/*
* Copyright 2025 TenToTen, All Rights Reserved.
*/


#pragma once

#include "Containers/CircularQueue.h"

struct FRecordFrame;
struct FComponentActiveInterval;
struct FRecordActorSaveData;

namespace BloodStainRecordDataUtils
{
	bool CookQueuedFrames(float SamplingInterval, TCircularQueue<FRecordFrame>* FrameQueuePtr, FRecordActorSaveData& OutGhostSaveData, TArray<FComponentActiveInterval>& OutComponentIntervals);
	void BuildInitialComponentStructure(int32 FirstFrameIndex, FRecordActorSaveData& OutGhostSaveData, TArray<FComponentActiveInterval>& OutComponentIntervals);
};
