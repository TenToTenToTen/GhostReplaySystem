/*
* Copyright 2025 TenToTen, All Rights Reserved.
*/


#pragma once

#include "CoreMinimal.h"
#include "BloodStainFileOptions.h"
#include "Async/AsyncWork.h"
#include "Async/TaskGraphInterfaces.h"
#include "GhostData.h"

/**
 * Async Task that saves recorded data to a file in the background.
 */
class BLOODSTAINSYSTEM_API FSaveRecordingTask : public FNonAbandonableTask
{
public:
	FRecordSaveData SavedData;
	FString LevelName;
	FString FileName;
	FBloodStainFileOptions FileOptions;

	/** This Delegate will trigger send replay file to server only if it's client */
	FSimpleDelegateGraphTask::FDelegate OnTaskCompleted;
	
	FSaveRecordingTask(FRecordSaveData&& InData, const FString& InLevelName, const FString& InFileName, const FBloodStainFileOptions& InOptions, FSimpleDelegateGraphTask::FDelegate&& InOnTaskCompleted)
		: SavedData(MoveTemp(InData))
		, LevelName(InLevelName)
		, FileName(InFileName)
		, FileOptions(InOptions)
		, OnTaskCompleted(MoveTemp(InOnTaskCompleted))
	{ }

	/** Save the recorded data to a file */
	void DoWork();

	FORCEINLINE TStatId GetStatId() const
	{
		RETURN_QUICK_DECLARE_CYCLE_STAT(FSaveRecordingTask, STATGROUP_ThreadPoolAsyncTasks);
	}
};
