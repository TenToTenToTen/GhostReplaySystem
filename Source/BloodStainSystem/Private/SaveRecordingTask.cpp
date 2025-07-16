/*
* Copyright 2025 TenToTen, All Rights Reserved.
*/



#include "SaveRecordingTask.h"
#include "BloodStainFileUtils.h"
#include "BloodStainsystem.h"

void FSaveRecordingTask::DoWork()
{
	if (!BloodStainFileUtils::SaveToFile(SavedData, LevelName, FileName, FileOptions))
	{
		UE_LOG(LogBloodStain, Log, TEXT("FSaveRecordingTask::DoWork() - SaveToFile failed"));
	}
}