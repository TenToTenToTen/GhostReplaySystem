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
	FRecordSavedData SavedData;
	FString FileName;
	FBloodStainFileOptions FileOptions;
	
	FSaveRecordingTask(FRecordSavedData&& InData, const FString& InFileName, const FBloodStainFileOptions& InOptions)
		: SavedData(MoveTemp(InData))
		, FileName(InFileName)
		, FileOptions(InOptions)
	{ }

	// Save the recorded data to a file
	void DoWork();

	// Required by FNonAbandonableTask
	FORCEINLINE TStatId GetStatId() const;
};
