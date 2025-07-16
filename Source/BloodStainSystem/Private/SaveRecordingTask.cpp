/*
* Copyright 2025 TenToTen, All Rights Reserved.
*/



#include "SaveRecordingTask.h"
#include "BloodStainFileUtils.h"
#include "BloodStainsystem.h"

void FSaveRecordingTask::DoWork()
{
	if (!FBloodStainFileUtils::SaveToFile(SavedData, FileName, FileOptions))
	{
		UE_LOG(LogBloodStain, Log, TEXT("FSaveRecordingTask::DoWork() - SaveToFile failed"));
	}
}