/*
* Copyright 2025 TenToTen, All Rights Reserved.
*/

#pragma once

#include "CoreMinimal.h"
#include "ReplayCustomUserData.generated.h"

/**
 * User Custom Struct
 * use for add meta-data
 * On StopRecording & Before Write Files, UBloodStainSubSystem::OnBuildRecordingHeader
 */
USTRUCT(BlueprintType)
struct FReplayCustomUserData
{
	GENERATED_BODY()

    UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "BloodStain|Header")
    FString ActorLabel;
	
	/**
	 * If you want to add custom variable, add here (e.g., Description, Character Info, ...)
	 */

	FReplayCustomUserData()
	{
	}

	friend FArchive& operator<<(FArchive& Ar, FReplayCustomUserData& Data)
	{
		Ar << Data.ActorLabel;
		return Ar;
	}
};
