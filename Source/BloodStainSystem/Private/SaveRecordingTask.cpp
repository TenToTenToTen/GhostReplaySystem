// Fill out your copyright notice in the Description page of Project Settings.


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