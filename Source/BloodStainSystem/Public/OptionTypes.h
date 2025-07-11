// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "OptionTypes.generated.h"

// 어떤 양자화 전략을 사용할지 선택하는 열거형
UENUM(BlueprintType)
enum class ETransformQuantizationMethod : uint8
{
	None,                // 양자화 안함 (FTransform, 48바이트)
	Standard_High,       // 표준 양자화 (FQuatFixed48 사용, 약 20바이트)
	Standard_Compact,    // 압축 양자화 (FQuatFixed32 사용, 약 18바이트)
};

// 양자화 옵션을 담는 구조체
USTRUCT(BlueprintType)
struct FQuantizationOptions
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Quantization")
	ETransformQuantizationMethod Method = ETransformQuantizationMethod::None;

	friend FArchive& operator<<(FArchive& Ar, FQuantizationOptions& Options)
	{
		Ar << Options.Method;
		return Ar;
	}
};

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
	FQuantizationOptions QuantizationOptions;
	
	friend FArchive& operator<<(FArchive& Ar, FBloodStainRecordOptions& Data)
	{
		Ar << Data.MaxRecordTime;
		Ar << Data.SamplingInterval;
		Ar << Data.PlaybackRate;
		Ar << Data.bIsLooping;
		Ar << Data.bUseGhostMaterial;
		Ar << Data.QuantizationOptions;
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