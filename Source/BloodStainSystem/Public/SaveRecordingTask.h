#pragma once

#include "CoreMinimal.h"
#include "BloodStainFileOptions.h"
#include "Async/AsyncWork.h"
#include "GhostData.h"

/**
 * Async Task that saves recorded data to a file in the background.
 */
class BLOODSTAINSYSTEM_API FSaveRecordingTask : public FNonAbandonableTask
{
public:
	FRecordSaveData SavedData;
	FString FileName;
	FBloodStainFileOptions FileOptions;
	
	FSaveRecordingTask(FRecordSaveData&& InData, const FString& InFileName, const FBloodStainFileOptions& InOptions)
		: SavedData(MoveTemp(InData))
		, FileName(InFileName)
		, FileOptions(InOptions)
	{ }

	// Save the recorded data to a file
	void DoWork();

	// Required by FNonAbandonableTask
	FORCEINLINE TStatId GetStatId() const
	{
		RETURN_QUICK_DECLARE_CYCLE_STAT(FSaveRecordingTask, STATGROUP_ThreadPoolAsyncTasks);
	}
};
