// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "OptionTypes.generated.h"

/** 녹화 옵션 구조체 */
USTRUCT(BlueprintType)
struct FBloodStainRecordOptions
{
	GENERATED_BODY()

	/** 최대 녹화 길이(초) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Record")
	float MaxRecordTime = 5.f;

	/** 프레임마다 기록 간격(초) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Record")
	float SamplingInterval = 0.1f; // 10fps

	/** 재생 속도 비율(1.0=실시간) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Replay")
	float PlaybackRate = 1.f;

	/** 재생 완료 시 자동 슬롯 클린업 여부 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Replay")
	bool bIsLooping = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Replay")
	bool bUseGhostMaterial = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Replay")
	bool bTrackAttachmentChanges = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Replay")
	bool bSaveImmediatelyIfGroupEmpty = false;
	
	
	friend FArchive& operator<<(FArchive& Ar, FBloodStainRecordOptions& Data)
	{
		Ar << Data.MaxRecordTime;
		Ar << Data.SamplingInterval;
		Ar << Data.PlaybackRate;
		Ar << Data.bIsLooping;
		Ar << Data.bUseGhostMaterial;
		Ar << Data.bTrackAttachmentChanges;
		Ar << Data.bSaveImmediatelyIfGroupEmpty;
		return Ar;
	}
};

/** Replay Options */
// USTRUCT(BlueprintType)
// struct FBloodStainReplayOptions
// {
// 	GENERATED_BODY()
//
// 	/** 재생 시간(초). 0 이면 전체 재생 */
// 	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Replay")
// 	float ReplayDuration = 0.f;
//
// 	/** 재생 속도 비율(1.0=실시간) */
// 	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Replay")
// 	float PlaybackRate = 1.f;
//
// 	/** 재생 완료 시 자동 슬롯 클린업 여부 */
// 	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Replay")
// 	bool bIsLooping = true;
// };