/*
* Copyright 2025 TenToTen, All Rights Reserved.
*/


#pragma once

#include "Containers/CircularQueue.h"

struct FRecordFrame;
struct FComponentInterval;
struct FRecordActorSaveData;

namespace BloodStainRecordDataUtils
{
	bool CookQueuedFrames(float SamplingInterval, TCircularQueue<FRecordFrame>* FrameQueuePtr, FRecordActorSaveData& OutGhostSaveData, TArray<FComponentInterval>& OutComponentIntervals);
	void BuildInitialComponentStructure(int32 FirstFrameIndex, FRecordActorSaveData& OutGhostSaveData, TArray<FComponentInterval>& OutComponentIntervals);
};
