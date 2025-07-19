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
	/**
	 * Cook QueuedFrameData to SaveData
	 */
	bool CookQueuedFrames(float SamplingInterval, TCircularQueue<FRecordFrame>* FrameQueuePtr, FRecordActorSaveData& OutGhostSaveData, TArray<FComponentActiveInterval>& OutComponentIntervals, const float& BaseTime);
	void BuildInitialComponentStructure(int32 FirstFrameIndex, FRecordActorSaveData& OutGhostSaveData, TArray<FComponentActiveInterval>& OutComponentIntervals);



	/**
	 * @brief Clips each actor’s saved data in ActorSaveDataArray to the last N seconds
	 *        according to the group’s maximum recording time and sampling interval.
	 *
	 * @param ActorSaveDataArray    The array of FRecordActorSaveData to be processed.
	 * @param MaxGroupRecordTime    The maximum recording duration for the entire group (in seconds).
	 * @param SamplingInterval      The sampling interval used when recording (in seconds).
	 */	
	void ClipActorSaveDataByGroup(TArray<FRecordActorSaveData>& Actors, float MaxGroupRecordTime, float SamplingInterval);
};
