/*
* Copyright 2025 TenToTen, All Rights Reserved.
*/


#include "SaveRecordingTask.h"
#include "BloodStainFileUtils.h"
#include "BloodStainsystem.h"

void FSaveRecordingTask::DoWork()
{
	const bool bSuccess = BloodStainFileUtils::SaveToFile(SavedData, LevelName, FileName, FileOptions);
	FGraphEventRef Task = FFunctionGraphTask::CreateAndDispatchWhenReady([=, LocalOnTaskCompleted = this->OnTaskCompleted]()
	{
		if (bSuccess)
		{
			LocalOnTaskCompleted.ExecuteIfBound();
		}
		else
		{
			UE_LOG(LogBloodStain, Error, TEXT("Async save task failed. Upload will not start."));
		}
	}, TStatId(), nullptr, ENamedThreads::GameThread);
}